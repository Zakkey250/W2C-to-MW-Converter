#pragma once
#include "CollisionWorld.h"
#include "../../integration/MWRaceBarrierPolicy.h"
namespace w2mw {
// MW 1.3's WWorldPos face is three 16-byte records, followed by flags,
// height offset and the native Attrib::Collection pointer. Surface and point
// layout were compared against the live legacy loader before replacement.
struct NativeFace {Vec3 p0;unsigned padding0;Vec3 p1;unsigned triangleFlags;Vec3 p2;unsigned padding2;};
struct NativeBarrier {Vec3 a;unsigned flags;Vec3 b;float inverseLength;unsigned extra[2];};
static_assert(sizeof(NativeFace)==48&&sizeof(NativeBarrier)==40);
#include "EventSceneBridge.h"
static void (*eventBarrierCollision)(Vec3,float,std::vector<NativeBarrier>&)=nullptr;
static std::vector<void*> surfaceCollections;
static unsigned groundQueries=0,wallQueries=0;
// A contact probe can start slightly below a thin deck after one physics step.
// Search one metre above it so stacked roads do not switch to the lower layer
// after a small penetration. The INI can restore the previous .02 m behavior.
static float groundRayUpwardTolerance=1.f;
static bool groundContactDiagnostics=false;
// Keep a nearby upper deck when a cached lower contact face is stale.
// Explicit INI 0 restores the previous native-list path for diagnosis.
static bool groundLayerReuseCorrection=true;
static bool GroundContactDiagnosticPoint(const Vec3& point){return (point.x>-3820.f&&point.x<-3700.f&&point.z>710.f&&point.z<830.f)||(point.x>-220.f&&point.x<-200.f&&point.z>1900.f&&point.z<1915.f);}
static void* Surface(const CollisionTriangle& t){if(surfaceCollections.size()!=collision.surfaces.size())surfaceCollections.resize(collision.surfaces.size());auto& ref=surfaceCollections[t.surface];if(!ref){auto hash=reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x5CC240);auto lookup=reinterpret_cast<void*(__cdecl*)(unsigned,unsigned)>(0x455FD0);ref=lookup(hash("simsurface"),hash(collision.surfaces[t.surface].data()));if(!ref)ref=lookup(hash("simsurface"),hash("asphalt_no_leaves"));}return ref;}
static void FillFace(void* destination,const RayHit& hit){const auto& t=collision.triangles[hit.triangle];auto out=static_cast<NativeFace*>(destination);*out={t.p[0],0,t.p[1],2,t.p[2],0};if(!t.wall&&Cross(Sub(out->p1,out->p0),Sub(out->p2,out->p0)).y<0)std::swap(out->p1,out->p2);}
static bool __fastcall NativeGround(void* self,void*,void*,const Vec3* point){NativeCollisionWorkTimer timing;if(!point)return false;auto flags=reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+48);*flags&=~1u;float offset=*reinterpret_cast<float*>(static_cast<BYTE*>(self)+52);if(!std::isfinite(offset)||std::abs(offset)>100)offset=0;RayHit hit;float originY=point->y+std::max(groundRayUpwardTolerance,offset);bool result=collision.Ray({point->x,originY,point->z},{point->x,point->y-1000,point->z},hit,true);if(result){FillFace(self,hit);*flags|=1;*reinterpret_cast<void**>(static_cast<BYTE*>(self)+56)=Surface(collision.triangles[hit.triangle]);}++groundQueries;if(groundContactDiagnostics&&GroundContactDiagnosticPoint(*point)){static DWORD previous=0;DWORD now=GetTickCount();if(now-previous>=100){previous=now;Log("GROUND_CONTACT_RAY at=%.4f,%.4f,%.4f origin_y=%.4f offset=%.4f hit=%u triangle=%u floor_y=%.4f",point->x,point->y,point->z,originY,offset,result?1u:0u,hit.triangle,result?hit.point.y:0.f);}}return result;}
static bool __fastcall NativeWorldRay(void* self,void*,void*,const Vec3* from,const Vec3* to){NativeCollisionWorkTimer timing;if(!from||!to)return false;RayHit hit;bool result=collision.Ray(*from,*to,hit);auto flags=reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+48);*flags&=~1u;if(result){FillFace(self,hit);*flags|=1;*reinterpret_cast<void**>(static_cast<BYTE*>(self)+56)=Surface(collision.triangles[hit.triangle]);}++groundQueries;return result;}
static bool __fastcall NativeGroundDispatch(void* self,void*,void* cache,const Vec3* point,unsigned){bool result=NativeGround(self,nullptr,cache,point);if(groundQueries<9)Log("GROUND_NATIVE hit=%u at=%.3f,%.3f,%.3f surface=%p",result,point->x,point->y,point->z,*reinterpret_cast<void**>(static_cast<BYTE*>(self)+56));return true;}
// The public world owns static collision even when optional props are absent.
#ifdef W2C_PUBLIC_BUILD
static bool propPolygonBridge=true;
#else
static bool propPolygonBridge=false;
#endif

// WCollisionMgr's +0x3c list contains CARP instances (inverse transforms and
// geometry pointers), not triangles. Its +0x5c list contains decoded 48-byte
// triangles. 0x787710 resolves CARP material indices into a Collection pointer
// at triangle +0x0c, then appends through 0x787350. We already own decoded world
// triangles: wrap their points in CARP strips for both ground and world queries.
struct NativeSurfaceTriangle {Vec3 a;void* surface;Vec3 b;unsigned padB;Vec3 c;unsigned stripFlags;};
static_assert(sizeof(NativeSurfaceTriangle)==48&&offsetof(NativeSurfaceTriangle,surface)==0x0c);
struct NativePolygon {Vec3 axisX;float extentX;unsigned short stamp,flags;float extentY;unsigned short gate,article;void* geometry;Vec3 axisZ;float extentZ;Vec3 inverseOrigin;float radius;};
struct NativeStripVertex {short xyz[3];unsigned char surface,flags;};
struct NativeFloorGeometry {unsigned short strips,stripBytes,edges,edgeBytes;unsigned char resolved,surfaces;unsigned short surfaceBytes,inter,flags;Vec3 center;unsigned short radius,offset;NativeStripVertex vertices[3];void* surface;};
struct NativeFloorEntry {NativePolygon polygon;NativeFloorGeometry geometry;};
struct OriginalFloorReference {NativePolygon polygon;std::vector<BYTE> geometry;};
static std::vector<OriginalFloorReference> originalFloorReferences;
static bool useOriginalFloorReference=false;
static bool LoadOriginalFloorReference(){
 FILE* f=nullptr;fopen_s(&f,".\\NativeProps\\floor-reference.bin","rb");if(!f)return false;unsigned header[3];bool valid=fread(header,4,3,f)==3&&header[0]==0x31525057&&header[1]<1000&&header[2]<1000;
 std::vector<std::array<char,80>> names(valid?header[2]:0);if(valid)valid=fread(names.data(),80,names.size(),f)==names.size();
 auto hash=reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x5CC240);auto lookup=reinterpret_cast<void*(__cdecl*)(unsigned,unsigned)>(0x455FD0);
 originalFloorReferences.reserve(valid?header[1]:0);
 for(unsigned i=0;valid&&i<header[1];i++){OriginalFloorReference e{};unsigned size=0;valid=fread(&e.polygon,64,1,f)==1&&fread(&size,4,1,f)==1&&size>=16&&size<100000;if(!valid)break;e.geometry.resize(size);valid=fread(e.geometry.data(),1,size,f)==size;if(!valid)break;
 auto g=reinterpret_cast<NativeFloorGeometry*>(e.geometry.data());unsigned start=16+g->stripBytes+g->edgeBytes;valid=start+4*g->surfaces<=size;
 for(unsigned j=0;valid&&j<g->surfaces;j++){auto ref=reinterpret_cast<unsigned*>(e.geometry.data()+start+4*j);valid=*ref<names.size()&&names[*ref][79]==0;if(!valid)break;auto material=lookup(hash("simsurface"),hash(names[*ref].data()));if(!material)material=lookup(hash("simsurface"),hash("asphalt_no_leaves"));valid=material!=nullptr;*ref=reinterpret_cast<unsigned>(material);}
 g->resolved=1;if(valid)originalFloorReferences.push_back(std::move(e));
 }
 valid=valid&&fgetc(f)==EOF;fclose(f);if(!valid){originalFloorReferences.clear();return false;}
 for(auto& e:originalFloorReferences)e.polygon.geometry=e.geometry.data();Log("PROP_FLOOR_REFERENCE instances=%u original_topology=1",unsigned(originalFloorReferences.size()));return true;
}
static_assert(sizeof(NativePolygon)==64&&offsetof(NativePolygon,geometry)==0x1c&&offsetof(NativePolygon,flags)==0x12);
static_assert(sizeof(NativeFloorGeometry)==60&&offsetof(NativeFloorGeometry,vertices)==32&&offsetof(NativeFloorGeometry,surface)==56);
struct BarrierCache {std::vector<NativeBarrier> walls;std::vector<unsigned> candidates;std::vector<NativeFloorEntry> floors;void* assignedWalls=nullptr;};
static void AddNativeFloor(BarrierCache& cache,Vec3 a,Vec3 b,Vec3 c,void* surface){
 Vec3 lo{std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})},hi{std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})};
 // CARP strips store signed 16-bit offsets at 1/128 metre precision.
 if(std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z})>500){Vec3 ab=Sub(b,a),bc=Sub(c,b),ca=Sub(a,c);float lengths[]={Dot(ab,ab),Dot(bc,bc),Dot(ca,ca)};if(lengths[0]>=lengths[1]&&lengths[0]>=lengths[2]){Vec3 m{(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};AddNativeFloor(cache,a,m,c,surface);AddNativeFloor(cache,m,b,c,surface);}else if(lengths[1]>=lengths[2]){Vec3 m{(b.x+c.x)*.5f,(b.y+c.y)*.5f,(b.z+c.z)*.5f};AddNativeFloor(cache,a,b,m,surface);AddNativeFloor(cache,a,m,c,surface);}else{Vec3 m{(c.x+a.x)*.5f,(c.y+a.y)*.5f,(c.z+a.z)*.5f};AddNativeFloor(cache,a,b,m,surface);AddNativeFloor(cache,m,b,c,surface);}return;}
 Vec3 origin{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f},extent{(hi.x-lo.x)*.5f+.01f,(hi.y-lo.y)*.5f+.01f,(hi.z-lo.z)*.5f+.01f};
 NativeFloorEntry e{};e.polygon={{1,0,0},extent.x,0,1,extent.y,0,0,nullptr,{0,0,1},extent.z,{-origin.x,-origin.y,-origin.z},std::sqrt(Dot(extent,extent))};
 auto& g=e.geometry;g.strips=1;g.stripBytes=40;g.resolved=1;g.surfaces=1;g.surfaceBytes=4;g.offset=16;g.radius=static_cast<unsigned short>(std::ceil(e.polygon.radius*16));g.surface=surface;
 Vec3 points[]={a,b,c};for(unsigned n=0;n<3;n++)for(unsigned k=0;k<3;k++)g.vertices[n].xyz[k]=static_cast<short>(std::lround(((&points[n].x)[k]-(&origin.x)[k])*128));
 g.vertices[0].surface=3; // First vertex's surface/flags bytes encode strip count.
 cache.floors.push_back(e);
}
static std::map<void*,BarrierCache> barrierCaches;
static void (__thiscall* originalCollisionLists)(void*,unsigned);
static void (__thiscall* originalBarrierClear)(void*);
static void (__thiscall* originalListReserve)(void*,unsigned);
static void (__thiscall* originalListReset)(void*,unsigned);
static void __fastcall NativeListReserve(void* self,void*,unsigned flags){originalListReserve(self,flags&(propPolygonBridge?0x1c:0x10));}
static void __fastcall NativeListReset(void* self,void*,unsigned flags){originalListReset(self,flags&(propPolygonBridge?0x1c:0x10));}
static void __fastcall NativeBarrierClear(void* self,void*){auto owner=static_cast<BYTE*>(self)-76;auto it=barrierCaches.find(owner);if(it!=barrierCaches.end()&&*reinterpret_cast<void**>(static_cast<BYTE*>(self)+4)==it->second.assignedWalls){memset(static_cast<BYTE*>(self)+4,0,12);}originalBarrierClear(self);}
static void __fastcall NativeCollisionLists(void* self,void*,unsigned flags){NativeCollisionWorkTimer timing;originalCollisionLists(self,flags&0x10);auto data=static_cast<BYTE*>(self);Vec3 center=*reinterpret_cast<Vec3*>(data+32);float radius=*reinterpret_cast<float*>(data+44);if(!std::isfinite(radius))radius=15;radius=std::clamp(radius+5,15.f,100.f);auto& cache=barrierCaches[self];collision.QueryBox({center.x-radius,center.y-4,center.z-radius},{center.x+radius,center.y+4,center.z+radius},cache.candidates,true);cache.walls.clear();
 // Bit 8 rebuilds the paired CARP and decoded ground lists. Bit 4 updates
 // barriers only; bit 16 updates scenery only. Preserve the ground cache
 // between those updates so native box contacts keep all support points.
 if(propPolygonBridge&&(flags&8)){
 originalListReset(self,8);collision.QueryBox({center.x-radius,center.y-radius,center.z-radius},{center.x+radius,center.y+radius,center.z+radius},cache.candidates,false);cache.floors.clear();
 bool referenceHere=useOriginalFloorReference&&center.x>-1410&&center.x<-1380&&center.z>2035&&center.z<2065;
 if(referenceHere&&originalFloorReferences.empty())referenceHere=LoadOriginalFloorReference();
 for(auto id:cache.candidates){if(referenceHere)break;auto& t=collision.triangles[id];if(t.wall)continue;auto a=t.p[0],b=t.p[1],c=t.p[2];
 if(std::max({a.x,b.x,c.x})<center.x-radius||std::min({a.x,b.x,c.x})>center.x+radius||std::max({a.y,b.y,c.y})<center.y-radius||std::min({a.y,b.y,c.y})>center.y+radius||std::max({a.z,b.z,c.z})<center.z-radius||std::min({a.z,b.z,c.z})>center.z+radius)continue;
 if(Cross(Sub(b,a),Sub(c,a)).y<0)std::swap(b,c);
 AddNativeFloor(cache,a,b,c,Surface(t));}
 reinterpret_cast<void(__thiscall*)(void*,unsigned)>(0x77FDF0)(data+60,unsigned(referenceHere?originalFloorReferences.size():cache.floors.size()));
 if(referenceHere)for(auto& e:originalFloorReferences){void* ptr=&e.polygon;reinterpret_cast<void(__thiscall*)(void*,void*)>(0x7822C0)(data+60,&ptr);}
 for(auto& e:cache.floors){e.polygon.geometry=&e.geometry;void* ptr=&e.polygon;reinterpret_cast<void(__thiscall*)(void*,void*)>(0x7822C0)(data+60,&ptr);}
 if(flags&8)reinterpret_cast<void(__thiscall*)(void*,void*,const Vec3*,float,void*)>(0x787710)(nullptr,data+60,&center,radius,data+92);
 if(groundContactDiagnostics&&GroundContactDiagnosticPoint(center)){static DWORD previous=0;DWORD now=GetTickCount();if(now-previous>=200){previous=now;unsigned upper=0,lower=0,decodedUpper=0,decodedLower=0,decodedCount=0;for(const auto& floor:cache.floors){float y=-floor.polygon.inverseOrigin.y;if(y>197.5f&&y<201.f)++upper;else if(y>193.f&&y<197.5f)++lower;}
 auto first=*reinterpret_cast<NativeSurfaceTriangle**>(data+92),last=*reinterpret_cast<NativeSurfaceTriangle**>(data+96);auto start=reinterpret_cast<uintptr_t>(first),finish=reinterpret_cast<uintptr_t>(last);if(start&&finish>=start&&finish-start<=48u*100000u&&(finish-start)%sizeof(NativeSurfaceTriangle)==0){decodedCount=unsigned((finish-start)/sizeof(NativeSurfaceTriangle));for(auto p=first;p!=last;++p){float y=(p->a.y+p->b.y+p->c.y)/3.f;if(y>197.5f&&y<201.f)++decodedUpper;else if(y>193.f&&y<197.5f)++decodedLower;}}
 Log("GROUND_CONTACT_LIST center=%.3f,%.3f,%.3f radius=%.3f flags=%X native_floors=%u upper=%u lower=%u decoded=%u decoded_upper=%u decoded_lower=%u",center.x,center.y,center.z,radius,flags,unsigned(cache.floors.size()),upper,lower,decodedCount,decodedUpper,decodedLower);}}
 static unsigned traces=0;if(traces++<8)Log("PROP_GROUND_NATIVE instances=%u at=%.3f,%.3f,%.3f format=carp64+strip",unsigned(cache.floors.size()),center.x,center.y,center.z);
 collision.QueryBox({center.x-radius,center.y-4,center.z-radius},{center.x+radius,center.y+4,center.z+radius},cache.candidates,true);
 }else if(!propPolygonBridge){*reinterpret_cast<void**>(data+64)=nullptr;*reinterpret_cast<void**>(data+68)=nullptr;}
 for(unsigned id:cache.candidates){const auto& t=collision.triangles[id];unsigned first=0;float best=-1;for(unsigned n=0;n<3;++n){auto a=t.p[n],b=t.p[(n+2)%3];float d=(a.x-b.x)*(a.x-b.x)+(a.z-b.z)*(a.z-b.z);if(d>best){best=d;first=n;}}if(best<1e-10f)continue;Vec3 a=t.p[first],b=t.p[(first+2)%3];float bottom=std::min({t.p[0].y,t.p[1].y,t.p[2].y}),top=std::max({t.p[0].y,t.p[1].y,t.p[2].y})-.25f;if(top<=bottom||center.y+4<bottom||center.y-4>top)continue;a.y=bottom;b.y=top;cache.walls.push_back({a,512,b,1/std::sqrt(best),{0,0}});}
 const size_t worldWallCount=cache.walls.size();
 if(eventBarrierCollision)eventBarrierCollision(center,radius,cache.walls);
 AppendExternalEventContacts(center,radius,cache.walls);
 const auto query=mw_race_barriers::ForCollision(self);const size_t guardCount=cache.walls.size()-worldWallCount;
 const bool officer=guardCount&&query.driver==0&&ExternalOfficerIgnoresGuards(query.worldId);
 cache.walls.erase(std::remove_if(cache.walls.begin()+worldWallCount,cache.walls.end(),[&](const NativeBarrier& w){return officer||!mw_race_barriers::Collides(w.flags,query);}),cache.walls.end());
 static unsigned seen=0;if(guardCount&&query.driver<7&&!(seen&(1u<<query.driver))){seen|=1u<<query.driver;Log("RACE_WALL_COLLISION_POLICY driver=%u world_id=%08X exclusions=%08X guards=%u collidable=%u world_walls=%u native_world_preserved=1",query.driver,query.worldId,query.exclusions,unsigned(guardCount),unsigned(cache.walls.size()-worldWallCount),unsigned(worldWallCount));}
 const size_t oneSide=cache.walls.size();cache.walls.reserve(oneSide*2);for(size_t n=0;n<oneSide;++n){auto w=cache.walls[n];cache.walls.push_back({{w.b.x,w.a.y,w.b.z},w.flags,{w.a.x,w.b.y,w.a.z},w.inverseLength,{0,0}});}
 *reinterpret_cast<void**>(data+80)=cache.walls.empty()?nullptr:cache.walls.data();*reinterpret_cast<void**>(data+84)=cache.walls.empty()?nullptr:cache.walls.data()+cache.walls.size();cache.assignedWalls=cache.walls.empty()?nullptr:cache.walls.data();++wallQueries;
}
static std::vector<void*> installedCollisionHooks;
static bool (__thiscall* originalContactGround)(void*,const Vec3*,float*,unsigned,void*,unsigned);
static bool ContactFaceHeight(void* self,const Vec3& point,float& height){
 if(!(*reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+48)&1u))return false;
 const auto face=static_cast<const NativeFace*>(self);
 const Vec3 ab=Sub(face->p1,face->p0),ac=Sub(face->p2,face->p0);
 const float determinant=ab.x*ac.z-ab.z*ac.x;
 if(!std::isfinite(determinant)||std::abs(determinant)<1e-7f)return false;
 const float dx=point.x-face->p0.x,dz=point.z-face->p0.z;
 const float u=(dx*ac.z-dz*ac.x)/determinant,v=(ab.x*dz-ab.z*dx)/determinant;
 if(u<-.001f||v<-.001f||u+v>1.001f)return false;
 height=face->p0.y+u*ab.y+v*ac.y;
 return std::isfinite(height);
}
static bool (__thiscall* originalVisualGround)(void*,void*,const Vec3*,unsigned);
static bool projectedShadowGround=false;
static void SetVisualGround(void* self,const RayHit& hit){
 FillFace(self,hit);
 auto data=static_cast<BYTE*>(self);
 *reinterpret_cast<unsigned*>(data+48)|=1u;
 *reinterpret_cast<void**>(data+56)=Surface(collision.triangles[hit.triangle]);
}
static bool ProjectedShadowGround(void* self,const Vec3& point,bool result,bool corner){
 // The native projection sets this render-space origin before the center and
 // corner floor probes. Physics, the car model and SunSet's depth pass do not
 // use these call sites. Keep the native face when it is on the car's deck.
 const auto renderOrigin=*reinterpret_cast<const Vec3*>(0x9B0C60);
 const Vec3 origin{-renderOrigin.y,renderOrigin.z,renderOrigin.x};
 if(!std::isfinite(origin.y)||!std::isfinite(point.y))return result;
 constexpr float maximumDrop=3.f;
 constexpr float upwardTolerance=1.25f; // Native shadow center probe (0x744267).
 float floor;
 if(ContactFaceHeight(self,point,floor)&&
    floor>=origin.y-maximumDrop&&floor<=origin.y+upwardTolerance)return result;
 RayHit hit;
 if(collision.Ray({point.x,origin.y+upwardTolerance,point.z},
                  {point.x,origin.y-maximumDrop,point.z},hit,true)){
  SetVisualGround(self,hit);
  return true;
 }
 // If the center still has support but this corner is off the bridge, use
 // the native missing-corner path rather than stretching a quad to a road
 // far beneath it. Leave normal airborne shadows to the original game.
 if(!corner||!collision.Ray({origin.x,origin.y+upwardTolerance,origin.z},
                           {origin.x,origin.y-maximumDrop,origin.z},hit,true))return result;
 *reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+48)&=~1u;
 return false;
}
static bool __fastcall NativeVisualGround(void* self,void*,void* cache,const Vec3* point,unsigned option){
 const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
 const bool result=originalVisualGround(self,cache,point,option);
 if(projectedShadowGround&&point&&(caller==0x743F04||caller==0x74427F||caller==0x7443AF))
  return ProjectedShadowGround(self,*point,result,caller==0x7443AF);
 // CubicCameraMover uses this floor to bound the camera height. At stacked
 // roads the decoded list can pick a much lower deck and pull the eye under
 // the road. Only correct that call, using the actual floor below the eye.
 constexpr float cameraClearanceLimit=4.5f; // Native upper limit at 0x47122F.
 float nativeHeight;
 if(caller!=0x4711D2||!point||!ContactFaceHeight(self,*point,nativeHeight)||
    point->y-nativeHeight<=cameraClearanceLimit)return result;
 RayHit upper;
 if(!collision.Ray(*point,{point->x,point->y-cameraClearanceLimit,point->z},upper,true))return result;
 SetVisualGround(self,upper);
 if(worldLogEnabled){
  static bool first=true;
  if(first){
   first=false;
   Log("CAMERA_GROUND_LAYER eye=%.3f,%.3f,%.3f lower_y=%.3f upper_y=%.3f",
       point->x,point->y,point->z,nativeHeight,upper.point.y);
  }
 }
 return true;
}
static bool NearbyUpperGround(const Vec3* point,float previousY,RayHit& candidate){
 if(!point||!std::isfinite(point->y)||point->y-previousY<1.2f)return false;
 const Vec3 from{point->x,point->y+groundRayUpwardTolerance,point->z},to{point->x,point->y-3.f,point->z};
 if(!collision.Ray(from,to,candidate,true))return false;
 const float difference=point->y-candidate.point.y;
 return candidate.point.y>previousY+.5f&&difference>=-groundRayUpwardTolerance&&difference<=1.5f;
}
static bool __fastcall TraceContactGround(void* self,void*,const Vec3* point,float* normal,unsigned reuse,void* cache,unsigned fallback){
 RayHit candidate{};float previousY=0;
 const bool stale=groundLayerReuseCorrection&&point&&reuse&&ContactFaceHeight(self,*point,previousY)&&
     NearbyUpperGround(point,previousY,candidate);
 // The native list path still starts its face search almost at point.y, so
 // merely clearing reuse can pick the lower deck again. A confirmed stale
 // lower face must use the WMC ground ray, which starts above small deck
 // penetration and lets WWorldPos calculate the native contact normal/depth.
 bool result=originalContactGround(self,point,normal,stale?0u:reuse,stale?nullptr:cache,fallback);
 // The decoded list can also choose the lower face when there was no cached
 // face to reuse. Apply the same bounded V1 ray after that lower hit or miss.
 bool corrected=stale;
 if(groundLayerReuseCorrection&&cache&&point&&normal&&!stale&&
    (!result||normal[3]<-1.2f||normal[3]>groundRayUpwardTolerance)){
  float chosenY=point->y-3.f;
  if(result&&!ContactFaceHeight(self,*point,chosenY))chosenY=point->y-3.f;
  if(NearbyUpperGround(point,chosenY,candidate)){
   result=originalContactGround(self,point,normal,0u,nullptr,fallback);
   corrected=true;
  }
 }
 if(groundContactDiagnostics&&point&&normal&&GroundContactDiagnosticPoint(*point)){
  static DWORD previous=0;const DWORD now=GetTickCount();
  if(now-previous<100)return result;
  previous=now;
 auto face=static_cast<NativeFace*>(self);
 Log("PROP_CONTACT_GROUND at=%.4f,%.4f,%.4f result=%u normal=%.5f,%.5f,%.5f depth=%.5f face_y=%.4f,%.4f,%.4f flags=%08X cache=%p reuse=%u corrected=%u candidate_y=%.4f",point->x,point->y,point->z,result,normal[0],normal[1],normal[2],normal[3],face->p0.y,face->p1.y,face->p2.y,*reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+48),cache,reuse,corrected?1u:0u,corrected?candidate.point.y:0.f);
 }
 return result;
}
static void UninstallNativeCollision(){for(auto address:installedCollisionHooks){MH_DisableHook(address);MH_RemoveHook(address);}installedCollisionHooks.clear();}
static bool InstallNativeCollision(){if(collision.nodes.empty())return false;
#ifdef W2C_PUBLIC_BUILD
 const auto ini=std::string(root)+"W2CMWPublic\\runtime.ini";
#else
 const auto ini=std::string(".\\W2CMWv2.ini");
#endif
 char rayToleranceSetting[32]{};GetPrivateProfileStringA("Collision","GroundRayUpwardToleranceMeters","1.0",rayToleranceSetting,sizeof(rayToleranceSetting),ini.c_str());char* rayToleranceEnd=nullptr;float rayTolerance=strtof(rayToleranceSetting,&rayToleranceEnd);if(rayToleranceEnd!=rayToleranceSetting&&*rayToleranceEnd==0&&std::isfinite(rayTolerance)&&rayTolerance>=.02f&&rayTolerance<=1.f)groundRayUpwardTolerance=rayTolerance;
#ifdef W2C_PUBLIC_BUILD
 groundContactDiagnostics=worldLogEnabled&&GetPrivateProfileIntA("Collision","GroundContactDiagnostics",0,ini.c_str())!=0;
#else
 groundContactDiagnostics=GetPrivateProfileIntA("Collision","GroundContactDiagnostics",0,ini.c_str())!=0;
#endif
 groundLayerReuseCorrection=GetPrivateProfileIntA("Collision","GroundLayerReuseCorrection",1,ini.c_str())!=0;
 Log("GROUND_RAY_TOLERANCE meters=%.3f previous=0.020",groundRayUpwardTolerance);
#ifndef W2C_PUBLIC_BUILD
 useOriginalFloorReference=GetPrivateProfileIntA("Probe","OriginalFloorReference",0,".\\W2CMWv2Bootstrap.ini")!=0&&GetPrivateProfileIntA("Probe","PropEffectsDiagnostics",0,".\\W2CMWv2Bootstrap.ini")!=0;
#endif
 struct Guard {unsigned address;BYTE bytes[9];};const Guard guards[]={
 {0x778F80,{0x81,0xEC,0xA8,0,0,0,0x55,0x8B,0xE9}},{0x779170,{0x83,0xEC,0x20,0x8B,0x44,0x24,0x28,0x53,0x55}},
 {0x786750,{0x6A,0xFF,0x68,0x08,0x22,0x88,0,0x64,0xA1}},{0x779310,{0x81,0xEC,0x80,0,0,0,0x55,0x8B,0xE9}},
 {0x789320,{0x83,0xEC,0x08,0x53,0x8A,0x5C,0x24,0x10,0xF6}},{0x77C6E0,{0x56,0x8B,0xF1,0x57,0x8B,0x7E,0x04,0x85,0xFF}},
 {0x781670,{0x53,0x8A,0x5C,0x24,0x08,0xF6,0xC3,0x08,0x56}},{0x782AB0,{0x53,0x8A,0x5C,0x24,0x08,0xF6,0xC3,0x08,0x56}},
 {0x787350,{0x6A,0xFF,0x68,0xEF,0x22,0x88,0,0x64,0xA1}},
 {0x787710,{0x81,0xEC,0xEC,0,0,0,0xD9,0x84,0x24}}};
 for(auto& guard:guards)if(memcmp(reinterpret_cast<void*>(guard.address),guard.bytes,sizeof(guard.bytes))){Log("REFUSED collision byte guard %08X",guard.address);return false;}
 struct Hook {unsigned address;void* replacement;const char* name;};Hook hooks[]={{0x778F80,reinterpret_cast<void*>(NativeGround),"ground"},{0x786750,reinterpret_cast<void*>(NativeGroundDispatch),"ground-dispatch"},{0x779310,reinterpret_cast<void*>(NativeWorldRay),"world-ray"},{0x789320,reinterpret_cast<void*>(NativeCollisionLists),"collision-lists"}};
 auto rollback=[&](){UninstallNativeCollision();return false;};
 auto install=[&](unsigned address,void* replacement,void** original){auto target=reinterpret_cast<void*>(address);if(MH_CreateHook(target,replacement,original)!=MH_OK)return false;installedCollisionHooks.push_back(target);return MH_EnableHook(target)==MH_OK;};
 bool traceContact=groundContactDiagnostics||groundLayerReuseCorrection;
#ifndef W2C_PUBLIC_BUILD
 traceContact=traceContact||GetPrivateProfileIntA("Probe","PropEffectsDiagnostics",0,".\\W2CMWv2Bootstrap.ini")||GetPrivateProfileIntA("Probe","PursuitDiagnostics",0,".\\W2CMWv2Bootstrap.ini");
#endif
 if(traceContact){
 static const BYTE contactGuard[]={0x8B,0x44,0x24,0x10,0x53,0x55,0x8B,0x6C,0x24,0x0C};
 if(memcmp(reinterpret_cast<void*>(0x789CC0),contactGuard,sizeof(contactGuard))||!install(0x789CC0,TraceContactGround,reinterpret_cast<void**>(&originalContactGround)))return rollback();
 }
 if(!install(0x77C6E0,NativeBarrierClear,reinterpret_cast<void**>(&originalBarrierClear))||!install(0x781670,NativeListReserve,reinterpret_cast<void**>(&originalListReserve))||!install(0x782AB0,NativeListReset,reinterpret_cast<void**>(&originalListReset)))return rollback();
 for(auto& hook:hooks){auto original=hook.address==0x789320?reinterpret_cast<void**>(&originalCollisionLists):nullptr;if(!install(hook.address,hook.replacement,original)){Log("REFUSED native collision hook %s",hook.name);return rollback();}}
 static const BYTE cameraGroundGuard[]={0x8B,0x44,0x24,0x04,0x85,0xC0,0x74,0x0C,0x83,0xC0,0x5C};
 static const BYTE cameraCallGuard[]={0xE8,0xBE,0x8A,0x31,0x00,0x8A,0x84,0x24,0xE4,0x00,0x00,0x00};
 static const BYTE shadowCenterGuard[]={0xE8,0x8C,0x5D,0x04,0x00,0xF6,0x43,0x78,0x01};
 static const BYTE shadowPlaneGuard[]={0xE8,0x11,0x5A,0x04,0x00,0xF6,0x43,0x78,0x01};
 static const BYTE shadowCornerGuard[]={0xE8,0xE1,0x58,0x04,0x00,0x8A,0x43,0x78,0x24,0x01};
 projectedShadowGround=!memcmp(reinterpret_cast<void*>(0x743EFF),shadowCenterGuard,sizeof(shadowCenterGuard))&&
     !memcmp(reinterpret_cast<void*>(0x74427A),shadowPlaneGuard,sizeof(shadowPlaneGuard))&&
     !memcmp(reinterpret_cast<void*>(0x7443AA),shadowCornerGuard,sizeof(shadowCornerGuard));
 if(!memcmp(reinterpret_cast<void*>(0x789C90),cameraGroundGuard,sizeof(cameraGroundGuard))&&
    !memcmp(reinterpret_cast<void*>(0x4711CD),cameraCallGuard,sizeof(cameraCallGuard))&&
    install(0x789C90,NativeVisualGround,reinterpret_cast<void**>(&originalVisualGround))){
  Log("ACTIVE chase camera stacked-road correction");
  Log("PROJECTED_VEHICLE_SHADOW_LAYER %s",projectedShadowGround?"active":"guard_mismatch");
 }else Log("REFUSED chase camera and projected vehicle shadow ground correction");
 Log("ACTIVE native collision BVH triangles=%u surfaces=%u; floor and barriers independent of rendering",unsigned(collision.triangles.size()),unsigned(collision.surfaces.size()));return true;
}
}


namespace w2mw {
// Keep the native ground/scenery queries and native allocation ownership.
struct StockEditorWalls {void* first=nullptr;void* end=nullptr;unsigned capacity=0;std::vector<NativeBarrier> combined;};
static std::map<void*,StockEditorWalls> stockEditorWalls;
static void DetachStockEditorWalls(void* self){auto it=stockEditorWalls.find(self);if(it==stockEditorWalls.end())return;auto data=static_cast<BYTE*>(self);auto& s=it->second;if(*reinterpret_cast<void**>(data+80)==s.combined.data()){*reinterpret_cast<void**>(data+80)=s.first;*reinterpret_cast<void**>(data+84)=s.end;*reinterpret_cast<unsigned*>(data+88)=s.capacity;}}
static void __fastcall StockEditorReserve(void* self,void*,unsigned flags){DetachStockEditorWalls(self);originalListReserve(self,flags);}
static void __fastcall StockEditorReset(void* self,void*,unsigned flags){DetachStockEditorWalls(self);originalListReset(self,flags);}
static void __fastcall StockEditorClear(void* self,void*){DetachStockEditorWalls(static_cast<BYTE*>(self)-76);originalBarrierClear(self);stockEditorWalls.erase(static_cast<BYTE*>(self)-76);}
static void __fastcall StockEditorCollision(void* self,void*,unsigned flags){
 DetachStockEditorWalls(self);originalCollisionLists(self,flags);if(!(flags&4)||!eventBarrierCollision)return;
 auto data=static_cast<BYTE*>(self);auto a=*reinterpret_cast<NativeBarrier**>(data+80),end=*reinterpret_cast<NativeBarrier**>(data+84);if((!a)!=(!end)||end<a||end-a>100000)return;
 auto& s=stockEditorWalls[self];s.first=a;s.end=end;s.capacity=*reinterpret_cast<unsigned*>(data+88);s.combined.clear();if(a)s.combined.assign(a,end);
 Vec3 center=*reinterpret_cast<Vec3*>(data+32);float radius=*reinterpret_cast<float*>(data+44);if(!std::isfinite(radius))return;
 std::vector<NativeBarrier> extra;eventBarrierCollision(center,std::clamp(radius+5,15.f,100.f),extra);if(extra.empty())return;
 const auto query=mw_race_barriers::ForCollision(self);unsigned added=0;
 if(query.driver==0&&ExternalOfficerIgnoresGuards(query.worldId))return;
 for(auto w:extra){if(!mw_race_barriers::Collides(w.flags,query))continue;++added;s.combined.push_back(w);s.combined.push_back({{w.b.x,w.a.y,w.b.z},w.flags,{w.a.x,w.b.y,w.a.z},w.inverseLength,{0,0}});}
 if(!added)return;
 *reinterpret_cast<void**>(data+80)=s.combined.data();*reinterpret_cast<void**>(data+84)=s.combined.data()+s.combined.size();
}
static bool InstallStockEditorCollision(){
 struct Hook {unsigned address;void* replacement;void** original;BYTE guard[9];};
 Hook hooks[]={{0x789320,reinterpret_cast<void*>(StockEditorCollision),reinterpret_cast<void**>(&originalCollisionLists),{0x83,0xEC,0x08,0x53,0x8A,0x5C,0x24,0x10,0xF6}},
 {0x77C6E0,reinterpret_cast<void*>(StockEditorClear),reinterpret_cast<void**>(&originalBarrierClear),{0x56,0x8B,0xF1,0x57,0x8B,0x7E,0x04,0x85,0xFF}},
 {0x781670,reinterpret_cast<void*>(StockEditorReserve),reinterpret_cast<void**>(&originalListReserve),{0x53,0x8A,0x5C,0x24,0x08,0xF6,0xC3,0x08,0x56}},
 {0x782AB0,reinterpret_cast<void*>(StockEditorReset),reinterpret_cast<void**>(&originalListReset),{0x53,0x8A,0x5C,0x24,0x08,0xF6,0xC3,0x08,0x56}}};
 for(auto& h:hooks)if(memcmp(reinterpret_cast<void*>(h.address),h.guard,sizeof(h.guard)))return false;
 std::vector<void*> installed;for(auto& h:hooks){auto site=reinterpret_cast<void*>(h.address);if(MH_CreateHook(site,h.replacement,h.original)!=MH_OK){for(auto p:installed){MH_DisableHook(p);MH_RemoveHook(p);}return false;}installed.push_back(site);if(MH_EnableHook(site)!=MH_OK){for(auto p:installed){MH_DisableHook(p);MH_RemoveHook(p);}return false;}}
 Log("MW_EDITOR_COLLISION_ACTIVE native_floor_and_native_walls_preserved=1");return true;
}
}
