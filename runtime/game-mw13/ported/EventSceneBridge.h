#pragma once
#include "../integration/MWEventSceneApi.h"
// Generic draw/contact consumers. Event package data, activation and textures
// stay in the independent event ASI; no editor sidecars enter the world loader.
static void AppendExternalEventContacts(Vec3 center,float radius,std::vector<NativeBarrier>& walls){
 static mwelscene::Contacts contacts=nullptr;if(!contacts){auto module=GetModuleHandleW(L"MWEventLoader.asi");if(module)contacts=reinterpret_cast<mwelscene::Contacts>(GetProcAddress(module,"MWEL_BarrierContactsV1"));}if(!contacts)return;
 static thread_local std::array<mwelscene::Barrier,2048> extra{};
 mwelscene::Point point{center.x,center.y,center.z};auto count=contacts(&point,radius,extra.data(),unsigned(extra.size()));if(count>extra.size())return;
 static_assert(sizeof(NativeBarrier)==sizeof(mwelscene::Barrier));for(unsigned i=0;i<count;++i){NativeBarrier value{};memcpy(&value,&extra[i],sizeof(value));walls.push_back(value);}
}
static bool ExternalOfficerIgnoresGuards(unsigned worldId){
 using Query=unsigned(__cdecl*)(unsigned);
 static Query query=nullptr;
 static ULONGLONG next=0;
 if(!query&&GetTickCount64()>=next){
  next=GetTickCount64()+1000;
  if(auto module=GetModuleHandleW(L"MWEventLoader.asi"))query=reinterpret_cast<Query>(GetProcAddress(module,"MWEL_OfficerIgnoresGuardsV1"));
 }
 return query&&query(worldId)!=0;
}
static void DrawExternalEventScene(void* view){
 static mwelscene::Draw draw=nullptr;if(!draw){auto module=GetModuleHandleW(L"MWEventLoader.asi");if(module)draw=reinterpret_cast<mwelscene::Draw>(GetProcAddress(module,"MWEL_DrawEventV1"));}if(draw)draw(view);
}
