// Offline Renegade proof, GPL-3.0-or-later. Not part of any game build.
// prepare.py makes a private reference-wrapper copy with METRES=1; all generic
// cut/refine/pose/sever algorithms stay unchanged. Its MDS/MD3 entry points are unused.
#include "w3d_reader.h"
#include "build/cgame/cg_gore.cpp"
#include <chrono>
#include <iomanip>
#include <iostream>

namespace proof {
using w3d::require;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a) { return std::chrono::duration<double,std::milli>(Clock::now()-a).count(); }
std::ofstream logFile;
void say(const std::string& s) { std::cout<<s<<'\n';logFile<<s<<'\n'; }
std::ofstream output(const std::string& path) { std::ofstream f(path);require(bool(f),"cannot write "+path);f<<std::setprecision(9);return f; }
void vertex(std::ostream& f,V3 p) { require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"nonfinite output");f<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n'; }
void meshObj(const std::string& path,const gore::Mesh& m) {
    auto f=output(path);for(auto p:m.pos)vertex(f,p);
    for(size_t i=0;i<m.idx.size();i+=3){for(int j=0;j<3;++j)require(m.idx[i+j]<m.pos.size(),"flesh index");f<<"f "<<m.idx[i]+1<<' '<<m.idx[i+1]+1<<' '<<m.idx[i+2]+1<<'\n';}
}
void skinObj(const std::string& path,const w3d::Model& model,const std::vector<w3d::TM>& pose,const vrGoreDraw_t* draw,V3 offset={0,0,0}) {
    auto f=output(path);size_t base=0;
    require(!draw || draw->numSurfaces==model.surfaces.size(),"surface count mismatch");
    for(size_t si=0;si<model.surfaces.size();++si) {
        const auto& s=model.surfaces[si];f<<"o "<<s.name<<'\n';std::vector<V3> pos;
        for(size_t v=0;v<s.local.size();++v){auto p=pose[s.bone[v]].point(s.local[v]);pos.push_back(p);vertex(f,p+offset);}
        const auto* g=draw?&draw->surf[si]:nullptr;
        require(!g || (g->numVerts==s.local.size() && g->numTriangles==s.idx.size()/3),"surface geometry mismatch");
        auto tri=[&](int t){require(t>=0 && size_t(t)*3+2<s.idx.size(),"kept triangle index");f<<"f "<<base+s.idx[t*3]+1<<' '<<base+s.idx[t*3+1]+1<<' '<<base+s.idx[t*3+2]+1<<'\n';};
        if(!g || g->numKeep<0)for(size_t t=0;t<s.idx.size()/3;++t)tri(int(t));
        else for(int k=0;k<g->numKeep;++k)tri(g->keep[k]);
        base+=pos.size();
        if(g)for(int k=0;k<g->numSub;++k) {
            const float* b=g->sub+k*VRG_SUB_FLOATS;int t=int(b[0]);require(t>=0 && size_t(t)*3+2<s.idx.size() && b[0]==float(t),"sub source triangle");
            for(int c=0;c<3;++c) {
                V3 p{0,0,0};float sum=0;
                for(int j=0;j<3;++j){float w=b[1+c*3+j];require(w>=-1e-5f && w<=1.00001f,"barycentric range");sum+=w;p=p+pos[s.idx[t*3+j]]*w;}
                require(std::abs(sum-1)<1e-5f,"barycentric sum");vertex(f,p+offset);
            }
            f<<"f "<<base+1<<' '<<base+2<<' '<<base+3<<'\n';base+=3;
        }
    }
}
void fleshObj(const std::string& path,const vrGoreDraw_t* draw,V3 offset={0,0,0}) {
    require(draw!=nullptr,"no draw data");auto f=output(path);
    for(int i=0;i<draw->numFleshVerts;++i){V3 p{draw->fleshXyz[i*3],draw->fleshXyz[i*3+1],draw->fleshXyz[i*3+2]};p=p+offset;
        require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"nonfinite flesh");
        f<<"v "<<p.x<<' '<<p.y<<' '<<p.z;
        for(int c=0;c<3;++c)f<<' '<<draw->fleshRGBA[i*4+c]/255.0f;
        f<<'\n';}
    for(int i=0;i<draw->numFleshIndexes;i+=3){for(int j=0;j<3;++j)require(draw->fleshIndexes[i+j]>=0 && draw->fleshIndexes[i+j]<draw->numFleshVerts,"posed flesh index");f<<"f "<<draw->fleshIndexes[i]+1<<' '<<draw->fleshIndexes[i+1]+1<<' '<<draw->fleshIndexes[i+2]+1<<'\n';}
}
std::vector<float> pack(const std::vector<w3d::TM>& pose) {
    std::vector<float> out;for(auto& p:pose){for(int i=0;i<3;++i)for(int j=0;j<3;++j)out.push_back(p.m[i][j]);out.push_back(p.m[0][3]);out.push_back(p.m[1][3]);out.push_back(p.m[2][3]);}return out;
}
int bone(const w3d::Skeleton& s,const std::string& name) { for(size_t i=0;i<s.names.size();++i)if(s.names[i]==name)return int(i);throw std::runtime_error("missing bone "+name); }
const vrGoreDraw_t* draw(int body,const std::vector<float>& pose) { float ambient[]={255,255,255},directed[]={0,0,0},light[]={0,0,1};return Gore_Draw(body,pose.data(),ambient,directed,light); }
int install(const w3d::Model& wm,const w3d::Skeleton& sk,float cell) {
    require(wm.surfaces.size()<=VRG_MAX_SURFS,"wrapper surface limit");
    auto m=std::make_unique<Model>();m->numBones=int(sk.names.size());m->parents=sk.parents;m->flags.resize(m->numBones,0);auto pose=pack(sk.rest);
    for(int i=0;i<m->numBones;++i){m->rest.push_back(PoseBone(pose.data(),i));m->restInv.push_back(Inverse(m->rest.back()));}
    gore::Body body;
    for(auto& ws:wm.surfaces) {
        Surf s;s.numVerts=int(ws.rest.size());s.numTris=int(ws.idx.size()/3);s.xyz=ws.rest;s.nrm=ws.restNormal;s.tri.assign(ws.idx.begin(),ws.idx.end());
        auto base=uint32_t(body.pos.size());body.pos.insert(body.pos.end(),ws.rest.begin(),ws.rest.end());for(auto b:ws.bone){require(b<255,"core bone limit");body.bone.push_back(uint8_t(b));}
        for(auto ix:ws.idx)body.idx.push_back(base+ix);m->surfs.push_back(std::move(s));
    }
    V3 lo=body.pos.front(),hi=lo;for(auto p:body.pos){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    say("rest bounds m: ("+std::to_string(lo.x)+","+std::to_string(lo.y)+","+std::to_string(lo.z)+") to ("+std::to_string(hi.x)+","+std::to_string(hi.y)+","+std::to_string(hi.z)+")");
    require(hi.z-lo.z>.8f && hi.z-lo.z<3.0f,"implausible soldier height");
    auto t=Clock::now();require(m->vb.build(body,cell),"voxel build failed");say("build ms: "+std::to_string(ms(t)));
    for(size_t i=1;i<sk.names.size();++i) {
        int p=sk.parents[i];auto name=sk.names[p];float radius=0;
        if(name.find("SPINE")!=std::string::npos)radius=.017f;
        else if(name.find("PELVIS")!=std::string::npos)radius=.025f;
        else if(name.find("UPPERARM")!=std::string::npos || name.find("FOREARM")!=std::string::npos)radius=.011f;
        else if(name.find("THIGH")!=std::string::npos)radius=.016f;
        else if(name.find("CALF")!=std::string::npos)radius=.013f;
        if(radius)m->vb.addBoneRod(sk.rest[p].origin(),sk.rest[i].origin(),radius,uint8_t(p));
    }
    size_t filled=0,bones=0;for(auto c:m->vb.state){filled+=c==gore::SOLID||c==gore::BONE;bones+=c==gore::BONE;}
    say("vertices "+std::to_string(body.pos.size())+", triangles "+std::to_string(body.idx.size()/3)+", cells "+std::to_string(m->vb.state.size())+", filled "+std::to_string(filled)+", bone "+std::to_string(bones));
    require(filled>1000 && bones>0,"empty voxel/bone proof");
    auto baseline=m->vb;auto pieces=baseline.pieces(40);say("baseline components >=40 cells: "+std::to_string(pieces.size()));
    for(auto& p:pieces)say(" baseline component cells "+std::to_string(p.cells)+" bone "+sk.names[p.mainBone]);
    meshObj("voxels.obj",m->vb.remesh(false));models[1]=std::move(m);return 1;
}
void stats(const vrGoreDraw_t* d) {
    int keep=0,sub=0;for(int i=0;i<d->numSurfaces;++i){keep+=d->surf[i].numKeep<0?d->surf[i].numTriangles:d->surf[i].numKeep;sub+=d->surf[i].numSub;}
    say("kept "+std::to_string(keep)+", subs "+std::to_string(sub)+", flesh vertices "+std::to_string(d->numFleshVerts)+", flesh triangles "+std::to_string(d->numFleshIndexes/3));
}
int run(int argc,char** argv) {
    require(argc==3,"usage: offline mesh.w3d hierarchy.w3d (outputs in current directory)");logFile=output("proof.log");
    auto sk=w3d::skeleton(w3d::Bytes(argv[2]));auto wm=w3d::mesh(w3d::Bytes(argv[1]),sk);
    say("W3D hierarchy "+sk.name+", bones "+std::to_string(sk.rest.size())+", visible skins "+std::to_string(wm.surfaces.size()));
    for(auto& s:wm.surfaces)say("include "+s.name);for(auto& s:wm.excluded)say("exclude "+s);
    auto bones=output("bones.obj");for(auto& p:sk.rest)vertex(bones,p.origin());
    auto palette=output("rest_palette.txt");
    for(size_t i=0;i<sk.rest.size();++i){palette<<i<<' '<<sk.names[i]<<" parent "<<sk.parents[i];for(int row=0;row<3;++row)for(int col=0;col<4;++col)palette<<' '<<sk.rest[i].m[row][col];palette<<'\n';}
    for(size_t i=1;i<sk.rest.size();++i)bones<<"l "<<sk.parents[i]+1<<' '<<i+1<<'\n';
    int model=install(wm,sk,.015f);auto pose=pack(sk.rest);skinObj("rest.obj",wm,sk.rest,nullptr);
    int body=Gore_NewBody(model);int spine=bone(sk,"C SPINE1");V3 chest=sk.rest[spine].origin();float incoming[]={-1,0,0};
    int total=0;
    for(int i=-1;i<=1;++i){float point[]={chest.x,chest.y+i*.055f,chest.z+.03f};goreShot_t result{};auto t=Clock::now();total+=Gore_Shot(body,pose.data(),point,incoming,.026f,.05f,1.65f,&result);say("chest shot cells "+std::to_string(result.cells)+", bone "+std::to_string(result.boneCells)+", exit "+std::to_string(result.exited)+", ms "+std::to_string(ms(t)));}
    require(total>0,"all chest shots missed");auto t=Clock::now();auto d=draw(body,pose);say("wound rebuild/pose ms: "+std::to_string(ms(t)));stats(d);require(d->numFleshIndexes>0,"no wound flesh");
    skinObj("cut_skin.obj",wm,sk.rest,d);fleshObj("flesh.obj",d);meshObj("flesh_depth.obj",GetBody(body)->flesh);
    const auto& flesh=GetBody(body)->flesh;int boneVerts=0,rimVerts=0;
    for(auto depth:flesh.depthCm){boneVerts+=depth==255;rimVerts+=depth<=2;}
    say("wound layer vertices: bone "+std::to_string(boneVerts)+", rim <=2cm "+std::to_string(rimVerts)+", deeper flesh "+std::to_string(int(flesh.depthCm.size())-boneVerts-rimVerts));
    // A synthetic palette checks the exact cut-list interpolation path without pretending to parse W3D animation.
    auto synthetic=sk.rest;int elbow=bone(sk,"C L FOREARM");V3 pivot=sk.rest[elbow].origin();w3d::TM bend;
    float angle=.65f;bend.m[0][0]=cosf(angle);bend.m[0][2]=sinf(angle);bend.m[2][0]=-sinf(angle);bend.m[2][2]=cosf(angle);
    auto rotated=bend.dir(pivot);auto offset=pivot-rotated;bend.m[0][3]=offset.x;bend.m[1][3]=offset.y;bend.m[2][3]=offset.z;
    for(size_t i=0;i<synthetic.size();++i){int ancestor=int(i);while(ancestor>0 && ancestor!=elbow)ancestor=sk.parents[ancestor];if(ancestor==elbow)synthetic[i]=w3d::mul(bend,synthetic[i]);}
    auto syntheticPose=pack(synthetic);auto ad=draw(body,syntheticPose);skinObj("posed_skin.obj",wm,synthetic,ad);fleshObj("posed_flesh.obj",ad);
    float input[]={pivot.x+.03f,pivot.y+.02f,pivot.z+.01f},actual[3];Gore_RestToPose(body,syntheticPose.data(),elbow,input,actual);auto expected=bend.point({input[0],input[1],input[2]});require(gore::len(V3{actual[0],actual[1],actual[2]}-expected)<1e-5f,"synthetic flesh pose mismatch");
    say("synthetic elbow flesh transform check: passed (<1e-5 m); this is not engine animation parity");
    int corpse=Gore_NewBody(model);int hand=bone(sk,"C L HAND");auto a=sk.rest[elbow].origin(),b=sk.rest[hand].origin();auto along=gore::norm(b-a);auto across=gore::norm(gore::cross(along,{0,0,1}));if(gore::len(across)<.5f)across={1,0,0};auto sweep=gore::norm(gore::cross(along,across));auto center=(a+b)*.5f;
    float dir[]={across.x,across.y,across.z};int removed=0;
    for(int i=-6;i<=6;++i){auto p=center+sweep*(i*.02f);float hit[]={p.x,p.y,p.z};goreShot_t r{};removed+=Gore_Shot(corpse,pose.data(),hit,dir,.03f,.05f,2.0f,&r);}
    int pcs[16],pb[16];float centers[16][3];int n=Gore_Sever(corpse,pcs,centers,pb,16);say("offline corpse burst removed "+std::to_string(removed)+" cells; separated "+std::to_string(n)+" components (compare baseline above)");
    require(n>0,"no separated component; inspect proof before calling severing done");
    d=draw(corpse,pose);skinObj("stump_skin.obj",wm,sk.rest,d);fleshObj("stump_flesh.obj",d);stats(d);
    for(int i=0;i<n;++i){auto pd=draw(pcs[i],pose);V3 off{0,.65f+float(i)*.25f,0};skinObj("piece"+std::to_string(i)+"_skin.obj",wm,sk.rest,pd,off);fleshObj("piece"+std::to_string(i)+"_flesh.obj",pd,off);say("piece "+std::to_string(i)+" bone "+sk.names[pb[i]]);}
    say("OFFLINE PROOF PASSED; engine parity, heads/helmets, materials and DX8 integration remain unverified");Gore_Shutdown();return 0;
}
}
int main(int argc,char** argv) { try { return proof::run(argc,argv); }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;} }
