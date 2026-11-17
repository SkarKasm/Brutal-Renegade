// Standalone W3D subset reader, GPL-3.0-or-later. No engine headers or libraries.
// Matches current ww3d2 file loading and base skinning; animated files are not evaluated.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../../voxel_gore_handoff/core/gorecore.h"

namespace w3d {
using gore::V3;
inline void require(bool b, const std::string& s) { if (!b) throw std::runtime_error(s); }
struct Bytes {
    std::vector<uint8_t> data;
    explicit Bytes(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        require(bool(f), "cannot open " + path);
        data.assign(std::istreambuf_iterator<char>(f), {});
    }
    void range(size_t p, size_t n) const { require(p <= data.size() && n <= data.size()-p, "file range outside input"); }
    uint32_t u32(size_t p) const { range(p,4); return uint32_t(data[p]) | uint32_t(data[p+1])<<8 | uint32_t(data[p+2])<<16 | uint32_t(data[p+3])<<24; }
    uint16_t u16(size_t p) const { range(p,2); return uint16_t(data[p]) | uint16_t(data[p+1])<<8; }
    float f32(size_t p) const { auto u=u32(p); float f; std::memcpy(&f,&u,4); require(std::isfinite(f),"nonfinite float"); return f; }
    V3 v3(size_t p) const { return {f32(p),f32(p+4),f32(p+8)}; }
    std::string str(size_t p,size_t n) const { range(p,n);size_t i=0;while(i<n && data[p+i])++i;return std::string(reinterpret_cast<const char*>(data.data()+p),i); }
};
struct Chunk { uint32_t id; size_t p,n; };
inline std::vector<Chunk> chunks(const Bytes& b,size_t p,size_t n) {
    b.range(p,n);size_t end=p+n;std::vector<Chunk> out;
    while(p<end) {
        require(end-p>=8,"truncated chunk header");auto id=b.u32(p);size_t len=b.u32(p+4)&0x7fffffff;
        require(len<=end-p-8,"chunk exceeds parent");out.push_back({id,p+8,len});p+=8+len;
    }
    return out;
}
inline Chunk one(const std::vector<Chunk>& cs,uint32_t id) {
    Chunk out{};int count=0;for(auto c:cs)if(c.id==id){out=c;++count;}
    require(count==1,"missing/duplicate required chunk " + std::to_string(id));return out;
}
struct TM {
    float m[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    V3 point(V3 p) const { return {m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+m[0][3],m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+m[1][3],m[2][0]*p.x+m[2][1]*p.y+m[2][2]*p.z+m[2][3]}; }
    V3 dir(V3 p) const { TM r=*this;r.m[0][3]=r.m[1][3]=r.m[2][3]=0;return r.point(p); }
    V3 origin() const { return {m[0][3],m[1][3],m[2][3]}; }
};
inline TM mul(const TM& a,const TM& b) {
    TM r;
    for(int i=0;i<3;++i) {
        for(int j=0;j<3;++j)r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j];
        r.m[i][3]=a.m[i][0]*b.m[0][3]+a.m[i][1]*b.m[1][3]+a.m[i][2]*b.m[2][3]+a.m[i][3];
    }
    return r;
}
// Literal expression order/types from wwmath/quat.cpp::Build_Matrix3D.
inline TM pivot(const Bytes& b,size_t p) {
    TM m;float q[4];for(int i=0;i<4;++i)q[i]=b.f32(p+44+i*4);
    m.m[0][0]=(float)(1.0-2.0*(q[1]*q[1]+q[2]*q[2]));
    m.m[0][1]=(float)(2.0*(q[0]*q[1]-q[2]*q[3]));
    m.m[0][2]=(float)(2.0*(q[2]*q[0]+q[1]*q[3]));
    m.m[1][0]=(float)(2.0*(q[0]*q[1]+q[2]*q[3]));
    m.m[1][1]=(float)(1.0-2.0f*(q[2]*q[2]+q[0]*q[0]));
    m.m[1][2]=(float)(2.0*(q[1]*q[2]-q[0]*q[3]));
    m.m[2][0]=(float)(2.0*(q[2]*q[0]-q[1]*q[3]));
    m.m[2][1]=(float)(2.0*(q[1]*q[2]+q[0]*q[3]));
    m.m[2][2]=(float)(1.0-2.0*(q[1]*q[1]+q[0]*q[0]));
    auto t=b.v3(p+20);m.m[0][3]=t.x;m.m[1][3]=t.y;m.m[2][3]=t.z;return m;
}
struct Skeleton { std::string name;std::vector<std::string> names;std::vector<int> parents;std::vector<TM> rest; };
inline Skeleton skeleton(const Bytes& b) {
    auto top=chunks(b,0,b.data.size());auto h=one(top,0x100);auto cs=chunks(b,h.p,h.n);auto hd=one(cs,0x101);auto pv=one(cs,0x102);
    require(hd.n==36,"hierarchy header size");require(b.u32(hd.p)>=0x30000,"pre-3.0 hierarchy unsupported");
    Skeleton s;s.name=b.str(hd.p+4,16);auto n=b.u32(hd.p+20);
    require(n>1 && n<=128 && pv.n==size_t(n)*60,"hierarchy count/size or wrapper bone limit");
    for(uint32_t i=0;i<n;++i) {
        size_t p=pv.p+i*60;auto parent=b.u32(p+16);
        require(i==0 ? parent==0xffffffffu : parent<i,"hierarchy not parent-first");
        s.names.push_back(b.str(p,16));s.parents.push_back(i==0?-1:int(parent));
        s.rest.push_back(i==0 ? TM{} : mul(s.rest[parent],pivot(b,p)));
    }
    return s;
}
struct Surface {
    std::string name;uint32_t attributes=0;
    std::vector<V3> local,normal,rest,restNormal;
    std::vector<uint16_t> bone;std::vector<uint32_t> idx;
};
struct Model { std::string hierarchy;std::vector<Surface> surfaces;std::vector<std::string> excluded; };
inline Model mesh(const Bytes& b,const Skeleton& sk,bool highest_file_lod=false) {
    auto top=chunks(b,0,b.data.size());auto hc=one(top,0x700);auto hs=chunks(b,hc.p,hc.n);auto hh=one(hs,0x701);
    require(hh.n==40,"HLOD header size");require(b.u32(hh.p+4)>=1 && b.u32(hh.p+4)<=16 && (highest_file_lod || b.u32(hh.p+4)==1),"unsupported file LOD count");
    Model model;model.hierarchy=b.str(hh.p+24,16);require(model.hierarchy==sk.name,"HLOD/skeleton name mismatch");
    std::vector<Chunk> lods;for(auto c:hs)if(c.id==0x702)lods.push_back(c);require(lods.size()==b.u32(hh.p+4),"LOD array count");auto la=lods.front();auto lc=chunks(b,la.p,la.n);auto lh=one(lc,0x703);require(lh.n==8,"LOD array header size");
    std::map<std::string,uint32_t> bindings;
    for(auto c:lc)if(c.id==0x704){require(c.n==36,"subobject size");require(bindings.emplace(b.str(c.p+4,32),b.u32(c.p)).second,"duplicate subobject");}
    require(bindings.size()==b.u32(lh.p),"subobject count mismatch");
    for(auto c:top)if(c.id==0) {
        auto cs=chunks(b,c.p,c.n);auto hd=one(cs,0x1f);require(hd.n==116,"mesh header size");
        require(b.u32(hd.p)>=0x40001,"old mesh attributes unsupported");
        Surface s;s.attributes=b.u32(hd.p+4);s.name=b.str(hd.p+24,16)+"."+b.str(hd.p+8,16);
        if(highest_file_lod && !bindings.count(s.name)){model.excluded.push_back(s.name);continue;}
        require(bindings.count(s.name)!=0,"mesh not in HLOD");
        // HIDDEN=0x1000; exclude shadow-named meshes and non-skins in this first proof.
        if((s.attributes&0x1000) || s.name.find("SHADOW")!=std::string::npos || (s.attributes&0xff0000)!=0x20000) {
            model.excluded.push_back(s.name);continue;
        }
        auto nv=b.u32(hd.p+44),nt=b.u32(hd.p+40);require(nv>0 && nv<=65535 && nt>0 && nt<1000000,"mesh counts");
        auto vp=one(cs,2),np=one(cs,3),ip=one(cs,0xe),tp=one(cs,0x20);
        require(vp.n==size_t(nv)*12 && np.n==size_t(nv)*12 && ip.n==size_t(nv)*8 && tp.n==size_t(nt)*32,"geometry chunk sizes");
        for(uint32_t i=0;i<nv;++i) {
            auto bone=b.u16(ip.p+i*8);require(bone<sk.rest.size(),"bone index out of range");
            auto p=b.v3(vp.p+i*12),n=b.v3(np.p+i*12);s.local.push_back(p);s.normal.push_back(n);s.bone.push_back(bone);
            s.rest.push_back(sk.rest[bone].point(p));s.restNormal.push_back(sk.rest[bone].dir(n));
        }
        for(uint32_t t=0;t<nt;++t)for(int j=0;j<3;++j){auto ix=b.u32(tp.p+t*32+j*4);require(ix<nv,"triangle index out of range");s.idx.push_back(ix);}
        model.surfaces.push_back(std::move(s));
    }
    require(!model.surfaces.empty(),"no visible skinned surfaces");return model;
}
} // namespace w3d
