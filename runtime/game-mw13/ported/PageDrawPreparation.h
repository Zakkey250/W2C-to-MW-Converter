#pragma once
#include <xmmintrin.h>

// Built after addon edits on the page reader. Lanes use the original submission order.
struct PageBoundsPacket {
 float low[3][4]{},high[3][4]{};
};
struct PageDrawPreparation {
 std::array<std::vector<PageBoundsPacket>,2> bounds;
 std::vector<std::pair<unsigned,unsigned>> spans;
 std::vector<Mesh> meshes;
};

static std::uint64_t PacketBytes(size_t opaque,size_t blended){
 return ((opaque+3)/4+(blended+3)/4)*std::uint64_t(sizeof(PageBoundsPacket));
}

static void PreparePageDraws(PageDrawPreparation& output,const std::vector<PageDraw>& draws,
 const std::vector<PageSpan>& spans,const std::array<std::vector<unsigned>,2>& orders){
 output.spans.resize(draws.size());
 for(size_t i=0;i<spans.size();++i){
  auto& range=output.spans.at(spans[i].source);
  if(!range.second)range.first=static_cast<unsigned>(i);
  ++range.second;
 }
 output.meshes.reserve(draws.size());
 for(const auto& d:draws)
  output.meshes.push_back({d.texture,d.oldFirstVertex,d.oldVertexCount,d.oldFirstIndex,d.indexCount,d.minimum,d.maximum});
 for(unsigned pass=0;pass<2;++pass){
  const auto& order=orders[pass];
  auto& packets=output.bounds[pass];
  packets.resize((order.size()+3)/4);
  for(size_t i=0;i<order.size();++i){
   const auto& d=draws.at(order[i]);
   auto& packet=packets[i/4];const unsigned lane=i%4;
   packet.low[0][lane]=d.minimum.x;packet.low[1][lane]=d.minimum.y;packet.low[2][lane]=d.minimum.z;
   packet.high[0][lane]=d.maximum.x;packet.high[1][lane]=d.maximum.y;packet.high[2][lane]=d.maximum.z;
  }
 }
}

struct PageFrustumPackets {
 struct Plane {__m128 x,y,z,w;bool highX,highY,highZ;};
 std::array<Plane,6> planes;
 explicit PageFrustumPackets(const float (&matrix)[6][4]){
  for(unsigned i=0;i<6;++i){const auto& p=matrix[i];
   planes[i]={_mm_set1_ps(p[0]),_mm_set1_ps(p[1]),_mm_set1_ps(p[2]),_mm_set1_ps(p[3]),p[0]>=0,p[1]>=0,p[2]>=0};
  }
 }
 unsigned visible(const PageBoundsPacket& box) const {
  __m128 rejected=_mm_setzero_ps();
  for(const auto& p:planes){
   const __m128 x=_mm_loadu_ps(p.highX?box.high[0]:box.low[0]);
   const __m128 y=_mm_loadu_ps(p.highY?box.high[1]:box.low[1]);
   const __m128 z=_mm_loadu_ps(p.highZ?box.high[2]:box.low[2]);
   const __m128 xy=_mm_add_ps(_mm_mul_ps(p.x,x),_mm_mul_ps(p.y,y));
   const __m128 dot=_mm_add_ps(_mm_add_ps(xy,_mm_mul_ps(p.z,z)),p.w);
   rejected=_mm_or_ps(rejected,_mm_cmplt_ps(dot,_mm_setzero_ps()));
   if(_mm_movemask_ps(rejected)==15)return 0;
  }
  return unsigned(_mm_movemask_ps(rejected))^15u;
 }
};
