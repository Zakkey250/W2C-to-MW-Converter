#pragma once
#include "PackFormat.h"
#include <vector>
#include <array>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>
#include <span>
namespace w2mw {
struct CollisionTriangle {Vec3 p[3];uint32_t surface,wall;};
struct BvhNode {Vec3 minimum,maximum;uint32_t left,right;};
static_assert(sizeof(CollisionTriangle)==44 && sizeof(BvhNode)==32);
struct RayHit {unsigned triangle=~0u;float fraction=1;Vec3 point{},normal{};};
inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
class CollisionWorld {
 struct RayKey {std::array<unsigned,7> bits;bool operator==(const RayKey&)const=default;};
 struct RayHash {size_t operator()(const RayKey& key)const{unsigned hash=2166136261u;for(auto bits:key.bits)hash=(hash^bits)*16777619u;hash^=hash>>16;hash*=0x7FEB352Du;hash^=hash>>15;hash*=0x846CA68Bu;return hash^(hash>>16);}};
 struct CachedRay {RayKey key{};unsigned generation=0;bool found=false;RayHit hit;};
 mutable std::vector<CachedRay> rayCache;
 mutable unsigned rayGeneration=0;
 mutable bool rayCacheActive=false;
 struct WallBoxCache {std::array<unsigned,6> key{};unsigned generation=0;bool unfiltered=false;std::vector<unsigned> candidates;};
 mutable std::vector<WallBoxCache> wallBoxes;
 struct FloorLeaf {unsigned index;Vec3 minimum,maximum;};
 struct FloorColumn {unsigned x=0,z=0,used=0;std::vector<FloorLeaf> leaves;};
 mutable std::vector<FloorColumn> floorColumns;
 std::vector<unsigned char> nodeKinds;
 struct QueryBounds {Vec3 minimum,maximum;};
 std::vector<QueryBounds> floorBounds,wallBounds;
 std::vector<QueryBounds> triangleBounds;
 struct TriangleGeometry {Vec3 edge1,edge2,normal;};
 std::vector<TriangleGeometry> triangleGeometry;
 std::vector<unsigned> triangleLeaf;
 Vec3 wallRayPadding{.001f,.001f,.001f};
public:
 // Optional native scenery gate. A query scope freezes its activation
 // snapshot; changing it between scopes cannot reuse a previous ray cache.
 bool (*triangleEnabled)(const void*,unsigned)=nullptr;
 const void* triangleEnabledContext=nullptr;
 bool Enabled(unsigned index)const{return !triangleEnabled||triangleEnabled(triangleEnabledContext,index);}
 struct ProfileCounters {unsigned lookups=0,hits=0,fallbacks=0,probes=0,rays=0,triangles=0,boxes=0,candidateRays=0,boxCacheHits=0;};
 mutable ProfileCounters profile{};
 bool profileEnabled=false;
 unsigned QueryGeneration()const{return rayCacheActive?rayGeneration:0;}
 unsigned BeginQuery()const{bool previous=rayCacheActive;if(!previous){if(rayCache.empty())rayCache.resize(32768);if(++rayGeneration==0){for(auto& slot:rayCache)slot.generation=0;floorColumns.clear();wallBoxes.clear();++rayGeneration;}rayCacheActive=true;}return unsigned(previous);}
 void EndQuery(unsigned previous)const{rayCacheActive=previous!=0;}
 // Exact-bit memoization of immutable geometry, scoped to one AI query.
 // No pose quantization and no cross-query reuse of traffic/event state.
 struct RayQueryScope {
  const CollisionWorld& world;bool previous;
  explicit RayQueryScope(const CollisionWorld& value):world(value),previous(value.BeginQuery()!=0){}
  ~RayQueryScope(){world.EndQuery(unsigned(previous));}
 };
 std::vector<std::array<char,80>> surfaces;
 std::vector<BvhNode> nodes;
 std::vector<CollisionTriangle> triangles;
 void QueryBox(Vec3 lo,Vec3 hi,std::vector<unsigned>& found,bool wallsOnly)const{
  if(profileEnabled)++profile.boxes;
  if(!wallsOnly||!rayCacheActive||nodes.empty()){QueryBoxUncached(lo,hi,found,wallsOnly);return;}
  Vec3 coarseLo{},coarseHi{};for(unsigned axis=0;axis<3;++axis){float a=(&lo.x)[axis],b=(&hi.x)[axis];if(!std::isfinite(a)||!std::isfinite(b)){QueryBoxUncached(lo,hi,found,wallsOnly);return;}(&coarseLo.x)[axis]=std::floor(a/8)*8;(&coarseHi.x)[axis]=std::ceil(b/8)*8;}
  RayKey key{};memcpy(key.bits.data(),&coarseLo,12);memcpy(key.bits.data()+3,&coarseHi,12);
  if(wallBoxes.empty())wallBoxes.resize(512);unsigned bucket=unsigned(RayHash{}(key))&508;auto* entry=&wallBoxes[bucket];bool matched=false;
  // W2C owns immutable triangles and has no activation filter. Its coarse
  // static-wall candidates remain exact across queries; traffic and event
  // walls are checked separately by the driver and are never cached here.
  // Native scenery with an activation callback still uses the query epoch.
  const bool immutable=triangleEnabled==nullptr;
  for(unsigned way=0;way<4;++way){auto& candidate=wallBoxes[bucket+way];if(candidate.generation&&candidate.unfiltered==immutable&&(immutable||candidate.generation==rayGeneration)&&memcmp(candidate.key.data(),key.bits.data(),24)==0){entry=&candidate;matched=true;break;}if(candidate.generation<entry->generation)entry=&candidate;}
  if(!matched){memcpy(entry->key.data(),key.bits.data(),24);entry->unfiltered=immutable;QueryBoxUncached(coarseLo,coarseHi,entry->candidates,true);}else if(profileEnabled)++profile.boxCacheHits;
  entry->generation=rayGeneration;
  found.clear();for(auto index:entry->candidates)if(TriangleOverlaps(index,lo,hi))found.push_back(index);
 }
 void QueryBoxUncached(Vec3 lo,Vec3 hi,std::vector<unsigned>& found,bool wallsOnly)const{
  found.clear();if(nodes.empty())return;unsigned stack[128],used=1;stack[0]=0;
  while(used){unsigned index=stack[--used];const auto& n=nodes[index];
   if(wallsOnly&&nodeKinds.size()==nodes.size()&&!(nodeKinds[index]&2))continue;
   Vec3 minimum=wallsOnly&&wallBounds.size()==nodes.size()?wallBounds[index].minimum:n.minimum,maximum=wallsOnly&&wallBounds.size()==nodes.size()?wallBounds[index].maximum:n.maximum;
   if(maximum.x<lo.x||minimum.x>hi.x||maximum.y<lo.y||minimum.y>hi.y||maximum.z<lo.z||minimum.z>hi.z)continue;
   if(!(n.right&0x80000000)){if(used+2>128)return;stack[used++]=n.left;stack[used++]=n.right;continue;}
   for(unsigned i=n.left;i<n.left+(n.right&0x7fffffff);++i){const auto& t=triangles[i];if(!Enabled(i)||(wallsOnly&&!t.wall))continue;
    // A leaf overlaps as a group; unrelated triangles in that leaf need not
    // enter every body-corner sweep. Inclusive bounds retain edge contacts.
    const auto& box=triangleBounds[i];
    if(box.maximum.x<lo.x||box.minimum.x>hi.x||box.maximum.y<lo.y||box.minimum.y>hi.y||box.maximum.z<lo.z||box.minimum.z>hi.z)continue;
    found.push_back(i);
   }
  }
 }
 bool LoadBytes(const std::vector<unsigned char>& bytes,unsigned expectedTriangles=987078){for(auto& slot:rayCache)slot.generation=0;floorColumns.clear();wallBoxes.clear();if(bytes.size()<20)return false;unsigned h[5];memcpy(h,bytes.data(),20);bool ok=h[0]==0x31434D57&&h[1]==1&&h[2]>0&&h[2]<=4000000&&(!expectedTriangles||h[2]==expectedTriangles)&&h[3]>0&&h[3]<1000000&&h[4]>0&&h[4]<1000;
 if(!ok||20ull+h[4]*80ull+h[3]*sizeof(BvhNode)+h[2]*sizeof(CollisionTriangle)!=bytes.size())return false;surfaces.resize(h[4]);nodes.resize(h[3]);triangles.resize(h[2]);auto at=bytes.data()+20;memcpy(surfaces.data(),at,h[4]*80);at+=h[4]*80;memcpy(nodes.data(),at,h[3]*sizeof(BvhNode));at+=h[3]*sizeof(BvhNode);memcpy(triangles.data(),at,h[2]*sizeof(CollisionTriangle));
  for(auto& s:surfaces)if(s[79])return false;
  for(auto& n:nodes){if(n.right&0x80000000){if(uint64_t(n.left)+(n.right&0x7fffffff)>triangles.size())return false;}else if(n.left>=nodes.size()||n.right>=nodes.size())return false;}
  for(auto& t:triangles)if(t.surface>=surfaces.size()||t.wall>1)return false;
  return PrepareQueries();
 }
 // Rebuild immutable query data after constructing an offline fixture.
 // Runtime Load validates the packed world before entering this same path.
 bool PrepareQueries(){
  for(auto& n:nodes){if(n.right&0x80000000){if(uint64_t(n.left)+(n.right&0x7fffffff)>triangles.size())return false;}else if(n.left>=nodes.size()||n.right>=nodes.size())return false;}
  for(auto& slot:rayCache)slot.generation=0;floorColumns.clear();wallBoxes.clear();
  triangleBounds.resize(triangles.size());for(unsigned i=0;i<triangles.size();++i){const auto& t=triangles[i];triangleBounds[i]={{std::min({t.p[0].x,t.p[1].x,t.p[2].x}),std::min({t.p[0].y,t.p[1].y,t.p[2].y}),std::min({t.p[0].z,t.p[1].z,t.p[2].z})},{std::max({t.p[0].x,t.p[1].x,t.p[2].x}),std::max({t.p[0].y,t.p[1].y,t.p[2].y}),std::max({t.p[0].z,t.p[1].z,t.p[2].z})}};}
  nodeKinds.assign(nodes.size(),0);std::vector<unsigned char> visit(nodes.size(),0);
  triangleGeometry.resize(triangles.size());for(unsigned i=0;i<triangles.size();++i){const auto& t=triangles[i];auto& g=triangleGeometry[i];g.edge1=Sub(t.p[1],t.p[0]);g.edge2=Sub(t.p[2],t.p[0]);auto normal=Cross(g.edge1,g.edge2);float length=std::sqrt(Dot(normal,normal));g.normal=length<1e-8f?Vec3{}:Vec3{normal.x/length,normal.y/length,normal.z/length};}
  const float infinity=std::numeric_limits<float>::infinity();QueryBounds empty{{infinity,infinity,infinity},{-infinity,-infinity,-infinity}};
  floorBounds.assign(nodes.size(),empty);wallBounds.assign(nodes.size(),empty);triangleLeaf.resize(triangles.size());wallRayPadding={.001f,.001f,.001f};
  auto extend=[](QueryBounds& box,Vec3 low,Vec3 high){box.minimum={std::min(box.minimum.x,low.x),std::min(box.minimum.y,low.y),std::min(box.minimum.z,low.z)};box.maximum={std::max(box.maximum.x,high.x),std::max(box.maximum.y,high.y),std::max(box.maximum.z,high.z)};};
  auto classify=[&](auto&& self,unsigned index,unsigned depth)->bool{
   if(depth>=128||visit[index]==1)return false;if(visit[index]==2)return true;visit[index]=1;
   const auto& n=nodes[index];unsigned char kind=0;
   if(n.right&0x80000000){for(unsigned i=n.left;i<n.left+(n.right&0x7fffffff);++i){const auto& t=triangles[i];triangleLeaf[i]=index;if(t.wall)for(unsigned axis=0;axis<3;++axis){float edge=std::abs((&t.p[1].x)[axis]-(&t.p[0].x)[axis])+std::abs((&t.p[2].x)[axis]-(&t.p[0].x)[axis]);(&wallRayPadding.x)[axis]=std::max((&wallRayPadding.x)[axis],.001f+2e-5f*edge);}
    kind|=t.wall?2:1;auto& box=t.wall?wallBounds[index]:floorBounds[index];for(auto p:t.p)extend(box,p,p);}
    // Ray's barycentric acceptance includes a small edge tolerance. Keep
    // the filtered bounds conservative for those accepted near-edge rays.
    for(auto box:{&floorBounds[index],&wallBounds[index]})if(std::isfinite(box->minimum.x))for(unsigned axis=0;axis<3;++axis){float pad=.001f+4e-5f*((&box->maximum.x)[axis]-(&box->minimum.x)[axis]);(&box->minimum.x)[axis]-=pad;(&box->maximum.x)[axis]+=pad;}
   }
   else{if(!self(self,n.left,depth+1)||!self(self,n.right,depth+1))return false;kind=nodeKinds[n.left]|nodeKinds[n.right];
    for(auto child:{n.left,n.right}){if(nodeKinds[child]&1)extend(floorBounds[index],floorBounds[child].minimum,floorBounds[child].maximum);if(nodeKinds[child]&2)extend(wallBounds[index],wallBounds[child].minimum,wallBounds[child].maximum);}
   }
   nodeKinds[index]=kind;visit[index]=2;return true;
  };
  return !nodes.empty()&&classify(classify,0,0);
 }
 bool Ray(Vec3 from,Vec3 to,RayHit& hit,bool floorsOnly=false,bool wallsOnly=false)const{
  if(!rayCacheActive)return RayUncached(from,to,hit,floorsOnly,wallsOnly);
  if(profileEnabled)++profile.lookups;
  RayKey key{};memcpy(key.bits.data(),&from,12);memcpy(key.bits.data()+3,&to,12);key.bits[6]=unsigned(floorsOnly)|(unsigned(wallsOnly)<<1);
  unsigned index=unsigned(RayHash{}(key))&32767;
  for(unsigned probe=0;probe<64;++probe,index=(index+1)&32767){auto& cached=rayCache[index];
   if(profileEnabled)++profile.probes;
   if(cached.generation!=rayGeneration){bool found=RayUncached(from,to,hit,floorsOnly,wallsOnly);cached={key,rayGeneration,found,hit};return found;}
   if(cached.key==key){if(profileEnabled)++profile.hits;if(cached.found)hit=cached.hit;else{hit.fraction=1;hit.triangle=~0u;}return cached.found;}
  }
  if(profileEnabled)++profile.fallbacks;return RayUncached(from,to,hit,floorsOnly,wallsOnly);
 }
 Vec3 WallRayPadding()const{return wallRayPadding;}
 bool TriangleOverlaps(unsigned index,Vec3 low,Vec3 high)const{if(!Enabled(index))return false;const auto& box=triangleBounds[index];return !(box.maximum.x<low.x||box.minimum.x>high.x||box.maximum.y<low.y||box.minimum.y>high.y||box.maximum.z<low.z||box.minimum.z>high.z);}
 void IntersectTriangle(unsigned index,Vec3 from,Vec3 direction,RayHit& hit)const{
  if(!Enabled(index))return;
  if(profileEnabled)++profile.triangles;
  const auto& t=triangles[index];const auto& geometry=triangleGeometry[index];Vec3 e1=geometry.edge1,e2=geometry.edge2,p=Cross(direction,e2);float determinant=Dot(e1,p);if(std::abs(determinant)<1e-8f)return;float inv=1/determinant;Vec3 s=Sub(from,t.p[0]);float u=Dot(s,p)*inv;if(u<-.00001f||u>1.00001f)return;Vec3 q=Cross(s,e1);float v=Dot(direction,q)*inv;if(v<-.00001f||u+v>1.00001f)return;float fraction=Dot(e2,q)*inv;if(fraction<0||fraction>=hit.fraction)return;Vec3 normal=geometry.normal;if(normal.x==0&&normal.y==0&&normal.z==0)return;if(Dot(normal,direction)>0)normal={-normal.x,-normal.y,-normal.z};hit={index,fraction,{from.x+direction.x*fraction,from.y+direction.y*fraction,from.z+direction.z*fraction},normal};
 }
 bool RayCandidates(Vec3 from,Vec3 to,RayHit& hit,const std::vector<unsigned>& candidates,bool completeWallBroadPhase=false)const{
  return RayCandidatesSpan(from,to,hit,candidates,completeWallBroadPhase);
 }
 bool RayCandidatesSpan(Vec3 from,Vec3 to,RayHit& hit,std::span<const unsigned> candidates,bool completeWallBroadPhase=false)const{
  if(profileEnabled)++profile.candidateRays;
  // Callers opting in must cover the full ray AABB plus WallRayPadding.
  // Then the result is the same immutable wall query as Ray(...,false,true).
  if(completeWallBroadPhase&&rayCacheActive){if(profileEnabled)++profile.lookups;RayKey key{};memcpy(key.bits.data(),&from,12);memcpy(key.bits.data()+3,&to,12);key.bits[6]=2;unsigned slot=unsigned(RayHash{}(key))&32767;
   for(unsigned probe=0;probe<64;++probe,slot=(slot+1)&32767){if(profileEnabled)++profile.probes;auto& cached=rayCache[slot];if(cached.generation!=rayGeneration){bool found=RayCandidatesSpan(from,to,hit,candidates);cached={key,rayGeneration,found,hit};return found;}if(cached.key==key){if(profileEnabled)++profile.hits;if(cached.found)hit=cached.hit;else{hit.fraction=1;hit.triangle=~0u;}return cached.found;}}if(profileEnabled)++profile.fallbacks;
  }
  hit.fraction=1;hit.triangle=~0u;Vec3 direction=Sub(to,from);unsigned previous=~0u;bool overlaps=false;
  const Vec3 low{std::min(from.x,to.x)-wallRayPadding.x,std::min(from.y,to.y)-wallRayPadding.y,std::min(from.z,to.z)-wallRayPadding.z},high{std::max(from.x,to.x)+wallRayPadding.x,std::max(from.y,to.y)+wallRayPadding.y,std::max(from.z,to.z)+wallRayPadding.z};
  for(auto index:candidates){if(index>=triangles.size()||!triangles[index].wall)continue;unsigned leaf=triangleLeaf[index];
   const auto& box=triangleBounds[index];if(box.maximum.x<low.x||box.minimum.x>high.x||box.maximum.y<low.y||box.minimum.y>high.y||box.maximum.z<low.z||box.minimum.z>high.z)continue;
   if(leaf!=previous){previous=leaf;const auto& n=nodes[leaf];float nearT=0,farT=hit.fraction;overlaps=true;
    for(unsigned axis=0;axis<3;++axis){float origin=(&from.x)[axis],dir=(&direction.x)[axis],low=(&n.minimum.x)[axis],high=(&n.maximum.x)[axis];
     if(std::abs(dir)<1e-12f){if(origin<low||origin>high){overlaps=false;break;}}
     else{float a=(low-origin)/dir,b=(high-origin)/dir;if(a>b)std::swap(a,b);nearT=std::max(nearT,a);farT=std::min(farT,b);if(nearT>farT){overlaps=false;break;}}
    }
   }
   if(overlaps)IntersectTriangle(index,from,direction,hit);
  }
  return hit.triangle!=~0u;
 }
 bool FloorColumnRay(Vec3 from,Vec3 to,RayHit& hit)const{
  if(floorColumns.empty())floorColumns.resize(8192);
  // Cache an immutable 8m X/Z broad phase, not an approximate ray result.
  // Nearby wheels/lookahead samples share BVH traversal; each ray still
  // checks the exact ancestor-clipped leaf bounds and native triangle math.
  const float tileX=std::floor(from.x/8)*8,tileZ=std::floor(from.z/8)*8;
  unsigned x,z;memcpy(&x,&tileX,4);memcpy(&z,&tileZ,4);RayKey key{};key.bits[0]=x;key.bits[1]=z;
  unsigned bucket=unsigned(RayHash{}(key))&8188;auto* column=&floorColumns[bucket];bool found=false;
  for(unsigned way=0;way<4;++way){auto& entry=floorColumns[bucket+way];if(entry.used&&entry.x==x&&entry.z==z){column=&entry;found=true;break;}if(entry.used<column->used)column=&entry;}
  auto bounds=[&](unsigned index){auto result=floorBounds[index];const auto& n=nodes[index];for(unsigned axis=0;axis<3;++axis){(&result.minimum.x)[axis]=std::max((&result.minimum.x)[axis],(&n.minimum.x)[axis]);(&result.maximum.x)[axis]=std::min((&result.maximum.x)[axis],(&n.maximum.x)[axis]);}return result;};
  if(!found){column->x=x;column->z=z;column->leaves.clear();const float inf=std::numeric_limits<float>::infinity();FloorLeaf stack[128];unsigned used=1;stack[0]={0,{-inf,-inf,-inf},{inf,inf,inf}};
   while(used){auto entry=stack[--used];unsigned index=entry.index;if(!(nodeKinds[index]&1))continue;const auto box=bounds(index);
    for(unsigned axis=0;axis<3;++axis){(&entry.minimum.x)[axis]=std::max((&entry.minimum.x)[axis],(&box.minimum.x)[axis]);(&entry.maximum.x)[axis]=std::min((&entry.maximum.x)[axis],(&box.maximum.x)[axis]);}
    if(entry.minimum.x>entry.maximum.x||entry.minimum.z>entry.maximum.z||tileX>entry.maximum.x||tileX+8<entry.minimum.x||tileZ>entry.maximum.z||tileZ+8<entry.minimum.z)continue;
    const auto& n=nodes[index];if(n.right&0x80000000)column->leaves.push_back(entry);else{if(used+2>128)return false;entry.index=n.left;stack[used++]=entry;entry.index=n.right;stack[used++]=entry;}
   }
  }column->used=rayGeneration;hit.fraction=1;hit.triangle=~0u;const auto direction=Sub(to,from);
  // Only the X/Z broad phase is reused. Exact height, fraction, source leaf
  // order and triangle math are evaluated for every ray.
  for(const auto& leaf:column->leaves){float nearT=0,farT=hit.fraction;
   if(from.x<leaf.minimum.x||from.x>leaf.maximum.x||from.z<leaf.minimum.z||from.z>leaf.maximum.z)continue;
   if(std::abs(direction.y)<1e-12f){if(from.y<leaf.minimum.y||from.y>leaf.maximum.y)continue;}
   else{float a=(leaf.minimum.y-from.y)/direction.y,b=(leaf.maximum.y-from.y)/direction.y;if(a>b)std::swap(a,b);nearT=std::max(nearT,a);farT=std::min(farT,b);if(nearT>farT)continue;}
   const auto& n=nodes[leaf.index];for(unsigned i=n.left;i<n.left+(n.right&0x7fffffff);++i)if(!triangles[i].wall){
    // A BVH leaf can contain several distant road triangles. Reject only
    // X/Z bounds that cannot contain this vertical ray, including the same
    // barycentric edge tolerance accepted by IntersectTriangle. Keep exact
    // triangle math, activation checks, tie order and height selection.
    const auto& box=triangleBounds[i];const auto& g=triangleGeometry[i];
    const float px=.001f+2e-5f*(std::abs(g.edge1.x)+std::abs(g.edge2.x));
    const float pz=.001f+2e-5f*(std::abs(g.edge1.z)+std::abs(g.edge2.z));
    if(from.x<box.minimum.x-px||from.x>box.maximum.x+px||from.z<box.minimum.z-pz||from.z>box.maximum.z+pz)continue;
    IntersectTriangle(i,from,direction,hit);
   }
  }return hit.triangle!=~0u;
 }
 bool RayUncached(Vec3 from,Vec3 to,RayHit& hit,bool floorsOnly=false,bool wallsOnly=false)const{if(nodes.empty())return false;Vec3 direction=Sub(to,from);if(profileEnabled)++profile.rays;
  if(rayCacheActive&&floorsOnly&&!wallsOnly&&direction.x==0&&direction.z==0&&floorBounds.size()==nodes.size())return FloorColumnRay(from,to,hit);
  unsigned stack[128];unsigned used=1;stack[0]=0;hit.fraction=1;hit.triangle=~0u;
  const bool vertical=direction.x==0&&direction.z==0;
  while(used){unsigned nodeIndex=stack[--used];if(nodeKinds.size()==nodes.size()&&((floorsOnly&&!(nodeKinds[nodeIndex]&1))||(wallsOnly&&!(nodeKinds[nodeIndex]&2))))continue;
   auto& n=nodes[nodeIndex];Vec3 minimum=n.minimum,maximum=n.maximum;
   if(floorBounds.size()==nodes.size()){if(floorsOnly){minimum=floorBounds[nodeIndex].minimum;maximum=floorBounds[nodeIndex].maximum;}else if(wallsOnly){minimum=wallBounds[nodeIndex].minimum;maximum=wallBounds[nodeIndex].maximum;}
    for(unsigned axis=0;axis<3;++axis){(&minimum.x)[axis]=std::max((&minimum.x)[axis],(&n.minimum.x)[axis]);(&maximum.x)[axis]=std::min((&maximum.x)[axis],(&n.maximum.x)[axis]);}
   }
   float nearT=0,farT=hit.fraction;float origin[3]={from.x,from.y,from.z},dir[3]={direction.x,direction.y,direction.z},lo[3]={minimum.x,minimum.y,minimum.z},hi[3]={maximum.x,maximum.y,maximum.z};bool overlaps=true;
   if(vertical){if(from.x<minimum.x||from.x>maximum.x||from.z<minimum.z||from.z>maximum.z)continue;
    if(std::abs(direction.y)<1e-12f){if(from.y<minimum.y||from.y>maximum.y)continue;}
    else{float a=(minimum.y-from.y)/direction.y,b=(maximum.y-from.y)/direction.y;if(a>b)std::swap(a,b);nearT=std::max(nearT,a);farT=std::min(farT,b);if(nearT>farT)continue;}
   }else{
   for(unsigned axis=0;axis<3;++axis){if(std::abs(dir[axis])<1e-12f){if(origin[axis]<lo[axis]||origin[axis]>hi[axis]){overlaps=false;break;}}else{float a=(lo[axis]-origin[axis])/dir[axis],b=(hi[axis]-origin[axis])/dir[axis];if(a>b)std::swap(a,b);nearT=std::max(nearT,a);farT=std::min(farT,b);if(nearT>farT){overlaps=false;break;}}}if(!overlaps)continue;
   }
     if(!(n.right&0x80000000)){if(used+2>128)return false;stack[used++]=n.left;stack[used++]=n.right;continue;}
   for(unsigned i=n.left;i<n.left+(n.right&0x7fffffff);++i){auto& t=triangles[i];if((floorsOnly&&t.wall)||(wallsOnly&&!t.wall))continue;IntersectTriangle(i,from,direction,hit);}
  }return hit.triangle!=~0u;
 }
};
}


