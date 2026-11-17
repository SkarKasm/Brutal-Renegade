#include "voxel_draw.h"
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <limits>
#include <algorithm>
#include <stdexcept>
static_assert(sizeof(BRVG_RenderVertex)==44,"DX8 XYZNDUV2 vertex layout");
struct BRVG_RenderPacket {BRVG_PacketView view{};std::vector<BRVG_RenderVertex> vertices;std::vector<uint16_t> indices;std::vector<BRVG_DrawRange> ranges;std::vector<uint32_t> sourceTriangles;};
namespace {
struct DrawBucket {
 std::vector<uint16_t> indices;
 std::vector<uint32_t> sourceTriangles;
};
void demand(bool b){if(!b)throw std::runtime_error("invalid or oversized wound packet");}
void append(std::vector<uint16_t>& v,uint32_t a,uint32_t b,uint32_t c){demand(a<65535&&b<65535&&c<65535);v.push_back(uint16_t(a));v.push_back(uint16_t(b));v.push_back(uint16_t(c));}
BRVG_RenderVertex blend(const BRVG_RenderVertex (&v)[3],const float* w) {
 BRVG_RenderVertex out{};float sum=0;
 for(int j=0;j<3;++j){demand(std::isfinite(w[j]) && w[j]>=-1e-5f && w[j]<=1.00001f);sum+=w[j];for(int i=0;i<3;++i){out.xyz[i]+=v[j].xyz[i]*w[j];out.normal[i]+=v[j].normal[i]*w[j];}for(int i=0;i<2;++i){out.uv0[i]+=v[j].uv0[i]*w[j];out.uv1[i]+=v[j].uv1[i]*w[j];}}
 demand(std::abs(sum-1)<1e-4f);float len=std::sqrt(out.normal[0]*out.normal[0]+out.normal[1]*out.normal[1]+out.normal[2]*out.normal[2]);if(len>1e-12f)for(float& n:out.normal)n/=len;
 for(int c=0;c<4;++c){float color=0;for(int j=0;j<3;++j)color+=float((v[j].diffuse>>(c*8))&255)*w[j];out.diffuse|=uint32_t((std::max)(0.f,(std::min)(255.f,std::round(color))))<<(c*8);}
 return out;
}
}
extern "C" BRVG_RenderPacket* BRVG_BuildPacket(const BRVG_SurfaceView* cut,uint64_t identity,uint32_t nv,uint32_t nt,const uint32_t* source,const BRVG_RenderVertex* posed,const uint32_t* groups) {
 try {
  demand(cut && cut->identity==identity && cut->vertex_count==nv && cut->triangle_count==nt && source && posed && groups && nv>0 && nt>0 && nt<1000000);
  demand(cut->keep_count>=-1 && (cut->keep_count<0 || uint32_t(cut->keep_count)<=nt) && (cut->keep_count<=0 || cut->keep) && (!cut->sub_count || cut->sub));
  uint64_t count=uint64_t(nv)+uint64_t(cut->sub_count)*3;demand(count<65535);uint64_t triCount=(cut->keep_count<0?nt:uint32_t(cut->keep_count))+uint64_t(cut->sub_count);demand(triCount*3<500000);
  for(size_t i=0;i<size_t(nt)*3;++i)demand(source[i]<nv);
  for(uint32_t i=0;i<nv;++i){for(float x:posed[i].xyz)demand(std::isfinite(x));for(float x:posed[i].normal)demand(std::isfinite(x));for(float x:posed[i].uv0)demand(std::isfinite(x));for(float x:posed[i].uv1)demand(std::isfinite(x));}
  auto p=std::make_unique<BRVG_RenderPacket>();p->vertices.reserve(size_t(count));p->vertices.assign(posed,posed+nv);std::map<uint32_t,DrawBucket> buckets;
  auto addTriangle=[&](uint32_t t,uint32_t a,uint32_t b,uint32_t c){
   auto& bucket=buckets[groups[t]];
   append(bucket.indices,a,b,c);
   bucket.sourceTriangles.push_back(t);
  };
  auto whole=[&](uint32_t t){demand(t<nt);addTriangle(t,source[t*3],source[t*3+1],source[t*3+2]);};
  if(cut->keep_count<0)for(uint32_t t=0;t<nt;++t)whole(t);else for(int32_t i=0;i<cut->keep_count;++i){demand(cut->keep[i]>=0);whole(uint32_t(cut->keep[i]));}
  for(uint32_t i=0;i<cut->sub_count;++i){const float* sub=cut->sub+size_t(i)*10;demand(std::isfinite(sub[0]) && sub[0]>=0 && sub[0]<nt && std::floor(sub[0])==sub[0]);uint32_t t=uint32_t(sub[0]);BRVG_RenderVertex corners[3]={posed[source[t*3]],posed[source[t*3+1]],posed[source[t*3+2]]};uint32_t base=uint32_t(p->vertices.size());for(int j=0;j<3;++j)p->vertices.push_back(blend(corners,sub+1+j*3));addTriangle(t,base,base+1,base+2);}
  p->indices.reserve(size_t(triCount)*3);
  p->sourceTriangles.reserve(size_t(triCount));
  for(auto& b:buckets){
   const auto& bucket=b.second;
   p->ranges.push_back({b.first,uint32_t(p->indices.size()),uint32_t(bucket.indices.size())});
   p->indices.insert(p->indices.end(),bucket.indices.begin(),bucket.indices.end());
   p->sourceTriangles.insert(p->sourceTriangles.end(),bucket.sourceTriangles.begin(),bucket.sourceTriangles.end());
  }
  p->view.vertex_count=uint32_t(p->vertices.size());p->view.index_count=uint32_t(p->indices.size());p->view.range_count=uint32_t(p->ranges.size());p->view.vertices=p->vertices.data();p->view.indices=p->indices.data();p->view.ranges=p->ranges.data();p->view.source_triangles=p->sourceTriangles.data();return p.release();
 }catch(...){return nullptr;}
}
extern "C" const BRVG_PacketView* BRVG_GetPacket(const BRVG_RenderPacket* packet){return packet?&packet->view:nullptr;}
extern "C" void BRVG_FreePacket(BRVG_RenderPacket* packet){delete packet;}
