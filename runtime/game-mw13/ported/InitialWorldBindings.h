#pragma once
#include "WorldLoadReadiness.h"
static bool InitialCollisionReady(){return !installedCollisionHooks.empty()&&!collision.triangles.empty();}
static bool InitialShadersReady(){if(v2Lost)return false;return !environmentInstalled||(device&&InitEnvironmentShaders()&&InitEnvironmentDepthShaders());}
static void InitialPropReadiness(Vec3 center,WorldLoadReadiness& r,std::set<unsigned>& needs){
 for(auto& holder:nativeProps){auto& p=*holder;const float* raw=reinterpret_cast<const float*>(p.record+16);
  float dx=center.x-raw[2],dy=center.y+raw[0];if(dx*dx+dy*dy>=180.f*180.f)continue;
  ++r.props;
#ifdef W2C_PUBLIC_BUILD
  // A terminal spawn failure cannot be retried. Terrain, collision and
  // pending props still have to be ready before dismissing the load screen.
  if(p.refused){++r.refusedProps;continue;}
#endif
  if(p.refused||!p.spawner||!p.initialized||(!PursuitPayloadOwner(&p)&&(!p.gpu.vb||!p.gpu.ib))){++r.missingProps;continue;}
  if(PursuitPayloadOwner(&p))continue; // native model/texture bank, no DDS draw
  for(auto& m:p.gpu.meshes){if(!m.texture||m.texture==0xD2E09151||m.texture==0x29D9478C)continue;needs.insert(m.texture);auto e=envMaterials.find(m.texture);if(e!=envMaterials.end()&&e->second.mask)needs.insert(e->second.mask);}
 }
}
static unsigned InitialGameFlow(){return *reinterpret_cast<unsigned*>(0x925E90);}
static void* InitialRace(){return *reinterpret_cast<void**>(0x91E000);}
static void* InitialFrontendManager(){return *reinterpret_cast<void**>(0x91CADC);}
static void* InitialLoadingPackage(){auto manager=InitialFrontendManager();auto name=reinterpret_cast<const char*(__cdecl*)()>(0x5705D0)();return manager&&name?reinterpret_cast<void*(__thiscall*)(void*,const char*)>(0x516AF0)(manager,name):nullptr;}
struct InitialFrontendLease {unsigned words[256]{};};
static bool ReadInitialFrontendLease(InitialFrontendLease& lease){__try{
 lease={};auto manager=InitialFrontendManager();if(!manager)return false;auto engine=*static_cast<BYTE**>(manager);if(!engine)return false;
 unsigned n=0;auto push=[&](unsigned value){lease.words[n++]=value;};push(unsigned(manager));push(unsigned(engine));
 auto head=*reinterpret_cast<BYTE**>(engine+0xE0);auto tail=*reinterpret_cast<BYTE**>(engine+0xE4);push(unsigned(head));push(unsigned(tail));
 for(unsigned offset:{0x4114u,0x4118u,0x411Cu,0x4124u,0x4128u,0x412Cu})push(*reinterpret_cast<unsigned*>(engine+offset));
 BYTE* previous=nullptr;unsigned count=0;for(auto node=head;node;node=*reinterpret_cast<BYTE**>(node+4)){
  if(++count>32||*reinterpret_cast<BYTE**>(node+8)!=previous)return false;
  for(unsigned i=0;i<count-1;++i)if(lease.words[10+i*7]==unsigned(node))return false;
  push(unsigned(node));for(unsigned offset:{4u,8u,12u,16u,28u,32u})push(*reinterpret_cast<unsigned*>(node+offset));previous=node;
 }return previous==tail;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool RearmInitialWorldActivation(){
 // The native dispatcher (006596A0) clears this slot before invoking the
 // callback. Like its own 006628F0 loading state, reschedule the same callback
 // so it is serviced once on the next engine update, not from a draw hook.
 auto next=reinterpret_cast<unsigned*>(0x925E70);
 if(*next)return false; // preserve a competing unload/travel transition
 next[1]=0;next[2]=0x8A8F0C;*next=0x6627E0;return true;
}
static void* InitialFrontendController(){return *reinterpret_cast<void**>(0x91CA48);}
static IDirect3DDevice9* InitialDevice(){return *reinterpret_cast<IDirect3DDevice9**>(0x982BDC);}
static bool InitialPlayerCenter(Vec3& result){__try{
 // IPlayer owns the local camera/vehicle, including a replacement police car
 // and the native attract autopilot. Do not infer ownership from driver class.
 using NFSPluginSDK::MW05::IPlayer;
 auto players=*reinterpret_cast<IPlayer***>(0x92D87C);unsigned playerCount=*reinterpret_cast<unsigned*>(0x92D884);
 if(players&&playerCount<=8)for(unsigned i=0;i<playerCount;++i){auto p=players[i];if(!p||!p->IsLocal()||!p->GetSimable())continue;
  // SDK UMath::Vector3 stores members as y,z,x. Native physics and all
  // scenery records use the contiguous raw axes, not those member names.
  const auto& actual=p->GetPosition();result=WorldLoadCenterFromNative(reinterpret_cast<const float*>(&actual));
  if(std::isfinite(result.x)&&std::isfinite(result.y)&&std::isfinite(result.z)&&fabs(result.x)<100000&&fabs(result.y)<100000&&fabs(result.z)<100000)return true;
 }
 auto list=*reinterpret_cast<BYTE***>(0x92CD1C);unsigned count=*reinterpret_cast<unsigned*>(0x92CD24);if(!list||count>256)return false;
 for(unsigned i=0;i<count;++i){auto v=list[i];if(!v||*reinterpret_cast<unsigned*>(v)!=0x8AA828||*reinterpret_cast<unsigned*>(v+0x94)!=0)continue;
  auto body=*reinterpret_cast<BYTE**>(v-0x34);if(!body)continue;auto matrix=*reinterpret_cast<BYTE***>(body+0x30);if(!matrix||!*matrix)continue;
  auto raw=reinterpret_cast<float*>(*matrix+0x10);result=WorldLoadCenterFromNative(raw);return std::isfinite(result.x)&&std::isfinite(result.y)&&std::isfinite(result.z);
 }return false;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
