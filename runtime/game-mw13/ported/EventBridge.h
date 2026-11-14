#pragma once
#include <NFSPluginSDK/Game.MW05/Types/GRaceStatus.h>
// Native gameplay icons keep their ordinary pursuit visibility and map logic.
// Only streaming residency is supplied by the replacement world loader.
struct EventIcon {unsigned short type,flags;short section,combined;void* model;void* emitter;Vec3 position;unsigned short rotation,pad;};
static_assert(sizeof(EventIcon)==32);
static void* eventMarkerChunk=nullptr;static bool eventMarkerRegistered=false,eventDiagnostics=false,eventBridgeActive=false;
static DWORD eventLastTick=0,eventLastLog=0;
static void TickEventBridge();
static void(__thiscall* eventOriginalVisibility)(void*);
static void __fastcall EventVisibility(void* self,void*){
 if(eventDiagnostics){auto m=static_cast<BYTE*>(self);m[0x2C8]=1;m[0x2C9]=1;}
 TickEventBridge();eventOriginalVisibility(self);TickPalmontFacilities();
}
static int EventFault(EXCEPTION_POINTERS* p){Log("EVENT_BRIDGE_FAULT code=%08X address=%p",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress);return EXCEPTION_EXECUTE_HANDLER;}
#ifdef W2C_PUBLIC_BUILD
static int(__thiscall* eventOriginalMapIcons)(void*,EventIcon**,void*);
// The two native map consumers accept 200 pointers. Supply stable, map-only
// records to their enumeration rather than spending gameplay icon slots or
// adding facility triggers/VLT data to the player's installation.
static std::array<EventIcon,8> palmontMapIcons=[]{
 std::array<EventIcon,8> result{};unsigned index=0;
 for(const auto& facility:palmontFacilities)if(facility.shared)for(unsigned short type:{8,10}){
  const auto p=facility.position;
  result[index++]={type,3,-1,-1,nullptr,nullptr,{p.z,-p.x,p.y},0,0};
 }
 return result;
}();
static int __fastcall EventMapIcons(void* self,void*,EventIcon** output,void* player){
 const int native=eventOriginalMapIcons(self,output,player);
 if(!output||native<0||native>200||!eventBridgeActive||!loaderActive||propNativeReference||
    !palmont_facility::Roaming())return native;
 int count=native;
 for(auto& marker:palmontMapIcons){
  // Stock visibility updates restore the two low flags on native icons
  // after pursuit. Our stable records are outside that list.
  const auto manager=static_cast<const BYTE*>(self);
  const bool visible=marker.type==8?manager[0x2CF]!=0:manager[0x2CB]!=0;
  if(!visible)continue;
  marker.flags|=3;
  bool duplicate=false;
  for(int i=0;i<native&&!duplicate;++i)if(output[i]&&output[i]->type==marker.type){
   const auto d=Sub(output[i]->position,marker.position);duplicate=d.x*d.x+d.y*d.y<12.f*12.f;
  }
  if(!duplicate&&count<200)output[count++]=&marker;
 }
 if(worldLogEnabled){static int previous=-1;if(count-native!=previous){previous=count-native;Log("PUBLIC_PALMONT_MAP_ICONS native=%d added=%d capacity=200 stock_slots_used=0",native,previous);}}
 return count;
}
static void InitPalmontMapIcons(){
 const BYTE guard[]={0x81,0xEC,0x4C,0x06,0,0,0xA1};
 auto target=reinterpret_cast<void*>(0x5E4940);
 if(memcmp(target,guard,sizeof(guard))||MH_CreateHook(target,EventMapIcons,reinterpret_cast<void**>(&eventOriginalMapIcons))!=MH_OK){Log("PUBLIC_PALMONT_MAP_ICONS_REFUSED native enumeration");return;}
 if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);Log("PUBLIC_PALMONT_MAP_ICONS_REFUSED hook enable");}
}
#endif
static bool WantsPalmontBreakerMarkers(){
#ifdef W2C_PUBLIC_BUILD
 return sourceCarbonPursuit;
#else
 return true;
#endif
}
static void InitEventBridge(){
#ifdef W2C_PUBLIC_BUILD
 if(v2Store.bundle().find(61,"native/barrier-meshes"))LoadSourceRaceBarriers();
#else
 LoadRaceBarriers();
#endif
 const BYTE load[]={0x8B,0x54,0x24,0x04,0x81,0x3A,0x46,0x41,0x03,0x00};
 const BYTE spawn[]={0x56,0x8B,0xF1,0x66,0x8B,0x06};
 const BYTE enable[]={0x56,0x8B,0xF1,0x8A,0x46,0x02,0xC0,0xE8};
 if(memcmp(reinterpret_cast<void*>(0x7270B0),load,sizeof(load))||memcmp(reinterpret_cast<void*>(0x5EC270),spawn,sizeof(spawn))||memcmp(reinterpret_cast<void*>(0x5E5BB0),enable,sizeof(enable))){Log("EVENT_BRIDGE_REFUSED ABI");return;}
 if(sourceRuntime){if(WantsPalmontBreakerMarkers())try{using namespace w2v2disk;auto bytes=SourceBytes(61,"native/pursuit-markers");need(bytes.size()==1744&&r32(bytes,0)==0x34146&&r32(bytes,4)==1736&&zeros(bytes,8,8),"source pursuit marker envelope");for(unsigned i=0;i<36;++i){unsigned at=16+i*48;need(zeros(bytes,at,8)&&r32(bytes,at+8)==0xec90a22a&&r32(bytes,at+32)==2600&&r32(bytes,at+36)==0,"source pursuit marker fields");for(unsigned j=0;j<3;++j){float p;memcpy(&p,bytes.data()+at+16+j*4,4);need(std::isfinite(p),"source pursuit marker position");}}eventMarkerChunk=VirtualAlloc(nullptr,bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);need(eventMarkerChunk!=nullptr,"source pursuit marker allocation");memcpy(eventMarkerChunk,bytes.data(),bytes.size());}catch(const std::exception& e){++v2Failures;Log("SOURCE_PURSUIT_MARKERS_REFUSED reason=%s",e.what());}}
 else{FILE* f=nullptr;fopen_s(&f,Path("Environment/pursuit-markers.bin").c_str(),"rb");
 // Race icons do not depend on the optional Palmont pursuit-marker pack.
 if(f){fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 if(n!=1744){fclose(f);Log("EVENT_BRIDGE_REFUSED marker size=%ld",n);return;}
 eventMarkerChunk=VirtualAlloc(nullptr,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 bool ok=eventMarkerChunk&&fread(eventMarkerChunk,1,n,f)==size_t(n);fclose(f);
 if(!ok||*reinterpret_cast<unsigned*>(eventMarkerChunk)!=0x34146){if(eventMarkerChunk)VirtualFree(eventMarkerChunk,0,MEM_RELEASE);eventMarkerChunk=nullptr;return;}}}
 eventDiagnostics=worldLogEnabled&&GetPrivateProfileIntA("Probe","EventVisualDiagnostics",0,Path("W2CMWv2Bootstrap.ini").c_str())!=0;
 const BYTE visibility[]={0x83,0xEC,0x18,0x53,0x55,0x57,0x8B,0xF9};auto target=reinterpret_cast<void*>(0x5FDAE0);
 if(memcmp(target,visibility,sizeof(visibility))||MH_CreateHook(target,EventVisibility,reinterpret_cast<void**>(&eventOriginalVisibility))!=MH_OK||MH_EnableHook(target)!=MH_OK){Log("EVENT_BRIDGE_REFUSED visibility hook");return;}
 eventBridgeActive=true;Log("EVENT_BRIDGE_ACTIVE source_breaker_markers=%u diagnostics=%u",eventMarkerChunk?36u:0u,eventDiagnostics);
#ifdef W2C_PUBLIC_BUILD
 InitPalmontMapIcons();
#endif

}
static void TickEventBridgeImpl(){
 if(!eventBridgeActive||*reinterpret_cast<int*>(0x925E90)!=6)return;
 TraceRaceGrid();
 auto manager=*reinterpret_cast<BYTE**>(0x91E00C);if(!manager||!manager[0x118])return;
 DWORD now=GetTickCount();if(now-eventLastTick<100)return;eventLastTick=now;
 if(eventMarkerChunk&&!eventMarkerRegistered){eventMarkerRegistered=reinterpret_cast<int(__cdecl*)(void*)>(0x7270B0)(eventMarkerChunk)!=0;Log("EVENT_MARKERS_REGISTERED result=%u",eventMarkerRegistered);}
 unsigned count=*reinterpret_cast<unsigned*>(manager+0x2BC);auto icons=*reinterpret_cast<EventIcon***>(manager+0x2C4);if(count>200||!icons)return;
 // Stock and owned race speed traps share the native race's live list. Never
 // infer them from checkpoint icon type: sprint checkpoints use the same type.
 // The checkpoint extension appends storage and preserves these native offsets.
 std::set<EventIcon*> speedIcons;
 auto race=NFSPluginSDK::MW05::GRaceStatus::Get();
 if(race&&race->mPlayMode==NFSPluginSDK::MW05::GRaceStatus::PlayMode::Racing&&race->nSpeedTraps>0&&race->nSpeedTraps<=16)
  for(int i=0;i<race->nSpeedTraps;++i){auto trigger=reinterpret_cast<BYTE*>(race->aSpeedTraps[i]);if(trigger&&*reinterpret_cast<unsigned*>(trigger)==0x8A3A84){auto icon=*reinterpret_cast<EventIcon**>(trigger+0xC0);if(icon)speedIcons.insert(icon);}}
 unsigned breakers=0,hiding=0,nearby=0,models=0,emitters=0,breakerMap=0,fieldModels=0,fieldEmitters=0;
 unsigned speedModels=0,speedEmitters=0,speedVisible=0,freeSpeedModels=0;
 const bool freeRoam=race&&race->mPlayMode==NFSPluginSDK::MW05::GRaceStatus::PlayMode::Roaming;
 for(unsigned n=0;n<count;++n){auto icon=icons[n];if(!icon)continue;
  const bool speed=(icon->type==13||icon->type==14)&&speedIcons.count(icon);
  // Type 13 is a camera in both the native freeroam milestone list and a
  // Speedtrap race. Type 14 by itself also describes ordinary checkpoints.
  const bool freeSpeed=freeRoam&&icon->type==13;
  if(!speed&&!freeSpeed&&(icon->type<1||icon->type>12||icon->type==5))continue;
  if(icon->type==12){++breakers;if(icon->flags&2)++breakerMap;}else if(icon->type==11)++hiding;
  // Carbon uses zero height for hiding regions. Native MW's downward-only
  // snap cannot reach Palmont ground above zero. Resolve that sentinel from
  // the converted floor BVH before handing the icon to the native spawner.
  if(icon->type==11&&icon->position.z==0&&!(icon->flags&32)){
   RayHit ground;if(collision.Ray({-icon->position.y,1000,icon->position.x},{-icon->position.y,-1000,icon->position.x},ground,true)){
    icon->position.z=ground.point.y;icon->flags|=32;Log("EVENT_HIDE_HEIGHT x=%.3f y=%.3f z=%.3f",icon->position.x,icon->position.y,icon->position.z);
   }
  }
  Vec3 d=Sub(icon->position,cameraPosition);bool resident=Dot(d,d)<220.f*220.f;
  if(eventDiagnostics&&icon->type>=11)icon->flags|=3;
  // Native source section state is unavailable after replacing the renderer.
  // Do not change position, icon type, or the native pursuit/cooldown condition.
  if(resident){++nearby;const auto* info=reinterpret_cast<const unsigned*>(0x8F5A58)+3*icon->type;
   if(!(icon->flags&4)||(info[1]&&!icon->model)||(info[2]&&!icon->emitter)){
    auto beforeModel=icon->model,beforeEmitter=icon->emitter;
    reinterpret_cast<void(__thiscall*)(void*)>(0x5EC270)(icon);
    // Native visibility can mark an icon enabled even if its resource wasn't
    // resident yet. Reapply visibility to newly created parts on this tick.
    if(beforeModel!=icon->model||beforeEmitter!=icon->emitter)icon->flags&=~8u;
   }
   if(speed){icon->flags|=1;reinterpret_cast<void(__thiscall*)(void*)>(0x5E5BB0)(icon);}
  }else if(icon->flags&4)reinterpret_cast<void(__thiscall*)(void*)>(0x5E5A00)(icon);
  if(icon->model)++models;if(icon->emitter)++emitters;
  if(icon->type<11){fieldModels+=icon->model!=nullptr;fieldEmitters+=icon->emitter!=nullptr;}
  if(freeSpeed&&icon->model)++freeSpeedModels;
  if(speed){speedModels+=icon->model!=nullptr;speedEmitters+=icon->emitter!=nullptr;speedVisible+=icon->model&&static_cast<BYTE*>(icon->model)[0x28]!=0;

  }
 }
 if(worldLogEnabled&&now-eventLastLog>3000){eventLastLog=now;Log("EVENT_ICONS total=%u breakers=%u breaker_map=%u hiding=%u nearby=%u models=%u emitters=%u field_models=%u field_emitters=%u pursuit_shown=%u hiding_shown=%u speed_points=%u speed_models=%u speed_emitters=%u speed_visible=%u freeroam_camera_models=%u native_speed_list=1",count,breakers,breakerMap,hiding,nearby,models,emitters,fieldModels,fieldEmitters,manager[0x2C8],manager[0x2C9],unsigned(speedIcons.size()),speedModels,speedEmitters,speedVisible,freeSpeedModels);}
}
static void TickEventBridge(){__try{TickEventBridgeImpl();}__except(EventFault(GetExceptionInformation())){eventBridgeActive=false;}}
// Retain only native race barrier entries from the game's culled draw list.
// The original draw function still applies its native visibility/pass flags.
// The list contains native model, transform and instance pointers: no guessed
// event placement or permanent geometry is added to the replacement world.
struct EventDrawEntry {unsigned model;void* matrix;void* instance;};
static_assert(sizeof(EventDrawEntry)==12);
// Game's GIcon::EffectInfo model hashes at 008F5A58, restricted to the
// free-roam effects serviced above and the active race's speedtrap models.
static bool NativeFieldEffectModel(unsigned hash){switch(hash){
 case 0x9E535570:case 0x3A583CA3:case 0xC8E0972E:case 0x734A6A5E:
 case 0x3232E2F8:case 0x4C471D8E:case 0x9E3223D3:case 0x85C25E95:
 case 0x868C2E14:case 0xD2592F60:case 0xC1FD605B:return true;
 default:return false;
}}
static std::vector<EventDrawEntry> eventBarrierDraws;
static void SubmitNativeEventBarrierList(void* self,BYTE* group,void* view,void* flags){
 auto first=*reinterpret_cast<void**>(group+0x88),end=*reinterpret_cast<void**>(group+0x8C);
 __try{
  *reinterpret_cast<void**>(group+0x88)=eventBarrierDraws.data();
  *reinterpret_cast<void**>(group+0x8C)=eventBarrierDraws.data()+eventBarrierDraws.size();
  nativeOriginalDraw(self,view,flags);
 }__finally{
  *reinterpret_cast<void**>(group+0x88)=first;
  *reinterpret_cast<void**>(group+0x8C)=end;
 }
}
static void DrawEventBarriers(void* self,void* view,void* flags){
 if(!nativeOriginalDraw||*reinterpret_cast<int*>(0x925E90)!=6)return;
 auto group=static_cast<BYTE*>(self);unsigned views=*reinterpret_cast<unsigned*>(group+0x9C0);
 if(views>32)return;
 for(unsigned v=0;v<views;v++,group+=0xD0){if(*reinterpret_cast<void**>(group+0x80)!=view)continue;
  auto first=*reinterpret_cast<EventDrawEntry**>(group+0x88),end=*reinterpret_cast<EventDrawEntry**>(group+0x8C);
  auto bytes=reinterpret_cast<uintptr_t>(end)-reinterpret_cast<uintptr_t>(first);
  if(!first||!end||end<first||bytes%sizeof(EventDrawEntry)||bytes>1200000)return;
  eventBarrierDraws.clear();
  for(auto entry=first;entry<end;++entry){
   auto model=reinterpret_cast<BYTE*>(entry->model&~3u);if(!model||!entry->instance)continue;
   unsigned hash=*reinterpret_cast<unsigned*>(model+8);
   // Native GIcon::EffectInfo types 13/14 both bind C1FD605B. Keep its
   // actual culled model entry alongside barriers instead of discarding it
   // with the replaced scenery. Native transforms/materials remain intact.
   const bool stockWall=hash==0x54A4FC3F||hash==0x54B22557;
#ifdef W2C_PUBLIC_BUILD
   // The replacement renderer draws stock walls once from native activation
   // groups. EventLoader's exported draw is separate and still runs normally.
   if((stockWall&&!raceBarriersLoaded)||NativeFieldEffectModel(hash))eventBarrierDraws.push_back(*entry);
#else
   if(stockWall||NativeFieldEffectModel(hash))eventBarrierDraws.push_back(*entry);
#endif
  }
  if(!eventBarrierDraws.empty())SubmitNativeEventBarrierList(self,group,view,flags);
  return;
 }
}
static void TraceEventBarrierCull(void* self,void* view){
 if(!worldLogEnabled)return;
 if(view!=reinterpret_cast<void*>(0x919650)||*reinterpret_cast<int*>(0x925E90)!=6)return;
 static DWORD last=0;DWORD now=GetTickCount();if(now-last<3000)return;last=now;
 __try{
  auto data=static_cast<BYTE*>(self);unsigned views=*reinterpret_cast<unsigned*>(data+0x9C0);
  if(views>32)return;
  for(unsigned v=0;v<views;v++,data+=0xD0){if(*reinterpret_cast<void**>(data+0x80)!=view)continue;
   auto first=*reinterpret_cast<BYTE**>(data+0x88),end=*reinterpret_cast<BYTE**>(data+0x8C);
   if(!first||!end||end<first||(end-first)%12||end-first>1200000)return;unsigned matched=0;
   for(auto entry=first;entry<end;entry+=12){
    auto model=reinterpret_cast<BYTE*>(*reinterpret_cast<unsigned*>(entry)&~3u);if(!model)continue;
    unsigned hash=*reinterpret_cast<unsigned*>(model+8);
    if(hash!=0x54A4FC3F&&hash!=0x54B22557)continue;
    auto instance=*reinterpret_cast<BYTE**>(entry+8);auto matrix=*reinterpret_cast<D3DMATRIX**>(entry+4);++matched;
    if(matched<=4)Log("EVENT_BARRIER_CULL model=%p solid=%08X instance=%p flags=%08X matrix=%p at=%.3f,%.3f,%.3f",model,hash,instance,instance?*reinterpret_cast<unsigned*>(instance+0x18):0,matrix,matrix?matrix->_41:0,matrix?matrix->_42:0,matrix?matrix->_43:0);
   }
   Log("EVENT_BARRIER_CULL_SUMMARY records=%u player_barriers=%u",unsigned((end-first)/12),matched);return;
  }
 }__except(EventFault(GetExceptionInformation())){}
}
