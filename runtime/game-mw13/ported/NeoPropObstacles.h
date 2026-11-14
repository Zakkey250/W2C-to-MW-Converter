#pragma once
#include "NeoFallenPropBounds.h"
// Light smashables stay contact-racing objects. A detached structural slab that
// has fallen onto another road is different: the native floor pieces observed
// in the lab weigh 20,000 kg. Snapshot only heavy, fallen, settled children.
static DWORD propObstacleThread=0;
struct PropObstacleBinding {unsigned owner=0;void* body=nullptr;void* model=nullptr;};
static std::map<void*,PropObstacleBinding> propObstacleBindings;
static std::map<void*,mwneo_obstacles::Volume> propSleepingObstacles;
static ULONGLONG propObstacleAt=0;
static void PropObstacleAwake(void* sim,NativeProp* owner,void* model){
 void* body=nullptr;__try{if(sim&&*reinterpret_cast<unsigned*>(sim)==0x8AA638)body=*reinterpret_cast<void**>(static_cast<BYTE*>(sim)+0x4c);}__except(EXCEPTION_EXECUTE_HANDLER){}
 if(body&&owner&&model){propObstacleBindings[sim]={owner->id,body,model};propSleepingObstacles.erase(model);propObstacleAt=0;}
}
static bool ReadFallenPropObstacle(void* sim,const NativeProp* owner,mwneo_obstacles::Volume& result){
 auto binding=propObstacleBindings.find(sim);if(binding==propObstacleBindings.end()||!owner||binding->second.owner!=owner->id)return false;
 __try{
  if(!sim||!owner||!owner->nativeFractured||*reinterpret_cast<unsigned*>(sim)!=0x8AA638)return false;
  auto body=*reinterpret_cast<BYTE**>(static_cast<BYTE*>(sim)+0x4c);if(!body||body!=binding->second.body)return false;
  auto vt=*reinterpret_cast<unsigned**>(body);
  if(vt[6]!=0x671160||vt[8]!=0x671180||vt[9]!=0x671190||vt[16]!=0x6712A0||vt[18]!=0x670E50)return false;
  float mass=reinterpret_cast<float(__thiscall*)(void*)>(vt[6])(body);
  auto position=reinterpret_cast<const Vec3*(__thiscall*)(void*)>(vt[8])(body);
  auto velocity=reinterpret_cast<const Vec3*(__thiscall*)(void*)>(vt[9])(body);
  Vec3 dimensions{};D3DMATRIX matrix{};
  reinterpret_cast<void(__thiscall*)(void*,Vec3*)>(vt[18])(body,&dimensions);
  reinterpret_cast<void(__thiscall*)(void*,D3DMATRIX*)>(vt[16])(body,&matrix);
  return position&&velocity&&mwneo_obstacles::FallenVolume(mass,&position->x,&velocity->x,&dimensions.x,&matrix._11,owner->initial._42,owner->id,result);
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void PropObstacleSleeping(void* sim,NativeProp* owner){
 auto binding=propObstacleBindings.find(sim);if(binding==propObstacleBindings.end())return;
 mwneo_obstacles::Volume volume;
 if(ReadFallenPropObstacle(sim,owner,volume))propSleepingObstacles[binding->second.model]=volume;
 propObstacleBindings.erase(binding);propObstacleAt=0;
}
static void PropObstacleModelGone(void* model){
 propSleepingObstacles.erase(model);
 for(auto i=propObstacleBindings.begin();i!=propObstacleBindings.end();)if(i->second.model==model)i=propObstacleBindings.erase(i);else ++i;
 propObstacleAt=0;
}
static void PropObstacleOwnerGone(unsigned owner){
 for(auto i=propSleepingObstacles.begin();i!=propSleepingObstacles.end();)if(i->second.owner==owner)i=propSleepingObstacles.erase(i);else ++i;
 for(auto i=propObstacleBindings.begin();i!=propObstacleBindings.end();)if(i->second.owner==owner)i=propObstacleBindings.erase(i);else ++i;
 propObstacleAt=0;
}
// At most one refresh per 64 ms, independently of racer count. A short copied
// cache covers sleep/wake transitions without retaining native child pointers.
static std::vector<mwneo_obstacles::Volume> propObstacleCache;
static unsigned QueryFallenPropObstacles(unsigned version,const float* origin,float range,mwneo_obstacles::Volume* output,unsigned capacity){
 if(version!=mwneo_obstacles::Version||!origin||!output||!capacity||capacity>128||
    GetCurrentThreadId()!=propObstacleThread||*reinterpret_cast<unsigned*>(0x925E90)!=6||propNativeReference)return 0;
 if(!std::isfinite(range)||range<=0||range>500)return 0;
 for(unsigned i=0;i<3;++i)if(!std::isfinite(origin[i]))return 0;
 auto now=GetTickCount64();
 if(!propObstacleAt||now-propObstacleAt>=64){
  propObstacleAt=now;propObstacleCache.clear();
  for(const auto& sleeping:propSleepingObstacles)propObstacleCache.push_back(sleeping.second);
  for(const auto& child:propChildSims){mwneo_obstacles::Volume volume;
   if(ReadFallenPropObstacle(child.first,child.second,volume))propObstacleCache.push_back(volume);
  }
 }
 unsigned count=0;
 for(const auto& volume:propObstacleCache){
  float dx=origin[0]-volume.position[0],dz=origin[2]-volume.position[2];
  float reach=range+std::max(volume.halfWidth,volume.halfLength);
  if(dx*dx+dz*dz>reach*reach)continue;
  output[count++]=volume;if(count==capacity)break;
 }
 return count;
}
#pragma comment(linker,"/EXPORT:W2MW_NeoObstaclesV1=_W2MW_NeoObstaclesV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_NeoObstaclesV1(unsigned version,const float* origin,float range,mwneo_obstacles::Volume* output,unsigned capacity){
 __try{return QueryFallenPropObstacles(version,origin,range,output,capacity);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
