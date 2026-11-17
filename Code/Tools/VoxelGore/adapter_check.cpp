// Real W3D fixture checks for state isolation, stereo cache, immutable deferred
// draw lifetime, rebuild scheduling, generation safety and living-piece separation.
#include "w3d_reader.h"
#include "../../VoxelGore/renegade_voxel.h"
#include "../../VoxelGore/voxel_draw.h"
#include <iostream>
#include <iomanip>

using w3d::require;
static void check(bool b,const char* text){if(!b)throw std::runtime_error(std::string(text)+": "+BRVG_LastError());std::cout<<"PASS "<<text<<'\n';}
static std::vector<float> pack(const std::vector<w3d::TM>& pose){std::vector<float> out;for(auto& m:pose){for(int r=0;r<3;++r)for(int c=0;c<3;++c)out.push_back(m.m[r][c]);out.push_back(m.m[0][3]);out.push_back(m.m[1][3]);out.push_back(m.m[2][3]);}return out;}
int main(int argc,char** argv){try {
 require(argc==3,"usage: voxel_adapter_check body.w3d hierarchy.w3d");auto sk=w3d::skeleton(w3d::Bytes(argv[2]));auto wm=w3d::mesh(w3d::Bytes(argv[1]),sk);auto pose=pack(sk.rest);
 std::vector<BRVG_SurfaceDesc> surfaces;std::vector<std::vector<float>> xyz,normals;std::vector<char> names(sk.names.size()*32,0);
 for(size_t i=0;i<sk.names.size();++i)std::memcpy(names.data()+i*32,sk.names[i].data(),(std::min)(size_t(31),sk.names[i].size()));
 for(size_t i=0;i<wm.surfaces.size();++i){auto& s=wm.surfaces[i];xyz.emplace_back();normals.emplace_back();for(auto p:s.rest){xyz.back().push_back(p.x);xyz.back().push_back(p.y);xyz.back().push_back(p.z);}for(auto p:s.restNormal){normals.back().push_back(p.x);normals.back().push_back(p.y);normals.back().push_back(p.z);}BRVG_SurfaceDesc d{};d.identity=1001+i;d.vertex_count=uint32_t(s.rest.size());d.triangle_count=uint32_t(s.idx.size()/3);d.rest_xyz=xyz.back().data();d.rest_normals=normals.back().data();d.bones=s.bone.data();d.indices=s.idx.data();surfaces.push_back(d);}
 BRVG_ModelDesc desc{};desc.size=sizeof(desc);desc.version=BRVG_ABI_VERSION;desc.bone_count=uint32_t(sk.rest.size());desc.surface_count=uint32_t(surfaces.size());desc.rest_palette=pose.data();desc.parents=sk.parents.data();desc.bone_names=names.data();desc.surfaces=surfaces.data();
 auto model=BRVG_CreateModel(&desc);check(model!=0,"real Nod model, 1cm default template");auto a=BRVG_CreateBody(model),b=BRVG_CreateBody(model);check(a&&b&&a!=b,"independent soldier voxel copies");
 auto bone=[&](const char* name){for(size_t i=0;i<sk.names.size();++i)if(sk.names[i]==name)return int(i);throw std::runtime_error("missing bone");};
 auto chest=sk.rest[bone("C SPINE1")].origin();float point[]={chest.x,chest.y,chest.z+.03f},dir[]={-1,0,0};BRVG_HitResult hit{};check(BRVG_Shot(a,pose.data(),desc.bone_count,point,dir,.014f,.028f,1.65f,&hit)>0,"bind-space rifle carve");
 float light[]={255,255,255,0,0,0,0,0,1};BRVG_BeginFrame(100);check(BRVG_AcquireDraw(a,pose.data(),desc.bone_count,light,0)==nullptr && BRVG_GetStats().rebuild_count==0,"culled soldier does no rebuild");
 auto first=BRVG_AcquireDraw(a,pose.data(),desc.bone_count,light,1);check(first!=nullptr,"wounded draw snapshot");auto* view=BRVG_GetDraw(first);check(view && view->two_sided && view->flesh_vertex_count>0,"two-sided wound walls and original skin cuts");
 // Build a real private material-grouped draw using two UV channels and
 // already-posed vertices. Verify subs use their original triangle's bucket.
 auto& src=wm.surfaces[0];auto& cut=view->surfaces[0];std::vector<BRVG_RenderVertex> vertices(src.rest.size());std::vector<uint32_t> groups(src.idx.size()/3);
 for(size_t i=0;i<vertices.size();++i){auto p=src.rest[i],n=src.restNormal[i];vertices[i]={{p.x,p.y,p.z},{n.x,n.y,n.z},0xff80a060u,{p.x,p.y},{p.y,p.z}};}
 for(size_t i=0;i<groups.size();++i)groups[i]=uint32_t(i%2);auto copy=vertices;
 auto* packet=BRVG_BuildPacket(&cut,cut.identity,uint32_t(vertices.size()),uint32_t(groups.size()),src.idx.data(),vertices.data(),groups.data());check(packet!=nullptr,"private material-grouped skin draw packet");auto* pv=BRVG_GetPacket(packet);
 check(pv->vertex_count==vertices.size()+cut.sub_count*3 && pv->index_count==uint32_t((cut.keep_count<0?cut.triangle_count:cut.keep_count)+cut.sub_count)*3,"exact VB and IB reservations including wound subs");
 uint32_t expected[2]={0,0};if(cut.keep_count<0)for(auto g:groups)expected[g]+=3;else for(int i=0;i<cut.keep_count;++i)expected[groups[cut.keep[i]]]+=3;for(uint32_t i=0;i<cut.sub_count;++i)expected[groups[uint32_t(cut.sub[i*10])]]+=3;
 check(pv->range_count==2 && pv->ranges[0].index_count==expected[0] && pv->ranges[1].index_count==expected[1],"subtriangles retain original material/pass groups");
 check(!memcmp(copy.data(),vertices.data(),vertices.size()*sizeof(vertices[0])),"shared posed source geometry remains untouched");
 if(cut.sub_count){const float* sub=cut.sub;uint32_t t=uint32_t(sub[0]);float uv0=0,uv1=0;for(int j=0;j<3;++j){auto& v=vertices[src.idx[t*3+j]];uv0+=v.uv0[1]*sub[1+j];uv1+=v.uv1[1]*sub[1+j];}auto& v=pv->vertices[vertices.size()];check(std::abs(v.uv0[1]-uv0)<1e-5f && std::abs(v.uv1[1]-uv1)<1e-5f,"both UV channels follow skinned barycentric corners");}
 check(BRVG_BuildPacket(&cut,cut.identity+1,uint32_t(vertices.size()),uint32_t(groups.size()),src.idx.data(),vertices.data(),groups.data())==nullptr,"wrong mesh/LOD identity falls back intact");
 BRVG_SurfaceView oversized=cut;oversized.vertex_count=65534;oversized.sub_count=1;check(BRVG_BuildPacket(&oversized,cut.identity,65534,uint32_t(groups.size()),src.idx.data(),vertices.data(),groups.data())==nullptr,"16-bit overflow rejected before vertex reads or writes");BRVG_FreePacket(packet);
 std::vector<float> frozen(view->flesh_xyz,view->flesh_xyz+size_t(view->flesh_vertex_count)*3);auto* second=BRVG_AcquireDraw(a,pose.data(),desc.bone_count,light,1);check(second==first && BRVG_GetStats().pose_cache_hits==1,"both VR eyes reuse identical posed snapshot");BRVG_ReleaseDraw(second);
 check(BRVG_Shot(b,pose.data(),desc.bone_count,point,dir,.014f,.028f,1.65f,&hit)>0,"second soldier independent carve");check(BRVG_AcquireDraw(b,pose.data(),desc.bone_count,light,1)==nullptr && BRVG_GetStats().rebuild_count==1,"strict one-body rebuild per frame including first hit");
 BRVG_BeginFrame(101);auto* bd=BRVG_AcquireDraw(b,pose.data(),desc.bone_count,light,1);check(bd && BRVG_GetStats().rebuild_count==2,"deferred second victim rebuilds next frame");BRVG_ReleaseDraw(bd);
 auto changed=pose;for(size_t i=0;i<sk.rest.size();++i)changed[i*12+9]+=.25f;BRVG_BeginFrame(102);auto* moved=BRVG_AcquireDraw(a,changed.data(),desc.bone_count,light,1);check(moved && moved!=first,"new pose produces independent immutable snapshot");auto* mv=BRVG_GetDraw(moved);check(std::abs(mv->flesh_xyz[0]-frozen[0]-.25f)<1e-5f,"wound follows bone pose without world offset");check(!std::memcmp(BRVG_GetDraw(first)->flesh_xyz,frozen.data(),frozen.size()*4),"old deferred draw remains unchanged");BRVG_ReleaseDraw(moved);
 BRVG_FreeBody(a);auto replacement=BRVG_CreateBody(model);check(replacement && replacement!=a && BRVG_Shot(a,pose.data(),desc.bone_count,point,dir,.014f,.028f,1.65f,&hit)==0,"stale soldier generation rejected after reuse");BRVG_FreeBody(b);BRVG_FreeBody(replacement);
 const int leftThigh = bone("C L THIGH"), rightThigh = bone("C R THIGH");
 auto spread = pose;
 for (size_t i = 0; i < sk.names.size(); ++i) {
  for (int parent = int(i); parent >= 0; parent = sk.parents[parent]) {
   if (parent == leftThigh || parent == rightThigh) {
    spread[i * 12 + 10] += parent == leftThigh ? .18f : -.18f;
    break;
   }
  }
 }
 float longestThighEdge = 0;
 for (int i = 0; i < 12; ++i) {
  const bool left = i < 6;
  const int thighBone = left ? leftThigh : rightThigh;
  const float side = left ? 1.f : -1.f;
  const auto thigh = sk.rest[thighBone].origin();
  const auto calf = sk.rest[bone(left ? "C L CALF" : "C R CALF")].origin();
  auto legs = BRVG_CreateBody(model);
  const auto center = thigh + (calf - thigh) * (.12f + .08f * (i % 6));
  const bool alreadySpread = i % 2 != 0;
  const float outerOffset = alreadySpread ? .26f : .08f;
  const float contact[] = {center.x, center.y + side * outerOffset, center.z};
  const float acrossLegs[] = {0, -side, 0};
  const auto& hitPose = alreadySpread ? spread : pose;
  require(BRVG_ShotOnBone(legs, hitPose.data(), desc.bone_count, thighBone, contact, acrossLegs, .024f, .048f, 2, &hit) > 0 && hit.exited,
          "sideways thigh shot still carves through tissue");
  BRVG_BeginFrame(103 + i);
  auto* shot = BRVG_AcquireDraw(legs, spread.data(), desc.bone_count, light, 1);
  require(shot, "sideways thigh shot snapshot");
  const auto* legDraw = BRVG_GetDraw(shot);
  require(legDraw->flesh_index_count > 0 && legDraw->surfaces[0].sub_count > 0,
          "thigh wound keeps its opening and flesh walls");
  for (unsigned j = 0; j < legDraw->flesh_index_count; j += 3) {
   for (int k = 0; k < 3; ++k) {
    const float* start = legDraw->flesh_xyz + 3 * legDraw->flesh_indices[j + k];
    const float* end = legDraw->flesh_xyz + 3 * legDraw->flesh_indices[j + (k + 1) % 3];
    longestThighEdge = (std::max)(longestThighEdge, gore::len({end[0] - start[0], end[1] - start[1], end[2] - start[2]}));
    require(side * start[1] > .08f, "flesh stays on the struck thigh and clear of the gap");
   }
  }
  BRVG_ReleaseDraw(shot);
  BRVG_FreeBody(legs);
 }
 check(longestThighEdge < .04f, "twelve sideways shots keep wound faces on the struck thigh in both poses");
 auto pairedLegs = BRVG_CreateBody(model);
 auto cutThigh = [&](bool left) {
  const int thighBone = left ? leftThigh : rightThigh;
  const auto hip = sk.rest[thighBone].origin();
  const auto knee = sk.rest[bone(left ? "C L CALF" : "C R CALF")].origin();
  const auto cut = hip + (knee - hip) * .24f;
  const float incoming[] = {-1, 0, 0};
  for (int i = -8; i <= 8; ++i) {
   const float contact[] = {cut.x + .14f, cut.y + i * .015f, cut.z};
   BRVG_ShotOnBone(pairedLegs, pose.data(), desc.bone_count, thighBone, contact, incoming, .03f, .05f, 2, &hit);
  }
  BRVG_Piece pieces[8]{};
  const unsigned n = BRVG_Separate(pairedLegs, pieces, 8);
  bool separated = false;
  for (unsigned i = 0; i < n; ++i) {
   for (int parent = pieces[i].bone; parent >= 0; parent = sk.parents[parent]) {
    if (parent == (left ? rightThigh : leftThigh)) require(false, "uncut thigh must stay attached");
    if (parent == thighBone) separated = true;
   }
   BRVG_FreeBody(pieces[i].body);
  }
  return separated;
 };
 check(cutThigh(false), "right thigh detaches while the left leg is intact");
 check(cutThigh(true), "left thigh can detach independently afterwards");
 BRVG_FreeBody(pairedLegs);
 auto living=BRVG_CreateBody(model);int elbow=bone("C L FOREARM"),hand=bone("C L HAND");auto p0=sk.rest[elbow].origin(),p1=sk.rest[hand].origin(),along=gore::norm(p1-p0),across=gore::norm(gore::cross(along,{0,0,1}));if(gore::len(across)<.5f)across={1,0,0};auto sweep=gore::norm(gore::cross(along,across)),center=(p0+p1)*.5f;float incoming[]={across.x,across.y,across.z};unsigned detached=0;
 for(int i=-6;i<=6;++i){auto p=center+sweep*(i*.02f);float hitpoint[]={p.x,p.y,p.z};if(BRVG_Shot(living,pose.data(),desc.bone_count,hitpoint,incoming,.03f,.05f,2,&hit)){BRVG_Piece pieces[8]{};unsigned n=BRVG_Separate(living,pieces,8);detached+=n;for(unsigned j=0;j<n;++j)BRVG_FreeBody(pieces[j].body);}}
 check(detached>0,"shot-through limb separates with no death prerequisite");
 BRVG_FreeBody(living);
 std::vector<BRVG_Body> crowd;
 auto addWounded = [&](unsigned i) {
  const BRVG_Body soldier = BRVG_CreateBody(model);
  require(soldier != 0, BRVG_LastError());
  crowd.push_back(soldier);
  require(BRVG_Shot(soldier, pose.data(), desc.bone_count, point, dir, .014f, .028f, 1.65f, &hit) > 0,
          "new soldier accepts wound after previous bodies accumulate");
  BRVG_BeginFrame(1000 + i);
  const auto *wound = BRVG_AcquireDraw(soldier, pose.data(), desc.bone_count, light, 1);
  require(wound != nullptr, BRVG_LastError());
  require(BRVG_GetDraw(wound)->flesh_index_count > 0, "new soldier keeps renderable holes");
  BRVG_ReleaseDraw(wound);
 };
 for (unsigned i = 0; i < 64; ++i) addWounded(i);
 const auto crowdStats = BRVG_GetStats();
 check(crowdStats.body_count == 64 && crowdStats.owned_bytes < 128ull*1024*1024,
       "64 simultaneously wounded soldiers fit within the existing memory budget");
 std::cout << "CROWD MEMORY " << crowdStats.owned_bytes / (1024 * 1024) << " MiB\n";
 check(!BRVG_FreeModel(model), "active body keeps its shared voxel template alive");
 const auto *oldWound = BRVG_AcquireDraw(crowd.front(), pose.data(), desc.bone_count, light, 1);
 check(oldWound && BRVG_GetDraw(oldWound)->flesh_index_count > 0, "oldest wound remains after new soldiers are shot");
 const unsigned oldWallCount = BRVG_GetDraw(oldWound)->flesh_index_count;
 const unsigned oldRimCount = BRVG_GetDraw(oldWound)->surfaces[0].sub_count;
 BRVG_ReleaseDraw(oldWound);
 for (unsigned i = 0; i < 48; ++i)
  require(BRVG_CompactBody(crowd[i]), "offscreen soldier compacts its voxel edits");
 check(BRVG_GetStats().owned_bytes < crowdStats.owned_bytes / 2,
       "compaction reclaims offscreen grid and draw memory");
 for (unsigned i = 64; i < 96; ++i) addWounded(i);
 BRVG_BeginFrame(1100);
 const auto *revisited = BRVG_AcquireDraw(crowd.front(), pose.data(), desc.bone_count, light, 1);
 check(revisited && BRVG_GetDraw(revisited)->flesh_index_count == oldWallCount &&
       BRVG_GetDraw(revisited)->surfaces[0].sub_count == oldRimCount,
       "old wound geometry is restored after 96 soldiers are wounded");
 BRVG_ReleaseDraw(revisited);
 std::cout << "CROWD WITH COMPACTION " << BRVG_GetStats().owned_bytes / (1024 * 1024) << " MiB\n";
 for (auto soldier : crowd) BRVG_FreeBody(soldier);
 check(BRVG_FreeModel(model) && BRVG_GetStats().model_count == 0, "unused model slot and shared grid are released");
 const auto reloadedModel = BRVG_CreateModel(&desc);
 check(reloadedModel && reloadedModel != model && !BRVG_CreateBody(model),
       "reused model slot rejects stale handles");
 BRVG_Shutdown();
 check(BRVG_GetDraw(first)!=nullptr,"pending render snapshot survives victim and level cleanup");
 BRVG_ReleaseDraw(first);
 check(BRVG_GetStats().snapshot_count==0 && BRVG_GetStats().body_count==0,"all snapshots and soldiers released");
 std::cout<<"ADAPTER CHECKS PASSED; gameplay/DX8 integration not active\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
