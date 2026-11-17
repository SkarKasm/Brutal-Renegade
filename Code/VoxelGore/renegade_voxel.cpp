// GPL-3.0-or-later. Reference algorithms are compiled once, privately here.
// Engine-facing code uses only renegade_voxel.h; no STL/CRT ownership crosses it.
#include "renegade_voxel.h"
#include <array>
#include <cmath>
#include <string>
#include <limits>
#include <stdexcept>
#include <cstring>
#include <set>
#include "reference/cg_gore.cpp"

static_assert(sizeof(int)==sizeof(int32_t),"reference indices require 32-bit int");
#if UINTPTR_MAX == 0xffffffffu
static_assert(sizeof(BRVG_SurfaceDesc)==32 && sizeof(BRVG_ModelDesc)==40,"x86 model ABI layout");
static_assert(sizeof(BRVG_SurfaceView)==32 && sizeof(BRVG_DrawView)==40,"x86 draw ABI layout");
#endif

struct BRVG_Snapshot {
    unsigned references=1;
    size_t bytes=0;
    BRVG_DrawView view{};
    std::vector<BRVG_SurfaceView> surfaces;
    std::vector<std::vector<int32_t>> keep;
    std::vector<std::vector<float>> sub;
    std::vector<float> xyz,normal;
    std::vector<uint8_t> rgba;
    std::vector<int32_t> indices;
};
namespace {
constexpr uint64_t MemoryLimit=128ull*1024*1024;
constexpr size_t MaxSubs=20000,MaxSnapshots=192;
static_assert(MAX_MODELS < 256 && MAX_BODIES < 256, "slot must fit in a handle byte");
struct ModelInfo {
    uint32_t generation=0;
    std::vector<uint64_t> identities;
    std::array<int8_t, 256> legSide{};
};
struct BodyInfo {uint32_t generation=0;BRVG_Snapshot* cached=nullptr;bool wounded=false;};
std::array<ModelInfo,MAX_MODELS+1> modelInfo;
std::array<BodyInfo,MAX_BODIES+1> bodyInfo;
std::set<const BRVG_Snapshot*> snapshots;
std::string error;
uint32_t generation=0;uint64_t currentFrame=0,totalRebuilds=0,cacheHits=0;bool frameValid=false,rebuildUsed=false;
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
bool finite(float f){return std::isfinite(f);}
void checkFloats(const float* p,size_t count){require(p!=nullptr,"missing float array");for(size_t i=0;i<count;++i)require(finite(p[i]),"non-finite input");}
void checkPose(const float* p,uint32_t bones) {
    checkFloats(p,size_t(bones)*12);
    for(uint32_t i=0;i<bones;++i){const float* m=p+i*12;float d=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);require(std::abs(d)>1e-7f,"singular bone transform");}
}
uint32_t token(int slot,uint32_t gen){return (gen<<8)|uint32_t(slot);}
uint32_t newGeneration(){require(generation<0xffffffu,"handle generation exhausted");return ++generation;}
int modelSlot(BRVG_Model handle){int slot=handle&255;require(slot>0 && slot<=MAX_MODELS && models[slot] && modelInfo[slot].generation==(handle>>8),"invalid/stale model handle");return slot;}
int bodySlot(BRVG_Body handle){int slot=handle&255;require(slot>0 && slot<=MAX_BODIES && bodies[slot] && bodyInfo[slot].generation==(handle>>8),"invalid/stale body handle");return slot;}
size_t gridBytes(const gore::VoxelBody& vb){return vb.state.capacity()+vb.depth.capacity()+vb.boneOf.capacity()+vb.labels().capacity()*sizeof(int);}
size_t modelBytes(const Model& m){size_t n=gridBytes(m.vb)+m.island.capacity();for(auto& s:m.surfs)n+=s.xyz.capacity()*sizeof(V3)+s.nrm.capacity()*sizeof(V3)+s.tri.capacity()*sizeof(int);return n;}
size_t bodyBytes(const Body& b){size_t n=gridBytes(b.vb)+b.compactMask.capacity()+b.drawPose.capacity()*4;for(auto& k:b.keep)n+=k.capacity()*4;for(auto& k:b.sub)n+=k.capacity()*4;n+=b.flesh.pos.capacity()*sizeof(V3)+b.flesh.nrm.capacity()*sizeof(V3)+b.flesh.idx.capacity()*4+b.flesh.bone.capacity()+b.flesh.depthCm.capacity()+b.flesh.wound.capacity();return n+(b.fxyz.capacity()+b.fnrm.capacity())*4+b.frgba.capacity()+b.fidx.capacity()*4;}
uint64_t owned(){uint64_t n=carvedSum.capacity()*sizeof(int);for(auto& m:models)if(m)n+=modelBytes(*m);for(auto& b:bodies)if(b)n+=bodyBytes(*b);for(auto* p:snapshots)n+=p->bytes;return n;}
void release(BRVG_Snapshot* p){if(p && --p->references==0){snapshots.erase(p);delete p;}}
void clearCache(int slot){release(bodyInfo[slot].cached);bodyInfo[slot].cached=nullptr;}

void restoreGrid(Body &body, const Model &model)
{
    if (body.compactMask.empty()) return;
    const size_t count = model.vb.state.size();
    require(owned() + count < MemoryLimit, "body restore memory budget");
    body.vb.copyStateFrom(model.vb);
    for (size_t i = 0; i < body.compactMask.size(); ++i) {
        const uint8_t bits = body.compactMask[i];
        if (!bits) continue;
        for (unsigned k = 0; k < 8 && i * 8 + k < count; ++k)
            if (bits & (1u << k)) body.vb.state[i * 8 + k] = gore::CARVED;
    }
    std::vector<uint8_t>().swap(body.compactMask);
}

void assignLowerPelvisCells(Model &model, const ModelInfo &info, const char *names,
                            int leftThigh, int rightThigh)
{
    int leftKnee = -1, rightKnee = -1, pelvis = -1;
    for (int i = 0; i < model.numBones; ++i) {
        if (std::strncmp(names + i * 32, "C L CALF", 32) == 0) leftKnee = i;
        if (std::strncmp(names + i * 32, "C R CALF", 32) == 0) rightKnee = i;
        if (std::strncmp(names + i * 32, "C PELVIS", 32) == 0) pelvis = i;
    }
    if (leftThigh < 0 || rightThigh < 0 || leftKnee < 0 || rightKnee < 0) return;

    const V3 left = ToV3(model.rest[leftThigh].t), right = ToV3(model.rest[rightThigh].t);
    const V3 hip = (left + right) * .5f;
    const V3 knee = (ToV3(model.rest[leftKnee].t) + ToV3(model.rest[rightKnee].t)) * .5f;
    const V3 down = gore::norm(knee - hip);
    const V3 across = gore::norm(left - right);
    const float hipDepth = .14f * gore::len(knee - hip);
    if (hipDepth < .02f || gore::len(across) < .5f) return;

    auto &grid = model.vb;
    for (int z = 0; z < grid.nz; ++z) for (int y = 0; y < grid.ny; ++y) for (int x = 0; x < grid.nx; ++x) {
        const int i = grid.id(x, y, z);
        const int owner = grid.boneOf[i];
        if (!grid.filled(i) || info.legSide[owner]) continue;
        if (owner != 255 && owner != 0 && owner != pelvis &&
            owner != model.parents[leftThigh] && owner != model.parents[rightThigh]) continue;
        const V3 offset = grid.centre(x, y, z) - hip;
        if (gore::dot(offset, down) <= hipDepth) continue;
        // Some trouser meshes assign much of the upper legs to the pelvis.
        // Keep the actual hip connection, but assign lower cells to their
        // anatomical side so pelvis weights cannot tie the thighs together.
        const bool onLeft = gore::dot(offset, across) >= 0;
        const bool belowKnee = gore::dot(grid.centre(x, y, z) - knee, down) > 0;
        grid.boneOf[i] = uint8_t(onLeft ? (belowKnee ? leftKnee : leftThigh)
                                      : (belowKnee ? rightKnee : rightThigh));
    }
}

void appendWoundFaces(BRVG_Snapshot& snapshot, const Body& body,
                      const Model& model, const ModelInfo& info, const vrGoreDraw_t& draw) {
    const gore::Mesh& rest = body.flesh;
    snapshot.indices.reserve(draw.numFleshIndexes);
    for (int i = 0; i < draw.numFleshIndexes; i += 3) {
        const int* face = draw.fleshIndexes + i;
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
            const int a = face[k], b = face[(k + 1) % 3];
            const unsigned boneA = rest.bone[a], boneB = rest.bone[b];
            if (boneA == boneB) continue;

            const int sideA = boneA < unsigned(model.numBones) ? info.legSide[boneA] : 0;
            const int sideB = boneB < unsigned(model.numBones) ? info.legSide[boneB] : 0;
            const V3 posedEdge = ToV3(draw.fleshXyz + 3 * a) - ToV3(draw.fleshXyz + 3 * b);
            const float maxLength = 2.0f * gore::len(rest.pos[a] - rest.pos[b]) + body.vb.cell;
            // Nearby voxels can have unrelated owners in the bind pose.
            // Opposite legs and torn-open joints must not share a wound face.
            if (sideA * sideB < 0 || gore::dot(posedEdge, posedEdge) > maxLength * maxLength) {
                valid = false;
                break;
            }
        }
        if (valid) snapshot.indices.insert(snapshot.indices.end(), face, face + 3);
    }
}

void boneRods(Model& m,const char* names) {
    for(int child=1;child<m.numBones;++child){int parent=m.parents[child];if(parent<0)continue;std::string name(names+size_t(parent)*32,32);float r=0;
        if(name.find("SPINE")!=std::string::npos)r=.017f;else if(name.find("PELVIS")!=std::string::npos)r=.025f;
        else if(name.find("UPPERARM")!=std::string::npos || name.find("FOREARM")!=std::string::npos)r=.011f;
        else if(name.find("THIGH")!=std::string::npos)r=.016f;else if(name.find("CALF")!=std::string::npos)r=.013f;
        if(r)m.vb.addBoneRod(ToV3(m.rest[parent].t),ToV3(m.rest[child].t),r,uint8_t(parent));}
}
}
extern "C" {
const char* BRVG_LastError(){return error.c_str();}
BRVG_Model BRVG_CreateModel(const BRVG_ModelDesc* d) {
 try {error.clear();require(d && d->size==sizeof(*d) && d->version==BRVG_ABI_VERSION,"model ABI mismatch");require(d->bone_count>0 && d->bone_count<=MAX_BONES && d->surface_count>0 && d->surface_count<=VRG_MAX_SURFS,"model limits");require(d->parents && d->surfaces && d->bone_names,"missing model arrays");checkPose(d->rest_palette,d->bone_count);
    int slot=1;while(slot<=MAX_MODELS && models[slot])++slot;require(slot<=MAX_MODELS,"model cache full");
    auto m=std::make_unique<Model>();m->numBones=int(d->bone_count);m->head=d->rigid_head!=0;m->flags.resize(d->bone_count,0);m->parents.assign(d->parents,d->parents+d->bone_count);
    for(uint32_t i=0;i<d->bone_count;++i){require(i==0?d->parents[i]==-1:d->parents[i]>=0 && uint32_t(d->parents[i])<i,"invalid parent order");m->rest.push_back(PoseBone(d->rest_palette,i));m->restInv.push_back(Inverse(m->rest.back()));}
    gore::Body skin;std::vector<uint64_t> identities;size_t sourceBytes=0;
    for(uint32_t si=0;si<d->surface_count;++si){auto& in=d->surfaces[si];require(in.identity && in.vertex_count>0 && in.vertex_count<65535 && in.triangle_count>0 && in.triangle_count<1000000,"surface limits/identity");require(in.bones && in.indices,"missing surface arrays");require(std::find(identities.begin(),identities.end(),in.identity)==identities.end(),"duplicate surface identity");identities.push_back(in.identity);checkFloats(in.rest_xyz,size_t(in.vertex_count)*3);checkFloats(in.rest_normals,size_t(in.vertex_count)*3);
        sourceBytes+=size_t(in.vertex_count)*32+size_t(in.triangle_count)*12;require(sourceBytes<16*1024*1024,"source geometry budget");Surf surf;surf.numVerts=int(in.vertex_count);surf.numTris=int(in.triangle_count);uint32_t base=uint32_t(skin.pos.size());
        for(uint32_t i=0;i<in.vertex_count;++i){require(in.bones[i]<d->bone_count,"vertex bone out of range");V3 p=ToV3(in.rest_xyz+i*3),n=ToV3(in.rest_normals+i*3);surf.xyz.push_back(p);surf.nrm.push_back(n);skin.pos.push_back(p);skin.bone.push_back(uint8_t(in.bones[i]));}
        for(size_t i=0;i<size_t(in.triangle_count)*3;++i){require(in.indices[i]<in.vertex_count,"triangle index out of range");surf.tri.push_back(int(in.indices[i]));skin.idx.push_back(base+in.indices[i]);}m->surfs.push_back(std::move(surf));}
    float cell=d->cell_m>0?d->cell_m:(d->rigid_head?BRVG_DEFAULT_HEAD_CELL_M:BRVG_DEFAULT_BODY_CELL_M);require(finite(cell) && cell>=.005f && cell<=.03f,"cell size outside supported range");
    V3 lo=skin.pos[0],hi=lo;for(auto p:skin.pos){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    double cells=(ceil((hi.x-lo.x)/cell)+8)*(ceil((hi.y-lo.y)/cell)+8)*(ceil((hi.z-lo.z)/cell)+8);require(cells<=4000000 && owned()+uint64_t(cells)*16<MemoryLimit,"voxel template budget");
    require(m->vb.build(skin,cell),"voxel build failed");boneRods(*m,d->bone_names);
    if(m->head){for(size_t i=0;i<m->vb.state.size();++i){float depth=m->vb.depth[i]*cell;if(m->vb.filled(int(i)) && depth>=.006f && depth<=.025f)m->vb.state[i]=gore::BONE;}}
    auto& info = modelInfo[slot];
    int leftThigh = -1, rightThigh = -1;
    for (uint32_t i = 0; i < d->bone_count; ++i) {
        const char* name = d->bone_names + i * 32;
        const int parent = d->parents[i];
        info.legSide[i] = parent >= 0 ? info.legSide[parent] : 0;
        if (std::strncmp(name, "C L THIGH", 32) == 0) { info.legSide[i] = 1; leftThigh = int(i); }
        if (std::strncmp(name, "C R THIGH", 32) == 0) { info.legSide[i] = -1; rightThigh = int(i); }
    }
    assignLowerPelvisCells(*m, info, d->bone_names, leftThigh, rightThigh);
    MarkIslands(*m, info.legSide.data());
    require(owned()+modelBytes(*m)<MemoryLimit,"template memory budget");
    uint32_t gen=newGeneration();models[slot]=std::move(m);info.generation=gen;info.identities=std::move(identities);return token(slot,gen);
 }catch(const std::exception& e){error=e.what();return 0;}
}
int BRVG_FreeModel(BRVG_Model model) {
    try {
        error.clear();
        const int slot = modelSlot(model);
        for (const auto &body : bodies)
            require(!body || body->model != slot, "model still has wounded bodies");
        models[slot].reset();
        modelInfo[slot] = {};
        return 1;
    } catch (const std::exception &e) {
        error = e.what();
        return 0;
    }
}
BRVG_Body BRVG_CreateBody(BRVG_Model model) {
 try {error.clear();int slot=modelSlot(model);const auto cells=models[slot]->vb.state.size();require(owned()+uint64_t(cells)*9<MemoryLimit,"body memory budget");const auto gen=newGeneration();int b=Gore_NewBody(slot);require(b>0,"body pool full");bodyInfo[b].generation=gen;bodyInfo[b].wounded=false;return token(b,gen);}catch(const std::exception& e){error=e.what();return 0;}
}
int BRVG_CompactBody(BRVG_Body handle) {
    try {
        error.clear();
        const int slot = bodySlot(handle);
        Body &body = *bodies[slot];
        if (!body.compactMask.empty() || body.piece || !bodyInfo[slot].wounded) return 0;
        const auto &state = body.vb.state;
        std::vector<uint8_t> bits((state.size() + 7) / 8);
        for (size_t i = 0; i < state.size(); ++i)
            if (state[i] == gore::CARVED) bits[i / 8] |= uint8_t(1u << (i % 8));
        body.compactMask = std::move(bits);
        std::vector<uint8_t>().swap(body.vb.state);
        clearCache(slot);
        body.keep.clear();
        body.sub.clear();
        body.flesh = {};
        std::vector<float>().swap(body.fxyz);
        std::vector<float>().swap(body.fnrm);
        std::vector<uint8_t>().swap(body.frgba);
        std::vector<int>().swap(body.fidx);
        body.draw = {};
        body.drawValid = false;
        body.dirty = body.fullSkinDirty = true;
        return 1;
    } catch (const std::exception &e) {
        error = e.what();
        return 0;
    }
}
void BRVG_FreeBody(BRVG_Body body){try{int b=bodySlot(body);clearCache(b);Gore_FreeBody(b);}catch(const std::exception& e){error=e.what();}}
void BRVG_Shutdown(){for(int b=1;b<=MAX_BODIES;++b)clearCache(b);Gore_Shutdown();for(auto& m:modelInfo)m.identities.clear();frameValid=false;}
void BRVG_BeginFrame(uint64_t frame){if(!frameValid || frame!=currentFrame){currentFrame=frame;frameValid=true;rebuildUsed=false;Gore_NewFrame();}}
static int shotWithBoneMask(BRVG_Body body,const float* pose,uint32_t bones,const float point[3],const float direction[3],float rin,float rout,float energy,BRVG_HitResult* result,const uint8_t* boneMask) {
 if(result)std::memset(result,0,sizeof(*result));
 try {error.clear();int b=bodySlot(body);auto &state=*bodies[b];auto &model=*models[state.model];require(bones==uint32_t(model.numBones),"hit palette mismatch");checkPose(pose,bones);checkFloats(point,3);checkFloats(direction,3);require(gore::len(ToV3(direction))>.001f && finite(rin)&&finite(rout)&&finite(energy)&&rin>0&&rout>=rin&&rout<=.12f&&energy>0&&energy<=10,"invalid round");restoreGrid(state,model);goreShot_t out{};int cells=Gore_Shot(b,pose,point,direction,rin,rout,energy,&out,boneMask);
    if(cells){clearCache(b);bodyInfo[b].wounded=true;if(result){result->cells=out.cells;result->bone_cells=out.boneCells;result->exited=out.exited;result->bone=out.bone;std::memcpy(result->rest_entry,out.restEntry,12);std::memcpy(result->rest_exit,out.restExit,12);std::memcpy(result->rest_direction,out.restDir,12);}}return cells;
 }catch(const std::exception& e){error=e.what();return 0;}
}
int BRVG_Shot(BRVG_Body body,const float* pose,uint32_t bones,const float point[3],const float direction[3],float rin,float rout,float energy,BRVG_HitResult* result) {
    return shotWithBoneMask(body,pose,bones,point,direction,rin,rout,energy,result,nullptr);
}
uint32_t BRVG_Separate(BRVG_Body body,BRVG_Piece* out,uint32_t cap) {
 try {error.clear();int b=bodySlot(body);require(out && cap>0 && cap<=MAX_BODIES,"invalid piece output");auto &state=*bodies[b];auto &model=*models[state.model];const uint64_t used=owned(),cells=model.vb.state.size(),temporary=cells*12,pieceReserve=cells;require(used+temporary+pieceReserve+(state.compactMask.empty()?0:cells)<MemoryLimit,"piece memory budget");restoreGrid(state,model);cap=(std::min)(cap,uint32_t((MemoryLimit-owned()-temporary)/pieceReserve));int pieces[MAX_BODIES],bones[MAX_BODIES];float centers[MAX_BODIES][3];int n=Gore_Sever(b,pieces,centers,bones,int(cap),modelInfo[state.model].legSide.data());if(n)clearCache(b);for(int i=0;i<n;++i){int p=pieces[i];bodyInfo[p].generation=newGeneration();out[i].body=token(p,bodyInfo[p].generation);out[i].bone=bones[i];std::memcpy(out[i].rest_center,centers[i],12);}return uint32_t(n);
 }catch(const std::exception& e){error=e.what();return 0;}
}
int BRVG_ShotOnBone(BRVG_Body handle,const float *pose,uint32_t bones,int32_t bone,const float point[3],const float direction[3],float rin,float rout,float energy,BRVG_HitResult *result){
    if(bone<0)return BRVG_Shot(handle,pose,bones,point,direction,rin,rout,energy,result);
    try{int b=bodySlot(handle);Model &m=*models[bodies[b]->model];require(bones==uint32_t(m.numBones)&&uint32_t(bone)<bones,"precise hit bone");checkPose(pose,bones);checkFloats(point,3);checkFloats(direction,3);
        Xf toRest=Mul(m.rest[bone],Inverse(PoseBone(pose,bone)));V3 p=Point(toRest,ToV3(point)),d=gore::norm(Dir(toRest,ToV3(direction)));float pr[]={p.x,p.y,p.z},dr[]={d.x,d.y,d.z};std::vector<float> base;base.reserve(bones*12);for(auto &tm:m.rest){base.insert(base.end(),tm.m,tm.m+9);base.insert(base.end(),tm.t,tm.t+3);}
        const auto& sides = modelInfo[bodies[b]->model].legSide;
        const int side = sides[bone];
        std::array<uint8_t, 256> limb{};
        if (side) {
            // This impact was raycast against one posed limb. The bind-pose ray
            // cannot establish another impact on the rest of the body.
            for (uint32_t i = 0; i < bones; ++i) limb[i] = sides[i] == side;
        }
        int cells=shotWithBoneMask(handle,base.data(),bones,pr,dr,rin,rout,energy,result,side ? limb.data() : nullptr);
        if(cells && result)result->bone=bone;return cells;
    }catch(const std::exception &e){error=e.what();if(result)std::memset(result,0,sizeof(*result));return 0;}
}
int BRVG_Erode(BRVG_Body handle,const float *pose,uint32_t bones,int32_t bone,
              const float center[3],const float direction[3],float radius,float depth,int skinOnly,int limbCut)
{
    try {
        error.clear();
        const int slot = bodySlot(handle);
        Body &body = *bodies[slot];
        const Model &model = *models[body.model];
        require(bones == uint32_t(model.numBones) && bone >= 0 && uint32_t(bone) < bones, "erosion bone");
        checkPose(pose,bones);
        checkFloats(center,3);
        checkFloats(direction,3);
        require(radius > 0 && radius <= .22f && depth > 0 && depth <= radius &&
                gore::len(ToV3(direction)) > .001f, "erosion dimensions");
        const Xf rest = Mul(model.rest[bone],Inverse(PoseBone(pose,bone)));
        const V3 origin = Point(rest,ToV3(center));
        const V3 axis = gore::norm(Dir(rest,ToV3(direction)));
        const auto &sides = modelInfo[body.model].legSide;
        const int side = sides[bone];
        std::array<bool,256> limb{};
        if (limbCut && !side) {
            // An arm cut must never take a bite out of the nearby torso.
            for (int i=bone;i<model.numBones;++i)
                limb[i] = i == bone || (model.parents[i] >= 0 && limb[model.parents[i]]);
        }
        restoreGrid(body,model);
        auto &grid = body.vb;
        const int dims[] = {grid.nx,grid.ny,grid.nz};
        const float offsets[] = {origin.x-grid.origin.x, origin.y-grid.origin.y, origin.z-grid.origin.z};
        int lo[3],hi[3],dirty[] = {grid.nx,grid.ny,grid.nz,0,0,0};
        for (int i=0;i<3;++i) {
            lo[i] = int(std::clamp((offsets[i]-radius)/grid.cell,0.f,float(dims[i])));
            hi[i] = int(std::clamp((offsets[i]+radius)/grid.cell+1.f,0.f,float(dims[i])));
        }
        int cells = 0;
        for (int z=lo[2];z<hi[2];++z) for (int y=lo[1];y<hi[1];++y) for (int x=lo[0];x<hi[0];++x) {
            const int i = grid.id(x,y,z);
            if (!grid.filled(i)) continue;
            const auto owner = grid.boneAt(i);
            if (sides[owner] != side || (limbCut && !side && !limb[owner])) continue;
            if (skinOnly && (grid.state[i] == gore::BONE || grid.depthAt(i)*grid.cell > .04f)) continue;
            const V3 delta = grid.centre(x,y,z) - origin;
            const float axial = gore::dot(delta,axis);
            const V3 radial = delta - axis*axial;
            const float radialFraction = gore::dot(radial,radial)/(radius*radius);
            const float roughEdge = skinOnly
                ? 1.f + .16f*std::sin(delta.x*108.f+delta.z*67.f)*std::cos(delta.y*85.f-delta.z*111.f)
                : 1.f;
            if (limbCut ? (std::abs(axial)>depth || radialFraction>1.f)
                        : (axial*axial/(depth*depth)+radialFraction>roughEdge)) continue;
            grid.state[i] = gore::CARVED;
            ++cells;
            const int xyz[] = {x,y,z};
            for (int k=0;k<3;++k) {
                dirty[k] = std::min(dirty[k],std::max(0,xyz[k]-2));
                dirty[k+3] = std::max(dirty[k+3],std::min(dims[k],xyz[k]+3));
            }
        }
        if (cells) {
            body.dirty = body.mergedChanges = true;
            MergeBounds(body.pendingBounds,dirty);
            MergeBounds(body.wallBounds,dirty);
            clearCache(slot);
            bodyInfo[slot].wounded = true;
        }
        return cells;
    } catch (const std::exception &e) {
        error = e.what();
        return 0;
    }
}

const BRVG_Snapshot* BRVG_AcquireDraw(BRVG_Body body,const float* pose,uint32_t bones,const float light[9],int visible) {
 try {error.clear();if(!visible)return nullptr;require(frameValid,"BeginFrame required");int b=bodySlot(body);Body& state=*bodies[b];Model& m=*models[state.model];require(bones==uint32_t(m.numBones),"draw palette mismatch");checkPose(pose,bones);checkFloats(light,9);
    if(state.dirty && rebuildUsed)return nullptr; // includes first wounds: no budget bypass
    bool dirty=state.dirty;bool same=state.drawValid && state.drawPose.size()==size_t(bones)*12 && !memcmp(state.drawPose.data(),pose,size_t(bones)*48) && !memcmp(state.drawLight,light,36);
    if(same && !dirty && bodyInfo[b].cached){++cacheHits;++bodyInfo[b].cached->references;return bodyInfo[b].cached;}
    require(snapshots.size()<MaxSnapshots,"snapshot pool full");
    if (dirty) require(owned()+uint64_t(m.vb.state.size())*(state.compactMask.empty()?8:9)<MemoryLimit,"rebuild memory reserve");
    restoreGrid(state,m);
    const vrGoreDraw_t* d=Gore_Draw(b,pose,light,light+3,light+6,modelInfo[state.model].legSide.data());if(dirty){rebuildUsed=true;++totalRebuilds;}require(d!=nullptr,"draw deferred");size_t subs=0;for(int i=0;i<d->numSurfaces;++i){subs+=d->surf[i].numSub;require(uint64_t(d->surf[i].numVerts)+uint64_t(d->surf[i].numSub)*3<65535,"skin 16-bit buffer budget; draw intact instead");}require(subs<=MaxSubs && d->numFleshVerts<65535 && d->numFleshIndexes<500000,"draw geometry budget; draw intact instead");
    auto p=std::make_unique<BRVG_Snapshot>();p->surfaces.resize(d->numSurfaces);p->keep.resize(d->numSurfaces);p->sub.resize(d->numSurfaces);
    for(int i=0;i<d->numSurfaces;++i){auto& a=d->surf[i];auto& v=p->surfaces[i];v.identity=modelInfo[state.model].identities[i];v.vertex_count=a.numVerts;v.triangle_count=a.numTriangles;v.keep_count=a.numKeep;v.sub_count=a.numSub;if(a.numKeep>0)p->keep[i].assign(a.keep,a.keep+a.numKeep);if(a.numSub>0)p->sub[i].assign(a.sub,a.sub+size_t(a.numSub)*10);v.keep=p->keep[i].data();v.sub=p->sub[i].data();p->bytes+=p->keep[i].capacity()*4+p->sub[i].capacity()*4;}
    if(d->numFleshVerts){p->xyz.assign(d->fleshXyz,d->fleshXyz+size_t(d->numFleshVerts)*3);p->normal.assign(d->fleshNormal,d->fleshNormal+size_t(d->numFleshVerts)*3);p->rgba.assign(d->fleshRGBA,d->fleshRGBA+size_t(d->numFleshVerts)*4);}
    if (d->numFleshIndexes) appendWoundFaces(*p, state, m, modelInfo[state.model], *d);
    p->view.version=BRVG_ABI_VERSION;p->view.surface_count=d->numSurfaces;p->view.two_sided=bodyInfo[b].wounded||state.piece;p->view.surfaces=p->surfaces.data();p->view.flesh_vertex_count=d->numFleshVerts;p->view.flesh_index_count=uint32_t(p->indices.size());p->view.flesh_xyz=p->xyz.data();p->view.flesh_normals=p->normal.data();p->view.flesh_rgba=p->rgba.data();p->view.flesh_indices=p->indices.data();p->bytes+=(p->xyz.capacity()+p->normal.capacity())*4+p->rgba.capacity()+p->indices.capacity()*4+sizeof(*p)+p->surfaces.capacity()*sizeof(BRVG_SurfaceView);require(owned()+p->bytes<MemoryLimit,"snapshot memory budget");clearCache(b);auto* raw=p.release();snapshots.insert(raw);bodyInfo[b].cached=raw;++raw->references;return raw;
 }catch(const std::exception& e){error=e.what();return nullptr;}
}
const BRVG_DrawView* BRVG_GetDraw(const BRVG_Snapshot* snapshot){return snapshots.count(snapshot)?&snapshot->view:nullptr;}
int BRVG_PosePoint(BRVG_Body body,const float *pose,uint32_t bones,int32_t bone,const float rest[3],float posed[3]){try{int b=bodySlot(body);require(bones==uint32_t(models[bodies[b]->model]->numBones)&&bone>=0&&uint32_t(bone)<bones&&posed,"point palette/bone");checkPose(pose,bones);checkFloats(rest,3);Gore_RestToPose(b,pose,bone,rest,posed);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
void BRVG_ReleaseDraw(const BRVG_Snapshot* snapshot){if(snapshots.count(snapshot))release(const_cast<BRVG_Snapshot*>(snapshot));}
BRVG_Stats BRVG_GetStats(){BRVG_Stats s{};for(auto& m:models)s.model_count+=m!=nullptr;for(auto& b:bodies)s.body_count+=b!=nullptr;s.snapshot_count=uint32_t(snapshots.size());s.owned_bytes=owned();s.rebuild_count=totalRebuilds;s.pose_cache_hits=cacheHits;return s;}
}
