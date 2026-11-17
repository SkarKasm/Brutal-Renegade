// Headless engine integration: the real MeshClass Render hook feeds a checked
// draw sink. No D3D device, window, scene, input, audio or game launch.
#include "w3d_reader.h"
#include "always.h"
#include "ramfile.h"
#include "chunkio.h"
#include "htree.h"
#include "hlod.h"
#include "mesh.h"
#include "meshmdl.h"
#include "vertmaterial.h"
#include "camera.h"
#include "dx8wrapper.h"
#include "rinfo.h"
#include "scene.h"
#include "meshmatdesc.h"
#include "voxel_skin.h"
#include "../../VoxelGore/voxel_draw.h"
#include <array>
#include <iostream>
#include <map>
#include <set>

class FixtureModel : public MeshModelClass {
public:
 FixtureModel(const w3d::Surface& s){Reset(int(s.idx.size()/3),int(s.local.size()),1);Set_Name(s.name.c_str());Set_Flag(SKIN,true);uint16 *bones=get_bone_links(true);
  for(size_t i=0;i<s.local.size();++i){auto p=s.local[i],n=s.normal[i];Vertex->Get_Array()[i]=Vector3(p.x,p.y,p.z);VertexNorm->Get_Array()[i]=Vector3(n.x,n.y,n.z);bones[i]=s.bone[i];}
  for(size_t i=0;i<s.idx.size()/3;++i)for(int k=0;k<3;++k)get_polys()[i][k]=s.idx[i*3+k];Set_Flag(DIRTY_VNORMALS,false);
  VertexMaterialClass *material=new VertexMaterialClass;Set_Single_Material(material,0);material->Release_Ref();ShaderClass shader;shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);shader.Set_Primary_Gradient(ShaderClass::GRADIENT_ADD);Set_Single_Shader(shader,0);
 }
 void Install_Seamed_UVs() {
  std::vector<Vector2> uv(Get_Vertex_Count());
  for (size_t i = 0; i < uv.size(); ++i) {
   const float u = (i / 3) % 2 ? 1.75f : -1.25f;
   uv[i].Set(u + (i % 3 == 1 ? .5f : 0), i % 3 == 2 ? .8f : .1f);
  }
  CurMatDesc->Install_UV_Array(0, 0, uv.data(), int(uv.size()));
 }
};
class FixtureMesh : public MeshClass {
public:
 FixtureMesh(const w3d::Surface& s, bool seams=false){
  if(Model)Model->Release_Ref();
  auto *model = new FixtureModel(s);
  if (seams) model->Install_Seamed_UVs();
  Model=model;
 }
};
class FixtureActor : public RenderObjClass {
 HTreeClass tree;MeshClass *skin;
public:
 FixtureActor(const HTreeClass& t,MeshClass *s):tree(t),skin(s){skin->Add_Ref();skin->Set_Container(this);tree.Base_Update(Matrix3D::Identity);}
 ~FixtureActor(){skin->Set_Container(NULL);skin->Release_Ref();}
 RenderObjClass *Clone() const override{return NULL;}
 void Render(RenderInfoClass&) override{}
 const HTreeClass *Get_HTree() const override{return &tree;}
 const Matrix3D &Get_Bone_Transform(int b) override{return tree.Get_Transform(b);}
 void Pose(const Matrix3D &root) { tree.Base_Update(root); }
 void Spread_Legs() {
  for (const char *name : {"C L THIGH", "C R THIGH"}) {
   const int bone = tree.Get_Bone_Index(name);
   w3d::require(bone > 0, "fixture thigh bone");
   Matrix3D shifted = tree.Get_Transform(bone);
   shifted.Set_Translation(shifted.Get_Translation() + Vector3(0, name[2] == 'L' ? .18f : -.18f, 0));
   tree.Capture_Bone(bone);
   tree.Control_Bone_Root_Relative(bone, shifted);
  }
  Pose(Matrix3D::Identity);
 }
 int Get_Num_Sub_Objects() const override{return 1;}
 RenderObjClass *Get_Sub_Object(int i) const override{if(i)return NULL;skin->Add_Ref();return skin;}
};
class FixtureHlod : public HLodClass {public:FixtureHlod(RenderObjClass **lods,const HTreeClass &tree):HLodClass("VOXEL_HEAD_LODS",lods,2){Set_HTree(const_cast<HTreeClass*>(&tree));}};
static unsigned draws=0,skinDraws=0,fleshDraws=0;
static unsigned lastSkinIndexCount=0;
using FaceKey = std::array<float, 9>;
static bool captureSoak = false;
static std::set<FaceKey> solidFaces;
static std::vector<std::array<BRVG_RenderVertex, 3>> stainFaces;

static FaceKey faceKey(const BRVG_RenderVertex (&face)[3]) {
 FaceKey key;
 for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) key[i*3+j] = face[i].xyz[j];
 return key;
}

static bool sink(const BRVG_RenderVertex *vertices,unsigned nv,const unsigned short *indices,unsigned ni){
 if(!nv||nv>=65535||!ni||ni>65532||ni%3)return false;
 for(unsigned i=0;i<nv;++i)for(float v:vertices[i].xyz)if(!std::isfinite(v))return false;
 for(unsigned i=0;i<ni;++i)if(indices[i]>=nv)return false;
 ShaderClass shader;
 DX8Wrapper::Get_Shader(shader);
 const bool soak = shader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_SRC_COLOR;
 if (shader.Get_Primary_Gradient() == ShaderClass::GRADIENT_ADD) lastSkinIndexCount += ni;
 ++draws;
 if (!soak) { if(nv>=4754)++skinDraws;else ++fleshDraws; }
 if (!captureSoak) return true;
 for (unsigned i = 0; i < ni; i += 3) {
  const BRVG_RenderVertex face[] = {vertices[indices[i]], vertices[indices[i+1]], vertices[indices[i+2]]};
  const FaceKey key = faceKey(face);
  if (soak) {
   w3d::require(shader.Get_Depth_Compare() == ShaderClass::PASS_EQUAL &&
                shader.Get_Depth_Mask() == ShaderClass::DEPTH_WRITE_DISABLE &&
                shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ZERO,
                "soak multiplies lit skin at equal depth without writing depth");
   w3d::require(solidFaces.count(key) != 0, "soak uses an exact surviving skin triangle");
   stainFaces.push_back({face[0], face[1], face[2]});
 } else if (shader.Get_Primary_Gradient() == ShaderClass::GRADIENT_ADD) {
   solidFaces.insert(key);
  }
 }
 return true;
}

static void appendSoakBox(w3d::Surface &s, float minY, float maxY) {
 const w3d::V3 corners[] = {
  {.045f,minY,.78f}, {.045f,maxY,.78f}, {.045f,maxY,1.02f}, {.045f,minY,1.02f},
  {-.045f,minY,.78f}, {-.045f,maxY,.78f}, {-.045f,maxY,1.02f}, {-.045f,minY,1.02f}
 };
 const int faces[][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7}};
 const int order[] = {0,1,2,0,2,3};
 for (const auto &face : faces) {
  const auto normal = gore::norm(gore::cross(corners[face[1]]-corners[face[0]], corners[face[2]]-corners[face[0]]));
  for (int i : order) {
   s.idx.push_back(uint32_t(s.local.size()));
   s.local.push_back(corners[face[i]]);
   s.normal.push_back(normal);
   s.bone.push_back(0);
  }
 }
}

static auto captureStains(FixtureMesh *mesh, RenderInfoClass &info) {
 solidFaces.clear();
 stainFaces.clear();
 captureSoak = true;
 w3d::require(VoxelSkin::Render(mesh, info), "private draw during soak capture");
 captureSoak = false;
 return stainFaces;
}

static void checkSoaking(const HTreeClass &tree, RenderInfoClass &info) {
 w3d::Surface fixture;
 fixture.name = "SOAK_SEAM.BODY";
 appendSoakBox(fixture, -.11f, .005f);
 appendSoakBox(fixture, .028f, .14f); // nearby disconnected skin with the same UV pattern
 auto *mesh = new FixtureMesh(fixture, true);
 auto *actor = new FixtureActor(tree, mesh);
 auto *scene = new SimpleSceneClass;
 actor->Notify_Added(scene);
 const Vector3 point(.052f, -.04f, .78f + .24f * (.07f / .115f));
 const Vector3 ray(-1,0,0);
 w3d::require(VoxelSkin::Hit(actor, point, ray, 60, 123), "valid hit on seam fixture");
 VoxelSkin::Tick(0);
 const auto fresh = captureStains(mesh, info);
 w3d::require(!fresh.empty(), "fresh wound submits a stain");
 const auto secondEye = captureStains(mesh, info);
 w3d::require(fresh.size() == secondEye.size() &&
              !std::memcmp(fresh.data(), secondEye.data(), fresh.size() * sizeof(fresh.front())),
              "both eyes use identical stain geometry in the same simulation tick");
 VoxelSkin::Tick(2.5f);
 const auto mature = captureStains(mesh, info);
 w3d::require(mature.size() > fresh.size(), "soak spreads to more surviving triangles over 2.5 seconds");
 bool belowSeam = false, aboveSeam = false;
 bool downhill = false;
 for (const auto &face : mature) {
  for (int k = 0; k < 3; ++k) {
   const auto &a = face[k], &b = face[(k + 1) % 3];
   const float dy = b.xyz[1] - a.xyz[1], dz = b.xyz[2] - a.xyz[2];
   if (a.xyz[0] < .044f || b.xyz[0] < .044f || std::abs(dy) < .002f || std::abs(dz) < .002f) continue;
   const float du = (b.uv0[0] - a.uv0[0]) / dy, dv = (b.uv0[1] - a.uv0[1]) / dz;
   downhill |= std::abs(du) > 25 && dv < -3 && dv > -16;
  }
 }
 w3d::require(downhill, "a narrow run develops along the downhill direction");
 for (const auto &face : mature) for (const auto &vertex : face) {
  const float *p = vertex.xyz;
  w3d::require(p[1] <= .0051f, "nearby skin with reused UVs stays clear");
  if (p[0] < .044f) continue;
  const float seam = .78f + .24f * ((p[1] + .11f) / .115f);
  belowSeam |= p[2] < seam - .002f;
  aboveSeam |= p[2] > seam + .002f;
 }
 w3d::require(belowSeam && aboveSeam, "soak crosses a duplicated edge with discontinuous UVs");
 info.Push_Override_Flags(RenderInfoClass::RINFO_OVERRIDE_SHADOW_RENDERING);
 w3d::require(captureStains(mesh, info).empty(), "shadow rendering omits the soak overlay");
 info.Pop_Override_Flags();

 Matrix3D moved(true);
 moved.Set_Translation(Vector3(0, 0, .25f));
 actor->Pose(moved);
 VoxelSkin::Tick(35.f);
 const auto movedFaces = captureStains(mesh, info);
 w3d::require(movedFaces.size() == mature.size(), "finished stain persists on a culled body");
 for (size_t i = 0; i < mature.size(); ++i) for (int j = 0; j < 3; ++j) {
  const auto &a = mature[i][j], &b = movedFaces[i][j];
  w3d::require(std::abs(b.xyz[2] - a.xyz[2] - .25f) < 1e-5f &&
               a.uv0[0] == b.uv0[0] && a.uv0[1] == b.uv0[1],
               "finished stain follows posed skin");
 }
 w3d::require(VoxelSkin::Hit(actor, point + Vector3(0,.018f,.25f), ray, 70, 123), "overlapping wound");
 VoxelSkin::Tick(2.5f);
 const auto overlap = captureStains(mesh, info);
 w3d::require(overlap.size() > movedFaces.size(), "overlapping hit adds stain coverage");
 std::set<FaceKey> once;
 bool layered = false;
 for (const auto &face : overlap) {
  const BRVG_RenderVertex key[] = {face[0], face[1], face[2]};
  layered |= !once.insert(faceKey(key)).second;
 }
 w3d::require(layered, "overlapping hits multiply the tint on shared faces");
 actor->Notify_Removed(scene);
 VoxelSkin::Tick(0);
 w3d::require(!VoxelSkin::Manages(actor), "removed corpse frees soak state");
 scene->Release_Ref();
 actor->Release_Ref();
 mesh->Release_Ref();
 std::cout << "SOAK PASSED: cut skin, UV seam, reused UV isolation, spreading, downhill run, stereo, shadow, overlap, corpse cleanup\n";
}

static void checkThighSoak(const HTreeClass &tree, const w3d::Surface &surface, RenderInfoClass &info) {
 auto *mesh = new FixtureMesh(surface);
 auto *actor = new FixtureActor(tree, mesh);
 const Vector3 top = actor->Get_Bone_Transform(tree.Get_Bone_Index("C L THIGH")).Get_Translation();
 const Vector3 bottom = actor->Get_Bone_Transform(tree.Get_Bone_Index("C L CALF")).Get_Translation();
 const Vector3 contact = top + (bottom - top) * .49f + Vector3(0,.09f,0);
 w3d::require(VoxelSkin::Hit(actor, contact, Vector3(0,-1,0), 60, 123), "sideways thigh hit");
 actor->Spread_Legs();
 VoxelSkin::Tick(2.5f);
 const auto stains = captureStains(mesh, info);
 w3d::require(!stains.empty(), "sideways thigh wound still stains its surviving skin");
 for (const auto &face : stains) for (const auto &vertex : face)
  w3d::require(vertex.xyz[1] > .08f, "thigh soak stays on the hit side as the legs separate");
 VoxelSkin::Clear_Level();
 actor->Release_Ref();
 mesh->Release_Ref();
 std::cout << "THIGH SOAK PASSED: stains remain on the struck leg with a clear gap\n";
}

static unsigned queuedPieces = 0;
static void countPiece(RenderObjClass *) { ++queuedPieces; }

static void checkQueuedSeparation(const HTreeClass &tree, const w3d::Surface &surface) {
 auto *mesh = new FixtureMesh(surface);
 auto *actor = new FixtureActor(tree, mesh);
 const Vector3 hip = actor->Get_Bone_Transform(tree.Get_Bone_Index("C R THIGH")).Get_Translation();
 const Vector3 knee = actor->Get_Bone_Transform(tree.Get_Bone_Index("C R CALF")).Get_Translation();
 const Vector3 center = hip + (knee - hip) * .24f;
 VoxelSkin::Set_Piece_Callbacks(countPiece, nullptr, nullptr);
 for (int i = -8; i <= 8; ++i)
  w3d::require(VoxelSkin::Hit(actor, center + Vector3(.14f, i * .015f, 0),
                             Vector3(-1,0,0), 100, 123, false, nullptr, "C R THIGH"),
               "queued right-thigh hit");
 w3d::require(queuedPieces == 0, "rapid hits defer the connectivity scan");
 VoxelSkin::Tick(.07f);
 w3d::require(queuedPieces == 1, "one queued scan releases the right leg");
 VoxelSkin::Clear_Level();
 VoxelSkin::Set_Piece_Callbacks(nullptr, nullptr, nullptr);
 actor->Release_Ref();
 mesh->Release_Ref();
 std::cout << "DEFERRED SEVER PASSED: rapid hits produce one right leg piece on the next eligible tick\n";
}

static void checkCorpseBurst(const HTreeClass &tree, const w3d::Surface &surface, RenderInfoClass &info) {
 auto *mesh = new FixtureMesh(surface);
 auto *actor = new FixtureActor(tree, mesh);
 const auto spine = actor->Get_Bone_Transform(tree.Get_Bone_Index("C SPINE1")).Get_Translation();
 const Vector3 ray(-1,0,0);
 w3d::require(VoxelSkin::Hit(actor, spine + Vector3(.10f,0,.03f), ray, 60, 123, false, nullptr, nullptr, true),
              "initial corpse wound");
 VoxelSkin::Tick(.016f);
 lastSkinIndexCount = 0;
 w3d::require(VoxelSkin::Render(mesh, info) && lastSkinIndexCount > 0, "initial cut corpse draw");
 const unsigned first = lastSkinIndexCount;
 for (int i = 0; i < 144; ++i) {
  const float y = ((i * 7) % 17 - 8) * .021f;
  const float z = .82f + ((i * 5 + i / 17) % 19) * .036f;
  w3d::require(VoxelSkin::Hit(actor, Vector3(.16f,y,z), ray, 60, 123, false, nullptr, nullptr, true),
               "sustained fire on one corpse");
  VoxelSkin::Tick(.06f);
  lastSkinIndexCount = 0;
  w3d::require(VoxelSkin::Render(mesh, info) && lastSkinIndexCount > 0,
               "corpse keeps drawing private cut skin during sustained fire");
 }
 VoxelSkin::Tick(.20f);
 lastSkinIndexCount = 0;
 w3d::require(VoxelSkin::Render(mesh, info) && lastSkinIndexCount > 0 && lastSkinIndexCount != first,
              "last corpse draw retains cumulative cuts");
 VoxelSkin::Clear_Level();
 actor->Release_Ref();
 mesh->Release_Ref();
 std::cout << "CORPSE BURST PASSED: 144 shots, continuous cut skin and accumulated geometry\n";
}
static void checkBlastAndBurn(const HTreeClass &tree,const w3d::Surface &surface,RenderInfoClass &info)
{
 auto *mesh = new FixtureMesh(surface);
 auto *actor = new FixtureActor(tree,mesh);
 VoxelSkin::Tick(.016f);
 const int bone = tree.Get_Bone_Index("C SPINE1");
 w3d::require(bone > 0,"blast fixture chest bone");
 const Vector3 center = tree.Get_Transform(bone).Get_Translation();
 size_t nearest = surface.rest.size();
 float distance = 1.f;
 for (size_t i=0;i<surface.rest.size();++i) {
  if (surface.bone[i] != bone || surface.rest[i].x < center.X+.07f) continue;
  const auto p = surface.rest[i];
  const float d = (Vector3(p.x,p.y,p.z)-center).Length2();
  if (d < distance) { distance=d; nearest=i; }
 }
 w3d::require(nearest < surface.rest.size(),"blast fixture exposed chest");
 const auto p = surface.rest[nearest];
 const Vector3 point(p.x,p.y,p.z), outward(1,0,0);
 w3d::require(VoxelSkin::Burn(actor,point,outward,"C SPINE1",4,.15f,2,false),"burn-only adoption");
 captureSoak = true;
 stainFaces.clear();
 solidFaces.clear();
 w3d::require(VoxelSkin::Render(mesh,info) && !stainFaces.empty(),"burn draws before any ballistic wound");
 const unsigned fleshBefore = fleshDraws;
 const Vector3 floorBlast(.60f,-.10f,0.f);
 const Vector3 pelvis = tree.Get_Transform(tree.Get_Bone_Index("C PELVIS")).Get_Translation();
 Vector3 incoming = pelvis-floorBlast;
 incoming.Normalize();
 w3d::require(VoxelSkin::Blast(actor,pelvis-incoming*.12f,incoming,"C PELVIS",.085f,.051f,false,true),
              "off-body floor splash carves the exposed pelvis");
 VoxelSkin::Tick(.20f);
 stainFaces.clear();
 solidFaces.clear();
 w3d::require(VoxelSkin::Render(mesh,info) && fleshDraws>fleshBefore,"blast draws actual voxel wound walls");
 w3d::require(!stainFaces.empty(),"burn persists after blast");
 for (const auto &face : stainFaces) {
  const BRVG_RenderVertex vertices[] = {face[0],face[1],face[2]};
  w3d::require(solidFaces.count(faceKey(vertices))!=0,"burn cannot fill blast holes");
 }
 VoxelSkin::Clear_Burn(actor);
 stainFaces.clear();
 solidFaces.clear();
 w3d::require(VoxelSkin::Render(mesh,info) && stainFaces.empty(),"chemical transition clears only the burn coating");
 captureSoak = false;
 VoxelSkin::Clear_Level();
 VoxelSkin::Tick(.016f);
 const unsigned burnedFleshBefore = fleshDraws;
 w3d::require(VoxelSkin::Burn(actor,point,outward,"C SPINE1",4,.42f,3,true),"maximum spreading burn");
 w3d::require(VoxelSkin::Render(mesh,info) && fleshDraws>burnedFleshBefore,
              "spreading fire alone exposes voxel flesh");
 const Vector3 hip = tree.Get_Transform(tree.Get_Bone_Index("C R THIGH")).Get_Translation();
 const Vector3 knee = tree.Get_Transform(tree.Get_Bone_Index("C R CALF")).Get_Translation();
 Vector3 axis = knee-hip;
 const Vector3 joint = hip+axis*.24f;
 axis.Normalize();
 VoxelSkin::Set_Piece_Callbacks(countPiece,nullptr,nullptr);
 const unsigned piecesBefore = queuedPieces;
 w3d::require(VoxelSkin::Blast(actor,joint,axis,"C R THIGH",.162f,.042f,true,true),"close blast cuts a joint");
 VoxelSkin::Tick(.07f);
 if (queuedPieces != piecesBefore+1) std::cerr << "Close blast detached pieces: " << queuedPieces-piecesBefore << "\n";
 w3d::require(queuedPieces==piecesBefore+1,"close blast releases the struck limb only");
 VoxelSkin::Set_Piece_Callbacks(nullptr,nullptr,nullptr);
 VoxelSkin::Clear_Level();
 actor->Release_Ref();
 mesh->Release_Ref();
 std::cout << "BLAST AND BURN PASSED: off-body floor splash, burn follows cut skin, spreading erosion, close blast separation, chemical transition\n";
}

int main(int argc,char **argv){try{
 w3d::require(argc==4 || argc==6,"native_bridge_check body.w3d hierarchy.w3d local-output/gore.ini [head.w3d head-tree.w3d]");auto sk=w3d::skeleton(w3d::Bytes(argv[2]));auto model=w3d::mesh(w3d::Bytes(argv[1]),sk);w3d::Bytes bytes(argv[2]);RAMFileClass file(bytes.data.data(),int(bytes.data.size()));file.Open(FileClass::READ);ChunkLoadClass chunks(&file);w3d::require(chunks.Open_Chunk(),"hierarchy chunk");HTreeClass tree;w3d::require(tree.Load_W3D(chunks)==HTreeClass::OK,"engine hierarchy");chunks.Close_Chunk();
 auto *mesh=new FixtureMesh(model.surfaces[0]);auto *actor=new FixtureActor(tree,mesh);auto *otherMesh=static_cast<MeshClass*>(mesh->Clone());auto *other=new FixtureActor(tree,otherMesh);
 VoxelSkin::Configure(true,argv[3]);VoxelSkin::Set_Offline_Draw_Sink(sink);VoxelSkin::Tick(.016f);w3d::require(!VoxelSkin::Manages(actor),"initial intact actor");int spine=tree.Get_Bone_Index("C SPINE1");tree.Base_Update(Matrix3D::Identity);Vector3 point=tree.Get_Transform(spine).Get_Translation();point.Z+=.03f;
 w3d::require(VoxelSkin::Hit(actor,point,Vector3(-1,0,0),60,123),"native precise hit");w3d::require(VoxelSkin::Manages(actor)&&!VoxelSkin::Manages(other),"instance isolation");
 CameraClass camera;
 RenderInfoClass info(camera);
 Matrix3D view(true), world(true);
 view.Set_Translation(Vector3(3, -2, 1));
 world.Set_Translation(Vector3(-4, 1, 2));
 DX8Wrapper::Set_Transform(D3DTS_VIEW, view);
 DX8Wrapper::Set_Transform(D3DTS_WORLD, world);
 w3d::require(VoxelSkin::Render(mesh,info),"native private draw path");
 w3d::require(skinDraws>0&&fleshDraws>0,"cut skin and flesh submissions");
 unsigned before=draws;
 mesh->Render(info);
 w3d::require(draws>before,"MeshClass::Render hook consumes private geometry");
 // Particle and sorting draws query these transforms before restoring them.
 Matrix4 restoredView, restoredWorld;
 DX8Wrapper::Get_Transform(D3DTS_VIEW, restoredView);
 DX8Wrapper::Get_Transform(D3DTS_WORLD, restoredWorld);
 const Matrix4 expectedView(view), expectedWorld(world);
 for (int row = 0; row < 4; ++row) {
  for (int column = 0; column < 4; ++column) {
   w3d::require(restoredView[row][column] == expectedView[row][column], "camera transform survives private draw");
   w3d::require(restoredWorld[row][column] == expectedWorld[row][column], "world transform survives private draw");
  }
 }
 w3d::require(!VoxelSkin::Render(otherMesh,info),"shared intact mesh keeps original pipeline");
 checkSoaking(tree, info);
 checkThighSoak(tree, model.surfaces[0], info);
 checkQueuedSeparation(tree, model.surfaces[0]);
 checkCorpseBurst(tree, model.surfaces[0], info);
 checkBlastAndBurn(tree, model.surfaces[0], info);
 if(argc==6){auto hs=w3d::skeleton(w3d::Bytes(argv[5]));auto hm=w3d::mesh(w3d::Bytes(argv[4]),hs,true);w3d::Bytes hb(argv[5]);RAMFileClass hf(hb.data.data(),int(hb.data.size()));hf.Open(FileClass::READ);ChunkLoadClass hc(&hf);w3d::require(hc.Open_Chunk(),"head hierarchy");HTreeClass ht;w3d::require(ht.Load_W3D(hc)==HTreeClass::OK,"actual head tree");hc.Close_Chunk();ht.Base_Update(Matrix3D::Identity);
  auto *high=new FixtureMesh(hm.surfaces[0]);auto *low=new FixtureMesh(hm.surfaces[0]);low->Set_Name("VOXEL_HEAD_DECOY.HEAD3");RenderObjClass *lods[]={low,high};auto *head=new FixtureHlod(lods,ht);Vector3 center(0,0,0);for(auto p:hm.surfaces[0].rest)center+=Vector3(p.x,p.y,p.z);center/=float(hm.surfaces[0].rest.size());
  VoxelSkin::Tick(.016f);w3d::require(VoxelSkin::Hit(head,center,Vector3(-1,0,0),60,123,true),"actual separate head hit");unsigned count=draws;w3d::require(VoxelSkin::Render(high,info)&&draws>count,"highest head LOD draws cut skin/flesh");w3d::require(!VoxelSkin::Render(low,info),"invisible lower head LOD never wounded");std::cout<<"HEAD REGRESSION PASSED: actual Nod head, highest LOD only, cut skin/flesh submissions\n";VoxelSkin::Clear_Level();head->Release_Ref();high->Release_Ref();low->Release_Ref();
 }
 VoxelSkin::Clear_Level();w3d::require(!VoxelSkin::Manages(actor),"unload clears associations");VoxelSkin::Shutdown();VoxelSkin::Set_Offline_Draw_Sink(NULL);actor->Release_Ref();other->Release_Ref();mesh->Release_Ref();otherMesh->Release_Ref();
 std::cout<<"NATIVE BRIDGE PASSED: precise hit, instance isolation, cut/flesh submissions, real MeshClass hook, unload cleanup; "<<draws<<" checked submissions. No GPU/game launched.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" / "<<BRVG_LastError()<<'\n';return 1;}}
