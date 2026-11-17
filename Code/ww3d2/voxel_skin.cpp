// GPL-3.0-or-later. Private per-instance DX8 skin geometry. The shared model,
// materials, collision, and authoritative damage are never modified here.
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <stdexcept>
#include "always.h"
#include "voxel_skin.h"
#include "../VoxelGore/renegade_voxel.h"
#include "../VoxelGore/voxel_draw.h"
#include "../VoxelGore/gore/gorecore.h"
#include "mesh.h"
#include "meshmdl.h"
#include "htree.h"
#include "hlod.h"
#include "rinfo.h"
#include "ww3d.h"
#include "dx8wrapper.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "vertmaterial.h"
#include "matpass.h"
#include "texture.h"
#include "surfaceclass.h"
#include "lightenvironment.h"
#include "sphere.h"
#include "aabox.h"

namespace {
bool enabled=false;
float elapsed=0;
FILE *logFile=NULL;
unsigned logLines=0;
uint64_t serial=1;
uint64_t logicFrame=1;
VoxelSkin::AddPiece addPiece=NULL;
VoxelSkin::MovePiece movePiece=NULL;
VoxelSkin::TerrainRay terrainRay=NULL;
VoxelSkin::OfflineDrawSink offlineSink=NULL;
struct Instance;
void spawnPieces(Instance &state,const std::vector<float> &pose,const Vector3 &direction);
void log(const char *format,...){if(!logFile || logLines++>2000)return;va_list args;va_start(args,format);vfprintf(logFile,format,args);va_end(args);fputc('\n',logFile);}
enum class PerfStage { Hit, Separate, Snapshot, BuildSkin, BuildSoak, Submit, Count };
struct PerfSample { double totalMs=0, peakMs=0; unsigned count=0; };
std::array<PerfSample, size_t(PerfStage::Count)> perf;
float perfAge = 0;
struct PerfScope {
    using Clock = std::chrono::steady_clock;
    PerfStage stage;
    Clock::time_point start;
    explicit PerfScope(PerfStage s): stage(s), start(logFile ? Clock::now() : Clock::time_point{}) {}
    ~PerfScope() {
        if (!logFile) return;
        auto &sample = perf[size_t(stage)];
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        sample.totalMs += ms;
        sample.peakMs = std::max(sample.peakMs, ms);
        ++sample.count;
    }
};
void flushPerf(float dt) {
    if (!logFile) return;
    perfAge += dt;
    if (perfAge < 2.f) return;
    constexpr const char *names[] = {"hit", "separate", "snapshot", "skin", "soak", "submit"};
    bool any = false;
    for (const auto &sample : perf) any |= sample.count != 0;
    if (any) {
        fprintf(logFile, "perf %.2fs (CPU ms total/peak, calls):", perfAge);
        for (size_t i = 0; i < perf.size(); ++i) {
            const auto &sample = perf[i];
            fprintf(logFile, " %s=%.2f/%.2f,%u", names[i], sample.totalMs, sample.peakMs, sample.count);
        }
        const BRVG_Stats memory = BRVG_GetStats();
        fprintf(logFile, " voxel=%.1fMiB meshes=%u templates=%u",
                double(memory.owned_bytes) / (1024.0 * 1024.0), memory.body_count, memory.model_count);
        fputc('\n', logFile);
    }
    fflush(logFile);
    perf = {};
    perfAge = 0;
}
void need(bool value,const char *message){if(!value)throw std::runtime_error(message);}
#include "voxel_soak.inl"
struct Template {
    MeshModelClass *model=NULL;uint64_t identity=0;BRVG_Model core=0;int bones=0;
    SoakSurface soakSurface;
    ~Template(){if(core)BRVG_FreeModel(core);if(model)model->Release_Ref();}
};
std::map<uint64_t,std::shared_ptr<Template>> templates;
bool reclaimUnusedTemplates() {
    bool freed = false;
    for (auto it = templates.begin(); it != templates.end();) {
        if (it->second.use_count() == 1) {
            it = templates.erase(it);
            freed = true;
        } else {
            ++it;
        }
    }
    return freed;
}
bool capacityError() {
    const char *error = BRVG_LastError();
    return strstr(error, "budget") || strstr(error, "full") || strstr(error, "memory reserve");
}
void reclaimCapacity();
uint64_t hash(uint64_t h,const void *data,size_t bytes){const unsigned char *p=(const unsigned char*)data;for(size_t i=0;i<bytes;++i){h^=p[i];h*=1099511628211ull;}return h;}
std::vector<float> palette(RenderObjClass *container) {
    need(container && container->Get_HTree(),"skin has no palette");
    container->Get_Bone_Transform(0); // evaluate this exact container's dirty tree
    const HTreeClass *tree=container->Get_HTree();int n=tree->Num_Pivots();need(n>0 && n<=128,"unsupported bone count");
    std::vector<float> p; p.reserve(n*12);
    for(int i=0;i<n;++i){const Matrix3D &m=tree->Get_Transform(i);for(int r=0;r<3;++r)for(int c=0;c<3;++c)p.push_back(m[r][c]);p.push_back(m[0][3]);p.push_back(m[1][3]);p.push_back(m[2][3]);}
    return p;
}
void enumerate(RenderObjClass *node,std::vector<MeshClass*> &out,int depth=0,bool forceLOD=true) {
    if(!node || depth>12)return;
    if(forceLOD)node->Set_LOD_Level(node->Get_LOD_Count()-1);
    if(node->Class_ID()==RenderObjClass::CLASSID_HLOD){
        HLodClass *lod=static_cast<HLodClass*>(node);int highest=lod->Get_Lod_Count()-1;
        if(highest>=0)for(int i=0;i<lod->Get_Lod_Model_Count(highest);++i){RenderObjClass *child=lod->Get_Lod_Model(highest,i);if(child){enumerate(child,out,depth+1,forceLOD);child->Release_Ref();}}
        for(int i=0;i<lod->Get_Additional_Model_Count();++i){RenderObjClass *child=lod->Get_Additional_Model(i);if(child){enumerate(child,out,depth+1,forceLOD);child->Release_Ref();}}
        return; // Get_Sub_Object enumerates ALL LODs, including invisible ones.
    }
    if(node->Class_ID()==RenderObjClass::CLASSID_MESH){MeshClass *mesh=static_cast<MeshClass*>(node);MeshModelClass *m=mesh->Peek_Model();const char *name=m->Get_Name();
        if(m->Get_Flag(MeshGeometryClass::SKIN) && (!name || strncmp(name,"G_THERMAL_",10)) && mesh->Is_Not_Hidden_At_All() && (!name || !strstr(name,"SHADOW")) && mesh->Get_Container() && mesh->Get_Container()->Get_HTree())out.push_back(mesh);
        return;
    }
    for(int i=0;i<node->Get_Num_Sub_Objects();++i){RenderObjClass *child=node->Get_Sub_Object(i);if(child){enumerate(child,out,depth+1,forceLOD);child->Release_Ref();}}
}
std::shared_ptr<Template> makeTemplate(MeshClass *mesh,bool allowReclaim=true) {
    MeshModelClass *model=mesh->Peek_Model();need(!model->WoundRestVertices && !model->WoundSkinInfluences,"legacy-mutated geometry");
    const HTreeClass *original=mesh->Get_Container()->Get_HTree();HTreeClass rest(*original);
    for(int b=0;b<rest.Num_Pivots();++b)if(rest.Is_Bone_Captured(b))rest.Release_Bone(b);
    rest.Base_Update(Matrix3D::Identity);
    int nv=model->Get_Vertex_Count(),nt=model->Get_Polygon_Count(),nb=rest.Num_Pivots();
    need(nv>0&&nv<65535&&nt>0&&nt<1000000&&nb>0&&nb<=128,"skin limits");
    const Vector3 *local=model->Get_Vertex_Array(),*normal=model->Get_Vertex_Normal_Array();const uint16 *links=model->Get_Vertex_Bone_Links();const TriIndex *polys=model->Get_Polygon_Array();need(links && local && normal && polys,"missing skin arrays");
    std::vector<float> positions(nv*3),normals(nv*3),base;std::vector<int32_t> parents(nb);std::vector<char> names(nb*32,0);std::vector<uint32_t> indices(nt*3);
    for(int b=0;b<nb;++b){parents[b]=b?rest.Get_Parent_Index(b):-1;const char *name=rest.Get_Bone_Name(b);if(name)strncpy(&names[b*32],name,31);const Matrix3D &m=rest.Get_Transform(b);for(int r=0;r<3;++r)for(int c=0;c<3;++c)base.push_back(m[r][c]);base.push_back(m[0][3]);base.push_back(m[1][3]);base.push_back(m[2][3]);}
    for(int v=0;v<nv;++v){need(links[v]<nb,"bone index");Vector3 p,n;const Matrix3D &m=rest.Get_Transform(links[v]);Matrix3D::Transform_Vector(m,local[v],&p);n=m.Rotate_Vector(normal[v]);for(int k=0;k<3;++k){positions[v*3+k]=p[k];normals[v*3+k]=n[k];}}
    for(int t=0;t<nt;++t)for(int k=0;k<3;++k)indices[t*3+k]=polys[t][k];
    uint64_t key=1469598103934665603ull;key=hash(key,positions.data(),positions.size()*4);key=hash(key,normals.data(),normals.size()*4);key=hash(key,indices.data(),indices.size()*4);key=hash(key,links,nv*2);key=hash(key,base.data(),base.size()*4);if(!key)key=++serial;
    auto found=templates.find(key);if(found!=templates.end())return found->second;
    BRVG_SurfaceDesc surface{};surface.identity=key;surface.vertex_count=nv;surface.triangle_count=nt;surface.rest_xyz=positions.data();surface.rest_normals=normals.data();surface.bones=links;surface.indices=indices.data();
    BRVG_ModelDesc desc{};desc.size=sizeof(desc);desc.version=BRVG_ABI_VERSION;desc.bone_count=nb;desc.surface_count=1;desc.rest_palette=base.data();desc.parents=parents.data();desc.bone_names=names.data();desc.surfaces=&surface;
    bool headOnly=strstr(model->Get_Name(),".HEAD") || strstr(original->Get_Name(),"_HEAD");desc.rigid_head=headOnly?1:0;desc.cell_m=headOnly?.0075f:.01f;
    auto result=std::make_shared<Template>();
    result->core=BRVG_CreateModel(&desc);
    if (!result->core && allowReclaim && capacityError()) {
        reclaimCapacity();
        result->core=BRVG_CreateModel(&desc);
    }
    need(result->core!=0,BRVG_LastError());
    result->identity=key;result->model=model;model->Add_Ref();result->bones=nb;
    buildSoakSurface(result->soakSurface, *model, rest, positions);
    templates[key]=result;
    log("template %s: %d vertices, %d triangles, %d bones, cell %.4fm%s",model->Get_Name(),nv,nt,nb,desc.cell_m,headOnly?" HEAD":"");return result;
}
struct MaterialGroup {
    VertexMaterialClass *material=NULL;TextureClass *texture0=NULL,*texture1=NULL;ShaderClass shader;
    MaterialGroup(VertexMaterialClass *m,TextureClass *a,TextureClass *b,ShaderClass s):material(m),texture0(a),texture1(b),shader(s){if(material)material->Add_Ref();if(a)a->Add_Ref();if(b)b->Add_Ref();}
    ~MaterialGroup(){if(material)material->Release_Ref();if(texture0)texture0->Release_Ref();if(texture1)texture1->Release_Ref();}
};
struct Pass {
    BRVG_RenderPacket *packet=NULL;std::vector<std::unique_ptr<MaterialGroup>> groups;
    ~Pass(){BRVG_FreePacket(packet);}
};
struct SavedState {RenderStateStruct state;SavedState(){DX8Wrapper::Get_Render_State(state);}~SavedState(){DX8Wrapper::Set_Render_State(state);}};
struct SnapshotOwner {const BRVG_Snapshot *p;explicit SnapshotOwner(const BRVG_Snapshot *s):p(s){}~SnapshotOwner(){BRVG_ReleaseDraw(p);}};
struct SoakBatch {
    std::vector<BRVG_RenderVertex> vertices;
    std::vector<uint16_t> indices;
};
using StainBatches = std::array<SoakBatch, 2>;
struct HeatPatch {
    unsigned region;
    int stage;
    BloodSoak stain;
};
struct FrameDraw {
    uint64_t tick=0;
    uint64_t woundRevision=0;
    const BRVG_Snapshot *snapshot=nullptr;
    std::vector<float> pose;
    bool alternateMaterial=false;
    std::vector<std::unique_ptr<Pass>> passes;
    StainBatches soaks;
    std::array<StainBatches, 3> burns;
    std::vector<uint8_t> burnFleshColors;
    bool soaksBuilt=false, burnsBuilt=false;
    bool soaksSettled=false;
    ~FrameDraw() { BRVG_ReleaseDraw(snapshot); }
};
struct Instance {
    RenderObjClass *actor=nullptr;
    MeshClass *mesh=nullptr;
    MeshModelClass *model=nullptr;
    std::shared_ptr<Template> source;
    BRVG_Body body=0;
    float lastSeen=0;
    bool firstDraw=true, reportedFallback=false, wounded=false, enteredScene=false;
    std::vector<BloodSoak> soaks;
    std::vector<HeatPatch> burns;
    std::array<uint8_t,20> burnErosionStages{};
    std::unique_ptr<FrameDraw> frameDraw;
    uint64_t woundRevision=0, loggedFailure=0;
    bool corpse=false;
    float refreshAfter=0;
    bool cutPending=false;
    float cutDue=0, lastCutCheck=-1;
    Vector3 cutDirection;
    ~Instance() {
        frameDraw.reset();
        if (body) BRVG_FreeBody(body);
        if (mesh) mesh->Release_Ref();
        if (actor) actor->Release_Ref();
    }
};
std::map<MeshClass*,std::unique_ptr<Instance>> instances;

void reclaimCapacity()
{
    reclaimUnusedTemplates();
    std::vector<Instance*> idle;
    for (auto &entry : instances) {
        auto &state = *entry.second;
        if (state.body && !state.cutPending && elapsed - state.lastSeen >= 2.f) idle.push_back(&state);
    }
    std::sort(idle.begin(), idle.end(), [](const Instance *a, const Instance *b) {
        return a->lastSeen < b->lastSeen;
    });
    unsigned compacted = 0;
    for (Instance *state : idle) {
        if (!BRVG_CompactBody(state->body)) continue;
        state->frameDraw.reset();
        if (++compacted == 4) break;
    }
    if (compacted) log("voxel memory: compacted %u offscreen wounded meshes", compacted);
}

bool unchangedUniformPasses(MeshModelClass &model, const FrameDraw &frame) {
    if (size_t(model.Get_Pass_Count()) != frame.passes.size()) return false;
    for (int p = 0; p < model.Get_Pass_Count(); ++p) {
        const Pass &pass = *frame.passes[p];
        if (pass.groups.size() != 1 || model.Has_Material_Array(p) ||
            model.Has_Shader_Array(p) || model.Has_Texture_Array(p,0) || model.Has_Texture_Array(p,1)) return false;
        const MaterialGroup &group = *pass.groups.front();
        if (group.material != model.Peek_Material(0,p) ||
            group.texture0 != model.Peek_Texture(0,p,0) ||
            group.texture1 != model.Peek_Texture(0,p,1) ||
            group.shader.Get_Bits() != model.Get_Shader(0,p).Get_Bits()) return false;
        const Vector2 *uv0 = model.Get_UV_Array(p,0), *uv1 = model.Get_UV_Array(p,1);
        const unsigned *colors = model.Get_DCG_Array(p);
        const auto *vertices = BRVG_GetPacket(pass.packet)->vertices;
        for (int v = 0; v < model.Get_Vertex_Count(); ++v) {
            const auto &old = vertices[v];
            if (old.diffuse != (colors ? colors[v] : 0xffffffffu) ||
                old.uv0[0] != (uv0 ? uv0[v].X : 0.f) || old.uv0[1] != (uv0 ? uv0[v].Y : 0.f) ||
                old.uv1[0] != (uv1 ? uv1[v].X : 0.f) || old.uv1[1] != (uv1 ? uv1[v].Y : 0.f)) return false;
        }
    }
    return true;
}
std::unique_ptr<Pass> preparePass(MeshClass *mesh,const BRVG_SurfaceView &cut,const std::vector<Vector3> &position,const std::vector<Vector3> &normal,int pass,uint64_t identity) {
    auto output=std::make_unique<Pass>();MeshModelClass *model=mesh->Peek_Model();int nv=model->Get_Vertex_Count(),nt=model->Get_Polygon_Count();const TriIndex *polys=model->Get_Polygon_Array();
    std::vector<uint32_t> indices(nt*3),groups(nt);std::vector<BRVG_RenderVertex> vertices(nv);const Vector2 *uv0=model->Get_UV_Array(pass,0),*uv1=model->Get_UV_Array(pass,1);const unsigned *colors=model->Get_DCG_Array(pass);
    for(int v=0;v<nv;++v){BRVG_RenderVertex &dst=vertices[v];for(int k=0;k<3;++k){dst.xyz[k]=position[v][k];dst.normal[k]=normal[v][k];}dst.diffuse=colors?colors[v]:0xffffffffu;for(int k=0;k<2;++k){dst.uv0[k]=uv0?uv0[v][k]:0;dst.uv1[k]=uv1?uv1[v][k]:0;}}
    for(int t=0;t<nt;++t){for(int k=0;k<3;++k)indices[t*3+k]=polys[t][k];VertexMaterialClass *material=model->Peek_Material(polys[t][0],pass);need(material!=NULL,"missing vertex material");
        TextureClass *a=model->Peek_Texture(t,pass,0),*b=model->Peek_Texture(t,pass,1);ShaderClass shader=model->Get_Shader(t,pass);
        need(shader.Get_Src_Blend_Func()==ShaderClass::SRCBLEND_ONE && shader.Get_Dst_Blend_Func()==ShaderClass::DSTBLEND_ZERO,"sorted/translucent skin fallback");
        // W3D categories also split by vertex material. Do not guess when a
        // triangle has different materials on its corners.
        for(int k=1;k<3;++k)need(model->Peek_Material(polys[t][k],pass)==material,"mixed corner materials");
        unsigned group=0;for(;group<output->groups.size();++group){auto &g=*output->groups[group];if(g.material==material&&g.texture0==a&&g.texture1==b&&g.shader.Get_Bits()==shader.Get_Bits())break;}
        if(group==output->groups.size())output->groups.emplace_back(new MaterialGroup(material,a,b,shader));groups[t]=group;
    }
    output->packet=BRVG_BuildPacket(&cut,identity,nv,nt,indices.data(),vertices.data(),groups.data());need(output->packet!=NULL,"private packet budget or geometry mismatch");return output;
}
class UploadedVertices {
    const BRVG_RenderVertex *vertices;
    unsigned count;
    std::unique_ptr<DynamicVBAccessClass> buffer;
public:
    UploadedVertices(const BRVG_RenderVertex *v, unsigned n): vertices(v), count(n) {
        need(n > 0 && n < 65535, "private VB budget");
        if (offlineSink) return;
        DX8Wrapper::Set_Vertex_Buffer(nullptr);
        buffer = std::make_unique<DynamicVBAccessClass>(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, uint16_t(n));
        {
            DynamicVBAccessClass::WriteLockClass lock(buffer.get());
            static_assert(sizeof(VertexFormatXYZNDUV2) == sizeof(BRVG_RenderVertex), "DX8 vertex layout");
            memcpy(lock.Get_Formatted_Vertex_Array(), v, n * sizeof(*v));
        }
        DX8Wrapper::Set_Vertex_Buffer(*buffer);
    }
    void Draw(const uint16_t *indices, unsigned indexCount) {
        // The index-count API is also 16-bit. Split before narrowing.
        for (unsigned offset = 0; offset < indexCount;) {
            const unsigned n = std::min(65532u, indexCount - offset);
            need(n % 3 == 0, "triangle index count");
            if (offlineSink) {
                need(offlineSink(vertices, count, indices + offset, n), "offline draw rejected");
            } else {
                DynamicIBAccessClass ib(BUFFER_TYPE_DYNAMIC_DX8, uint16_t(n));
                {
                    DynamicIBAccessClass::WriteLockClass lock(&ib);
                    memcpy(lock.Get_Index_Array(), indices + offset, n * sizeof(*indices));
                }
                DX8Wrapper::Set_Index_Buffer(ib, 0);
                DX8Wrapper::Draw_Triangles(0, uint16_t(n / 3), 0, uint16_t(count));
            }
            offset += n;
        }
    }
};
void uploadAndDraw(const BRVG_RenderVertex *vertices,unsigned nv,const uint16_t *indices,unsigned count) {
    if (!count) return;
    UploadedVertices uploaded(vertices, nv);
    uploaded.Draw(indices, count);
}
void drawPacket(Pass &pass) {
    const BRVG_PacketView *packet=BRVG_GetPacket(pass.packet);
    if (!packet->index_count) return;
    UploadedVertices uploaded(packet->vertices, packet->vertex_count);
    for(unsigned i=0;i<packet->range_count;++i){const BRVG_DrawRange &range=packet->ranges[i];MaterialGroup &g=*pass.groups[range.material_group];ShaderClass shader=g.shader;shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);DX8Wrapper::Set_Shader(shader);DX8Wrapper::Set_Material(g.material);DX8Wrapper::Set_Texture(0,g.texture0);DX8Wrapper::Set_Texture(1,g.texture1);uploaded.Draw(packet->indices+range.first_index,range.index_count);}
}

TextureClass *soakTexture = nullptr;

static TextureClass *burnTextures[3] = {};

TextureClass *getStainTexture(int stage)
{
    TextureClass *&texture = stage ? burnTextures[stage-1] : soakTexture;
    if (texture) return texture;
    constexpr int size = 64;
    auto *surface = new SurfaceClass(size, size, WW3D_FORMAT_A8R8G8B8);
    if (!surface->Peek_D3D_Surface()) {
        surface->Release_Ref();
        return nullptr;
    }
    int pitch = 0;
    auto *pixels = static_cast<unsigned char*>(surface->Lock(&pitch));
    if (!pixels) {
        surface->Release_Ref();
        return nullptr;
    }
    for (int y = 0; y < size; ++y) {
        auto *row = reinterpret_cast<uint32_t*>(pixels + y * pitch);
        for (int x = 0; x < size; ++x) {
            const float u = ((x + .5f) / size - .5f) / .46f;
            const float v = ((y + .5f) / size - .5f) / .46f;
            const float angle = std::atan2(v, u);
            const float radius = std::sqrt(u * u + v * v);
            const float edge = .88f + .075f * std::sin(angle * 5) + .045f * std::cos(angle * 11);
            const float t = std::clamp((edge - radius) / .27f, 0.f, 1.f);
            const float grain = .87f + .13f * std::cos(u * 41 + v * 29) * std::sin(v * 53 - u * 17);
            const float coverage = t * t * (3 - 2 * t) * grain;
            // Multiply the already-lit skin. White is transparent in this
            // blend mode, and the center darkens green/blue more than red.
            const float heat = stage ? coverage * (.31f + .17f * stage) *
                (.85f + .15f * std::sin(u*71+v*54) * std::cos(u*31-v*93)) : 0;
            const uint32_t r = uint32_t(255 * (1 - (stage ? .82f * heat : .30f * coverage)));
            const uint32_t g = uint32_t(255 * (1 - (stage ? .98f * heat : .71f * coverage)));
            const uint32_t b = uint32_t(255 * (1 - (stage ? heat : .67f * coverage)));
            row[x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
    surface->Unlock();
    texture = new TextureClass(surface, TextureClass::MIP_LEVELS_1);
    surface->Release_Ref();
    if (!texture->Peek_DX8_Texture()) {
        REF_PTR_RELEASE(texture);
        return nullptr;
    }
    texture->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
    texture->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
    return texture;
}

void submitSoak(const SoakBatch &batch, bool fog, TextureClass *texture)
{
    if (batch.indices.empty()) return;
    auto *material = new VertexMaterialClass;
    material->Set_Lighting(false);
    ShaderClass shader;
    shader.Set_Depth_Compare(ShaderClass::PASS_EQUAL);
    shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
    shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
    shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
    shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
    shader.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
    shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    if (fog) shader.Set_Fog_Func(ShaderClass::FOG_WHITE);
    DX8Wrapper::Set_Shader(shader);
    DX8Wrapper::Set_Material(material);
    material->Release_Ref();
    DX8Wrapper::Set_Texture(0, texture);
    DX8Wrapper::Set_Texture(1, nullptr);
    uploadAndDraw(batch.vertices.data(), unsigned(batch.vertices.size()),
                  batch.indices.data(), unsigned(batch.indices.size()));
}

void buildSoaks(const Pass &pass, const BRVG_SurfaceView &cut, MeshModelClass &model,
                const SoakSurface &surface, const std::vector<BloodSoak> &soaks,
                StainBatches &batches,const std::vector<HeatPatch> *burns = nullptr,int stage = 0)
{
    if (burns ? burns->empty() : soaks.empty()) return;
    PerfScope measure(PerfStage::BuildSoak);
    const BRVG_PacketView &packet = *BRVG_GetPacket(pass.packet);

    // Index only surviving triangles in the first color/depth pass. The
    // overlay reuses the exact posed positions, including cut subdivisions.
    std::vector<int> first(model.Get_Polygon_Count(), -1), next(packet.index_count / 3);
    std::vector<uint8_t> fogged(packet.index_count / 3);
    for (unsigned r = 0; r < packet.range_count; ++r) {
        const BRVG_DrawRange &range = packet.ranges[r];
        const ShaderClass shader = pass.groups[range.material_group]->shader;
        if (shader.Get_Depth_Mask() != ShaderClass::DEPTH_WRITE_ENABLE ||
            shader.Get_Color_Mask() != ShaderClass::COLOR_WRITE_ENABLE) continue;
        for (unsigned i = range.first_index / 3; i < (range.first_index + range.index_count) / 3; ++i) {
            const unsigned source = packet.source_triangles[i];
            next[i] = first[source];
            first[source] = int(i);
            fogged[i] = shader.Uses_Fog();
        }
    }
    const TriIndex *triangles = model.Get_Polygon_Array();
    constexpr unsigned maxDrawVertices = 48000;
    unsigned submittedVertices = 0;
    auto project = [&](const BloodSoak &soak, const Vector3 &center,
                       const Vector3 &uAxis, const Vector3 &vAxis) {
        for (int source : soak.faces) {
            for (int i = first[source]; i >= 0; i = next[i]) {
                if (submittedVertices >= maxDrawVertices) return;
                BRVG_RenderVertex corners[3];
                for (int k = 0; k < 3; ++k) {
                    const unsigned vertex = packet.indices[3 * i + k];
                    Vector3 rest;
                    if (vertex < unsigned(model.Get_Vertex_Count())) {
                        rest = surface.rest[vertex];
                    } else {
                        const unsigned sub = vertex - cut.vertex_count;
                        const float *w = cut.sub + size_t(sub / 3) * 10 + 1 + (sub % 3) * 3;
                        rest = interpolate(surface.rest, triangles[source], Vector3(w[0], w[1], w[2]));
                    }
                    const Vector3 d = rest - center;
                    corners[k] = packet.vertices[vertex];
                    corners[k].uv0[0] = .5f + Vector3::Dot_Product(d, uAxis);
                    corners[k].uv0[1] = .5f + Vector3::Dot_Product(d, vAxis);
                }
                const auto outside = [&](int axis) {
                    const float a = corners[0].uv0[axis], b = corners[1].uv0[axis], c = corners[2].uv0[axis];
                    return std::min({a, b, c}) >= 1 || std::max({a, b, c}) <= 0;
                };
                if (outside(0) || outside(1)) continue;
                submittedVertices += 3;
                SoakBatch &batch = batches[fogged[i]];
                for (const auto &vertex : corners) {
                    batch.indices.push_back(uint16_t(batch.vertices.size()));
                    batch.vertices.push_back(vertex);
                }
            }
        }
    };
    auto drawPatch = [&](const BloodSoak &soak) {
        if (submittedVertices >= maxDrawVertices) return;
        const float age = elapsed - soak.started;
        const float spread = soakEase(age / SoakSeconds);
        const float radius = soak.initialRadius + (soak.finalRadius - soak.initialRadius) * spread;
        const float scale = .46f / radius;
        const Vector3 center = interpolate(surface.rest, triangles[soak.anchor.triangle], soak.anchor.weights);
        project(soak, center, soak.tangent * scale, soak.bitangent * (scale / .88f));

        const float run = soak.runoff * soakEase((age - .20f) / (SoakSeconds - .20f));
        if (run > .003f) {
            const float halfLength = .5f * run + .009f;
            project(soak, center + soak.downhill * (.5f * run),
                    soak.acrossRun * (.46f / (.009f + .004f * spread)),
                    soak.downhill * (.46f / halfLength));
        }
    };
    if (burns) {
        for (const auto &patch : *burns)
            if (patch.stage == stage) drawPatch(patch.stain);
    } else {
        for (const BloodSoak &soak : soaks) drawPatch(soak);
    }
}

void drawSoaks(const StainBatches &batches, int stage = 0)
{
    if (batches[0].indices.empty() && batches[1].indices.empty()) return;
    TextureClass *texture = offlineSink ? nullptr : getStainTexture(stage);
    if (offlineSink || texture) {
        submitSoak(batches[0], false, texture);
        submitSoak(batches[1], true, texture);
    }
}

void tintBurnedFlesh(const BRVG_DrawView &draw,const BRVG_PacketView &skin,MeshModelClass &model,
                     const std::vector<HeatPatch> &burns,std::vector<uint8_t> &colors)
{
    if (burns.empty() || !draw.flesh_vertex_count) return;
    struct HotSpot { Vector3 center; float reach, intensity; };
    std::vector<HotSpot> spots;
    for (const HeatPatch &patch : burns) {
        if (patch.stage < 2) continue;
        const auto &anchor = patch.stain.anchor;
        const TriIndex &triangle = model.Get_Polygon_Array()[anchor.triangle];
        Vector3 center(0,0,0);
        for (int k=0;k<3;++k) {
            const auto &v = skin.vertices[triangle[k]];
            center += Vector3(v.xyz[0],v.xyz[1],v.xyz[2])*anchor.weights[k];
        }
        spots.push_back({center,std::min(.17f,patch.stain.finalRadius),patch.stage == 3 ? .70f : .40f});
    }
    if (spots.empty()) return;
    colors.assign(draw.flesh_rgba,draw.flesh_rgba+size_t(draw.flesh_vertex_count)*4);
    for (unsigned i=0;i<draw.flesh_vertex_count;++i) {
        const float *xyz = draw.flesh_xyz+i*3;
        Vector3 point(xyz[0],xyz[1],xyz[2]);
        float heat = 0;
        for (const auto &spot : spots)
            heat = std::max(heat,spot.intensity*soakEase(1.f-(point-spot.center).Length2()/(spot.reach*spot.reach)));
        colors[i*4] = uint8_t(colors[i*4]*(1.f-heat*.81f));
        colors[i*4+1] = uint8_t(colors[i*4+1]*(1.f-heat*.93f));
        colors[i*4+2] = uint8_t(colors[i*4+2]*(1.f-heat));
    }
}

void recordSoaks(Instance &state, const BRVG_HitResult &hit, const std::vector<float> &pose,
                 const Vector3 &point, const Vector3 &direction, const Vector3 *contactNormal,
                 float entryRim, float exitRim)
{
    if (state.soaks.size() >= MaxSoaks) return;
    const SoakSurface &surface = state.source->soakSurface;
    MeshModelClass &model = *state.model;
    std::vector<Vector3> posed(model.Get_Vertex_Count()), normals(posed.size());
    state.mesh->Get_Deformed_Vertices(posed.data(), normals.data());
    const int side = hit.bone >= 0 && hit.bone < state.source->bones ? surface.legSide[hit.bone] : 0;
    Vector3 ray = direction;
    ray.Normalize();
    auto posePoint = [&](const float *rest, Vector3 &world) {
        float p[3];
        if (!BRVG_PosePoint(state.body, pose.data(), state.source->bones, hit.bone, rest, p)) return false;
        world.Set(p[0], p[1], p[2]);
        return true;
    };
    Vector3 entryPoint;
    SoakAnchor entry;
    if (posePoint(hit.rest_entry, entryPoint) && (entryPoint - point).Length2() < .22f * .22f) {
        entry = locateSoak(model, surface, posed, normals, entryPoint, ray, contactNormal, side, .065f);
    }
    if (entry.triangle < 0) entry = locateSoak(model, surface, posed, normals, point, ray, contactNormal, side, .14f);
    addSoak(state.soaks, model, surface, posed, entry, side, entryRim, elapsed);

    Vector3 exitPoint;
    if (!hit.exited || !posePoint(hit.rest_exit, exitPoint)) return;
    const SoakAnchor exit = locateSoak(model, surface, posed, normals, exitPoint, -ray, nullptr, side, .065f);
    if (exit.triangle < 0 || exit.triangle == entry.triangle) return;
    const TriIndex *triangles = model.Get_Polygon_Array();
    if (entry.triangle >= 0 &&
        (interpolate(surface.rest, triangles[entry.triangle], entry.weights) -
         interpolate(surface.rest, triangles[exit.triangle], exit.weights)).Length2() < .025f * .025f) return;
    addSoak(state.soaks, model, surface, posed, exit, side, exitRim, elapsed);
}
void flesh(const BRVG_DrawView &draw,RenderInfoClass &info,bool base) {
    if(!draw.flesh_vertex_count||!draw.flesh_index_count)return;
    std::vector<BRVG_RenderVertex> vertices(draw.flesh_vertex_count);std::vector<uint16_t> indices(draw.flesh_index_count);
    for(unsigned v=0;v<draw.flesh_vertex_count;++v){auto &out=vertices[v];for(int k=0;k<3;++k){out.xyz[k]=draw.flesh_xyz[v*3+k];out.normal[k]=draw.flesh_normals[v*3+k];}const uint8_t *c=draw.flesh_rgba+v*4;out.diffuse=0xff000000u|(uint32_t(c[0])<<16)|(uint32_t(c[1])<<8)|c[2];}
    for(unsigned i=0;i<draw.flesh_index_count;++i){need(draw.flesh_indices[i]>=0 && unsigned(draw.flesh_indices[i])<draw.flesh_vertex_count,"flesh index");indices[i]=(uint16_t)draw.flesh_indices[i];}
    if(base){VertexMaterialClass *material=new VertexMaterialClass;material->Set_Lighting(false);material->Set_Diffuse_Color_Source(VertexMaterialClass::COLOR1);ShaderClass shader;shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);shader.Set_Depth_Compare(ShaderClass::PASS_LEQUAL);shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_ENABLE);DX8Wrapper::Set_Shader(shader);DX8Wrapper::Set_Material(material);DX8Wrapper::Set_Texture(0,NULL);DX8Wrapper::Set_Texture(1,NULL);uploadAndDraw(vertices.data(),(unsigned)vertices.size(),indices.data(),(unsigned)indices.size());material->Release_Ref();}
    for(int i=0;i<info.Additional_Pass_Count();++i){MaterialPassClass *pass=info.Peek_Additional_Pass(i);pass->Install_Materials();ShaderClass shader=pass->Peek_Shader();shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);DX8Wrapper::Set_Shader(shader);uploadAndDraw(vertices.data(),(unsigned)vertices.size(),indices.data(),(unsigned)indices.size());}
}
}
#include "voxel_pieces.inl"
namespace VoxelSkin {
void Set_Piece_Callbacks(AddPiece add,MovePiece move,TerrainRay ray){addPiece=add;movePiece=move;terrainRay=ray;}
void Set_Offline_Draw_Sink(OfflineDrawSink sink){offlineSink=sink;}
void Configure(bool active,const char *path){Shutdown();enabled=active;if(active && path){std::string name(path);size_t slash=name.find_last_of("/\\");name=(slash==std::string::npos?std::string():name.substr(0,slash+1))+"voxel_gore.log";logFile=fopen(name.c_str(),"w");logLines=0;log("Voxel gore enabled: SP ballistic cosmetics, 1cm, private DX8 draws");}}
void Shutdown(){clearPieces();instances.clear();templates.clear();BRVG_Shutdown();REF_PTR_RELEASE(soakTexture);for(auto &texture:burnTextures)REF_PTR_RELEASE(texture);if(logFile){fclose(logFile);logFile=NULL;}enabled=false;elapsed=0;perf={};perfAge=0;}
void Clear_Level(){clearPieces();instances.clear();templates.clear();BRVG_Shutdown();elapsed=0;log("level state cleared");if(logFile)fflush(logFile);perf={};perfAge=0;}
void Tick(float dt)
{
    if (!enabled) return;
    const float step = std::max(0.f, dt);
    flushPerf(step);
    elapsed += step;
    BRVG_BeginFrame(++logicFrame);
    tickPieces(step);
    Instance *nextCut = nullptr;
    for (auto it = instances.begin(); it != instances.end();) {
        Instance &state = *it->second;
        const bool inScene = state.actor->Peek_Scene() != nullptr;
        state.enteredScene |= inScene;
        // A culled corpse still owns its stains. Drop them when its model
        // leaves the scene rather than when it has been offscreen for a while.
        if ((!inScene && (state.enteredScene || elapsed - state.lastSeen > 30)) ||
            !state.mesh->Get_Container() || state.mesh->Peek_Model() != state.model) {
            it = instances.erase(it);
        } else {
            // Keep voxel edits for an offscreen corpse. The large prepared
            // draw buffers can be recreated from its last published snapshot.
            if (state.frameDraw && state.frameDraw->woundRevision == state.woundRevision &&
                elapsed - state.lastSeen > .5f) state.frameDraw.reset();
            if (state.cutPending && state.cutDue <= elapsed &&
                (!nextCut || state.cutDue < nextCut->cutDue)) nextCut = &state;
            ++it;
        }
    }
    if (nextCut) {
        nextCut->cutPending = false;
        nextCut->lastCutCheck = elapsed;
        ++nextCut->woundRevision;
        PerfScope measure(PerfStage::Separate);
        try {
            spawnPieces(*nextCut, palette(nextCut->mesh->Get_Container()), nextCut->cutDirection);
        } catch (const std::exception &e) {
            log("separation skipped: %s", e.what());
        }
    }
}
bool Manages(const RenderObjClass *actor){for(int depth=0;actor && depth<12;++depth,actor=actor->Get_Container())for(auto &row:instances)if(row.second->actor==actor)return true;return false;}
bool Is_Enabled(){return enabled;}

bool Prewarm(RenderObjClass *actor)
{
    // Leave room for private wounded bodies, draw snapshots and detached parts.
    constexpr uint64_t WarmBudget = 48ull * 1024 * 1024;
    if (!enabled || BRVG_GetStats().owned_bytes >= WarmBudget) return false;

    std::vector<MeshClass*> meshes;
    enumerate(actor,meshes,0,false);
    for (MeshClass *mesh : meshes) {
        const auto before=BRVG_GetStats();
        try {
            makeTemplate(mesh,false);
            if (BRVG_GetStats().model_count>before.model_count) {
                log("prewarmed %s during level load",mesh->Get_Name());
            }
        } catch (const std::exception &e) {
            log("prewarm skipped %s: %s",mesh->Get_Name(),e.what());
        }
        if (BRVG_GetStats().owned_bytes >= WarmBudget) return false;
    }
    return true;
}

static Instance &meshInstance(RenderObjClass *actor,MeshClass *mesh)
{
    auto it = instances.find(mesh);
    if (it != instances.end() && it->second->model == mesh->Peek_Model()) return *it->second;
    if (it != instances.end()) instances.erase(it);
    auto record = std::make_unique<Instance>();
    record->source = makeTemplate(mesh);
    record->actor = actor;
    actor->Add_Ref();
    record->mesh = mesh;
    mesh->Add_Ref();
    record->model = mesh->Peek_Model();
    record->lastSeen = elapsed;
    record->enteredScene = actor->Peek_Scene() != nullptr;
    return *instances.emplace(mesh,std::move(record)).first->second;
}

static void ensureBody(Instance &state)
{
    if (state.body) return;
    state.body = BRVG_CreateBody(state.source->core);
    if (!state.body && capacityError()) {
        reclaimCapacity();
        state.body = BRVG_CreateBody(state.source->core);
    }
    while (!state.body && capacityError() && !pieces.empty()) {
        discardPiece(0);
        reclaimUnusedTemplates();
        state.body = BRVG_CreateBody(state.source->core);
    }
    need(state.body != 0,BRVG_LastError());
}

static int erode(Instance &state,int bone,const Vector3 &point,const Vector3 &direction,
                 float radius,float depth,bool skinOnly,bool limbCut = false)
{
    ensureBody(state);
    const auto pose = palette(state.mesh->Get_Container());
    const float p[] = {point.X,point.Y,point.Z}, d[] = {direction.X,direction.Y,direction.Z};
    int cells = BRVG_Erode(state.body,pose.data(),state.source->bones,bone,p,d,radius,depth,skinOnly,limbCut);
    if (!cells && capacityError()) {
        reclaimCapacity();
        cells = BRVG_Erode(state.body,pose.data(),state.source->bones,bone,p,d,radius,depth,skinOnly,limbCut);
    }
    if (cells) {
        state.wounded = true;
        ++state.woundRevision;
    }
    return cells;
}

static void scheduleSeparation(Instance &state,const Vector3 &direction)
{
    if (!addPiece) return;
    if (!state.cutPending) {
        state.cutPending = true;
        state.cutDue = std::max(elapsed + .06f,state.lastCutCheck + (state.corpse ? .25f : .15f));
    }
    state.cutDirection = direction;
}

bool Blast(RenderObjClass *actor,const Vector3 &point,const Vector3 &direction,
           const char *boneName,float radius,float depth,bool sever,bool corpse)
{
    if (!enabled || !actor || !boneName) return false;
    try {
        PerfScope measure(PerfStage::Hit);
        std::vector<MeshClass*> meshes;
        enumerate(actor,meshes);
        MeshClass *best = nullptr;
        const float reach = sever ? radius : std::max(.18f,radius);
        float closest = reach*reach;
        int hitBone = -1;
        Vector3 contact = point;
        for (auto *mesh : meshes) {
            const auto *tree = mesh->Get_Container()->Get_HTree();
            const int bone = tree->Get_Bone_Index(boneName);
            const int count = mesh->Peek_Model()->Get_Vertex_Count();
            const auto *links = mesh->Peek_Model()->Get_Vertex_Bone_Links();
            if (bone <= 0 || count <= 0 || count >= 65535 || !links) continue;
            mesh->Get_Container()->Get_Bone_Transform(0);
            std::vector<Vector3> posed(count), normals(count);
            mesh->Get_Deformed_Vertices(posed.data(),normals.data());
            for (int i=0;i<count;++i) {
                if (links[i] != bone || (!sever && Vector3::Dot_Product(normals[i],direction) > -.10f)) continue;
                const float distance = (posed[i]-point).Length2();
                if (distance >= closest) continue;
                closest = distance;
                best = mesh;
                hitBone = bone;
                if (!sever) contact = posed[i]+direction*.016f;
            }
        }
        if (!best) return false;
        Instance &state = meshInstance(actor,best);
        const int cells = erode(state,hitBone,contact,direction,radius,depth,false,sever);
        if (!cells) return false;
        state.corpse |= corpse;
        state.lastSeen = elapsed;
        if (sever) scheduleSeparation(state,direction);
        log("blast %s %s: %d voxels",best->Get_Name(),boneName,cells);
        return true;
    } catch (const std::exception &e) {
        log("blast skipped: %s",e.what());
        return false;
    }
}

bool Burn(RenderObjClass *actor,const Vector3 &point,const Vector3 &normal,const char *boneName,
          unsigned region,float radius,int stage,bool wholeRegion)
{
    if (!enabled || !actor || !boneName || stage < 1 || stage > 3 || radius <= 0) return false;
    try {
        std::vector<MeshClass*> meshes;
        enumerate(actor,meshes);
        for (auto *mesh : meshes) {
            auto *container = mesh->Get_Container();
            const int bone = container->Get_HTree()->Get_Bone_Index(boneName);
            const int count = mesh->Peek_Model()->Get_Vertex_Count();
            if (bone <= 0 || count <= 0 || count >= 65535) continue;
            const auto *links = mesh->Peek_Model()->Get_Vertex_Bone_Links();
            if (!links || std::find(links,links+count,bone) == links+count) continue;
            // Do not voxelize unheated accessories while looking for the patch.
            container->Get_Bone_Transform(0);
            std::vector<Vector3> posed(count),normals(count);
            mesh->Get_Deformed_Vertices(posed.data(),normals.data());
            bool nearby = false;
            for (int i=0;i<count;++i) if (links[i] == bone && (posed[i]-point).Length2() < radius*radius*4.f) {
                nearby = true;
                break;
            }
            if (!nearby) continue;
            Instance &state = meshInstance(actor,mesh);
            const auto &surface = state.source->soakSurface;
            const int side = surface.legSide[bone];
            const auto anchor = locateSoak(*state.model,surface,posed,normals,point,-normal,&normal,side,
                wholeRegion ? .28f : .15f,bone);
            std::vector<BloodSoak> stains;
            addSoak(stains,*state.model,surface,posed,anchor,side,0.f,elapsed,radius);
            if (stains.empty()) continue;

            const unsigned faces = unsigned(stains.front().faces.size());
            auto previous = std::find_if(state.burns.begin(),state.burns.end(),
                [region](const HeatPatch &patch) { return patch.region == region; });
            if (previous == state.burns.end()) {
                state.burns.push_back({region,stage,std::move(stains.front())});
            } else {
                *previous = {region,stage,std::move(stains.front())};
            }
            for (auto &entry : instances) {
                Instance &other = *entry.second;
                if (&other == &state || other.actor != actor) continue;
                auto stale = std::remove_if(other.burns.begin(),other.burns.end(),
                    [region](const HeatPatch &patch) { return patch.region == region; });
                if (stale != other.burns.end()) {
                    other.burns.erase(stale,other.burns.end());
                    ++other.woundRevision;
                }
            }
            ++state.woundRevision;
            int cells = 0;
            const unsigned part = region/2;
            if (part < state.burnErosionStages.size() && stage >= (wholeRegion ? 3 : 2) &&
                state.burnErosionStages[part] < stage) {
                auto &previousErosion = state.burnErosionStages[part];
                const int eroded = int(std::count_if(state.burnErosionStages.begin(),state.burnErosionStages.end(),
                    [](uint8_t level) { return level != 0; }));
                // Reserve room for severe burns that spread after the victim falls.
                if (previousErosion || eroded < (stage == 3 ? 4 : 2)) {
                    try {
                        const auto &tri = state.model->Get_Polygon_Array()[anchor.triangle];
                        const Vector3 contact = interpolate(posed,tri,anchor.weights);
                        Vector3 outward = interpolate(normals,tri,anchor.weights);
                        if (outward.Length2() < .0001f) outward = normal;
                        outward.Normalize();
                        int corner = 0;
                        for (int k=1;k<3;++k)
                            if (anchor.weights[k] > anchor.weights[corner]) corner = k;
                        const int contactBone = links[tri[corner]];
                        const float width = std::min(radius*(stage == 3 ? .72f : .58f),
                            stage == 3 ? .116f : .066f);
                        const float depth = std::min(width,stage == 3 ? .042f : .026f);
                        cells = erode(state,contactBone,contact-outward*.012f,-outward,width,depth,true);
                        if (cells) previousErosion = uint8_t(stage);
                    } catch (const std::exception &e) {
                        log("thermal erosion skipped: %s",e.what());
                    }
                }
            }
            log("burn %s region %u stage %d: %u skin faces, %d eroded voxels",mesh->Get_Name(),region,stage,
                faces,cells);
            return true;
        }
    } catch (const std::exception &e) {
        log("burn skipped: %s",e.what());
    }
    return false;
}

void Clear_Burn(RenderObjClass *actor)
{
    for (auto &entry : instances) {
        Instance &state = *entry.second;
        if (state.actor != actor || state.burns.empty()) continue;
        state.burns.clear();
        state.frameDraw.reset();
        ++state.woundRevision;
    }
}

bool Hit(RenderObjClass *actor,const Vector3 &point,const Vector3 &direction,float damage,int ammo,bool headHit,
         const Vector3 *contactNormal,const char *legBoneHint,bool corpseHit) {
    if(!enabled||!actor||damage<=0||direction.Length2()<1e-10f)return false;
    PerfScope measure(PerfStage::Hit);
    try {
        std::vector<MeshClass*> meshes;enumerate(actor,meshes);MeshClass *best=NULL;float distance=1e30f;
        // Collision boxes exceed the visible surface. Pick the closest actually
        // posed skin, then let the reference's 8cm bone-ray retry decide a miss.
        int hitBone=-1;bool choseHead=false;
        for(auto *mesh:meshes){int nv=mesh->Peek_Model()->Get_Vertex_Count();if(nv<=0||nv>=65535)continue;mesh->Get_Container()->Get_Bone_Transform(0);std::vector<Vector3> p(nv);mesh->Get_Deformed_Vertices(p.data());bool isHead=strstr(mesh->Get_Name(),".HEAD")!=NULL;const HTreeClass *tree=mesh->Get_Container()->Get_HTree();if(tree && strstr(tree->Get_Name(),"_HEAD"))isHead=true;
            if(headHit && choseHead && !isHead)continue;
            const uint16 *links=mesh->Peek_Model()->Get_Vertex_Bone_Links();
            for(int v=0;v<nv;++v){float d=(p[v]-point).Length2();if(d<distance || (headHit && isHead && !choseHead && d<.09f)){distance=d;best=mesh;hitBone=links?links[v]:-1;choseHead=isHead;}}
        }
        need(best!=NULL,"no supported soldier skin");
        if (legBoneHint && !choseHead) {
            const int legBone = best->Get_Container()->Get_HTree()->Get_Bone_Index(legBoneHint);
            if (legBone > 0) hitBone = legBone;
        }
        const bool created = instances.find(best) == instances.end();
        Instance &state = meshInstance(actor,best);
        ensureBody(state);
        auto pose=palette(best->Get_Container());float p[]={point.X,point.Y,point.Z},d[]={direction.X,direction.Y,direction.Z};float rin=std::min(.03f,.012f+damage*.0002f),rout=std::min(.05f,rin*2);BRVG_HitResult result{};
        state.lastSeen=elapsed;
        const float energy = std::min(10.f,.25f+damage*.02f);
        int cells=BRVG_ShotOnBone(state.body,pose.data(),state.source->bones,hitBone,p,d,rin,rout,energy,&result);
        if (!cells && capacityError()) {
            reclaimCapacity();
            cells=BRVG_ShotOnBone(state.body,pose.data(),state.source->bones,hitBone,p,d,rin,rout,energy,&result);
        }
        state.enteredScene |= actor->Peek_Scene() != nullptr;
        if (cells) {
            state.wounded = true;
            state.corpse |= corpseHit;
            ++state.woundRevision;
            log("hit %s ammo %d: %d cells, bone %d, exit %d%s",best->Get_Name(),ammo,cells,result.bone,result.exited,choseHead?" HEAD":"");
            try {
                recordSoaks(state, result, pose, point, direction, contactNormal, rin, rout);
            } catch (const std::exception &e) {
                log("soak contact skipped: %s", e.what());
            }
            scheduleSeparation(state,direction);
        }
        else if(created)log("voxel miss %s: old geometry path suppressed",best->Get_Name());
        return true;
    }catch(const std::exception &e){log("hit fallback: %s",e.what());return false;}
}
bool Render(MeshClass *mesh,RenderInfoClass &info) {
    auto it=instances.find(mesh);if(!enabled||it==instances.end())return false;Instance &state=*it->second;if(!state.wounded && state.burns.empty())return false;
    if(mesh->Peek_Model()!=state.model){instances.erase(it);return false;}
    try {
        state.lastSeen = elapsed;
        auto pose = palette(mesh->Get_Container());
        MeshModelClass *model = mesh->Peek_Model();
        need(!model->Get_Flag(MeshGeometryClass::SORT) && model->Get_Sort_Level() == SORT_LEVEL_NONE,
             "sorted skin fallback");
        const bool alternate = model->Is_Alternate_Material_Description_Enabled();
        const bool samePose = state.frameDraw && state.frameDraw->alternateMaterial == alternate &&
            state.frameDraw->pose == pose && !model->WoundRestVertices && !model->WoundSkinInfluences;
        const bool canReuse = samePose && (state.frameDraw->tick == logicFrame ||
            unchangedUniformPasses(*model, *state.frameDraw));
        const bool pending = !state.frameDraw || state.frameDraw->woundRevision != state.woundRevision;
        const bool holdCorpse = canReuse && state.corpse && elapsed < state.refreshAfter;
        if (!canReuse || (pending && !holdCorpse)) {
            try {
                auto frame = std::make_unique<FrameDraw>();
                frame->tick = logicFrame;
                frame->woundRevision = state.woundRevision;
                frame->alternateMaterial = alternate;
                frame->pose = std::move(pose);
                BRVG_SurfaceView intact{};
                intact.identity = state.source->identity;
                intact.vertex_count = model->Get_Vertex_Count();
                intact.triangle_count = model->Get_Polygon_Count();
                intact.keep_count = -1;
                const BRVG_SurfaceView *surface = &intact;
                if (state.wounded) {
                    PerfScope measure(PerfStage::Snapshot);
                    const float light[] = {255,255,255,0,0,0,0,0,1};
                    frame->snapshot = BRVG_AcquireDraw(state.body, frame->pose.data(), state.source->bones, light, 1);
                    if (!frame->snapshot && capacityError()) {
                        reclaimCapacity();
                        frame->snapshot = BRVG_AcquireDraw(state.body, frame->pose.data(), state.source->bones, light, 1);
                    }
                    need(frame->snapshot != nullptr, BRVG_LastError());
                    const BRVG_DrawView *draw = BRVG_GetDraw(frame->snapshot);
                    need(draw && draw->surface_count == 1, "draw surface mismatch");
                    surface = &draw->surfaces[0];
                }
                PerfScope measure(PerfStage::BuildSkin);
                const int nv = model->Get_Vertex_Count();
                std::vector<Vector3> positions(nv), normals(nv);
                mesh->Get_Deformed_Vertices(positions.data(), normals.data());
                for (int i = 0; i < model->Get_Pass_Count(); ++i)
                    frame->passes.push_back(preparePass(mesh, *surface, positions, normals, i, state.source->identity));
                state.frameDraw = std::move(frame);
                if (pending) state.refreshAfter = elapsed + .16f;
            } catch (const std::exception &e) {
                if (!samePose) throw;
                // A dirty-budget deferral or an oversized update must not
                // switch a visible corpse back to the intact shared model.
                state.refreshAfter = elapsed + .16f;
                if (state.loggedFailure != state.woundRevision && *e.what()) {
                    log("retaining previous wounds on %s: %s", mesh->Get_Name(), e.what());
                    state.loggedFailure = state.woundRevision;
                }
            }
        }
        FrameDraw &frame = *state.frameDraw;
        if (frame.tick != logicFrame) {
            frame.tick = logicFrame;
            if (!frame.soaksSettled) {
                frame.soaksBuilt = false;
                for (auto &batch : frame.soaks) {
                    batch.vertices.clear();
                    batch.indices.clear();
                }
            }
        }
        const auto *draw = frame.snapshot ? BRVG_GetDraw(frame.snapshot) : nullptr;
        auto &passes = frame.passes;
        const bool base=(info.Current_Override_Flags()&RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY)==0;
        const bool showSoaks = base && !passes.empty() &&
            !(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_SHADOW_RENDERING);
        if (showSoaks && !frame.soaksBuilt) {
            frame.soaksBuilt = true;
            try {
                BRVG_SurfaceView intact{};
                const BRVG_SurfaceView &surface = draw ? draw->surfaces[0] : intact;
                buildSoaks(*passes[0], surface, *model, state.source->soakSurface, state.soaks, frame.soaks);

                frame.soaksSettled = std::all_of(state.soaks.begin(), state.soaks.end(),
                    [](const BloodSoak &soak) { return elapsed - soak.started >= SoakSeconds; });
            } catch (const std::exception &e) {
                frame.soaks = {};
                log("soak build skipped: %s", e.what());
            }
        }
        if (showSoaks && !frame.burnsBuilt) {
            try {
                BRVG_SurfaceView intact{};
                const BRVG_SurfaceView &surface = draw ? draw->surfaces[0] : intact;
                for (int i=0;i<3;++i)
                    buildSoaks(*passes[0],surface,*model,state.source->soakSurface,state.soaks,frame.burns[i],&state.burns,i+1);
                if (draw)
                    tintBurnedFlesh(*draw,*BRVG_GetPacket(passes[0]->packet),*model,state.burns,frame.burnFleshColors);
                frame.burnsBuilt = true;
            } catch (const std::exception &e) {
                frame.burns = {};
                frame.burnsBuilt = true;
                log("burn draw build skipped: %s",e.what());
            }
        }
        PerfScope submit(PerfStage::Submit);
        SavedState saved;DX8Wrapper::Set_World_Identity();if(mesh->Get_Lighting_Environment())DX8Wrapper::Set_Light_Environment(mesh->Get_Lighting_Environment());
        if(base)for(auto &pass:passes)drawPacket(*pass);
        const BRVG_PacketView *geometry=passes.empty()?NULL:BRVG_GetPacket(passes[0]->packet);
        if(geometry)for(int i=0;i<info.Additional_Pass_Count();++i){MaterialPassClass *pass=info.Peek_Additional_Pass(i);pass->Install_Materials();ShaderClass shader=pass->Peek_Shader();shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);DX8Wrapper::Set_Shader(shader);uploadAndDraw(geometry->vertices,geometry->vertex_count,geometry->indices,geometry->index_count);}
        if (draw) {
            BRVG_DrawView shaded = *draw;
            if (showSoaks && !frame.burnFleshColors.empty()) shaded.flesh_rgba = frame.burnFleshColors.data();
            flesh(shaded,info,base);
        }
        if (showSoaks) {
            try {
                drawSoaks(frame.soaks);
                for (int i=0;i<3;++i) drawSoaks(frame.burns[i],i+1);
            } catch (const std::exception &e) {
                log("soak draw skipped: %s", e.what());
            }
        }
        if(state.firstDraw){log("DX8 private draw %s: %u flesh verts, %u flesh indices, %d material passes, %u soaks",mesh->Get_Name(),draw?draw->flesh_vertex_count:0,draw?draw->flesh_index_count:0,model->Get_Pass_Count(),unsigned(state.soaks.size()));state.firstDraw=false;}return true;
    }catch(const std::exception &e){if(!state.reportedFallback){log("draw fallback %s: %s",mesh->Get_Name(),e.what());state.reportedFallback=true;}return false;}
}
}
