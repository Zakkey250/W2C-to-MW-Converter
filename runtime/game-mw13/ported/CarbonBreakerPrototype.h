#pragma once
// Converted pursuit-breaker bridge. Explicit configuration is required; absent
// or invalid values leave it OFF. Existing MW engines remain registered.
static bool CarbonBreakerFlag(const char* file,const char* section,const char* key){
 char value[8]{};GetPrivateProfileStringA(section,key,"0",value,sizeof(value),Path(file).c_str());return !strcmp(value,"1");
}
static bool carbonBreakerInstalled=false,carbonBreakerLab=false;
static void* carbonBreakerPack=nullptr;
static bool carbonBreakerTried=false,carbonBreakerPrototype=false;
static DWORD carbonBreakerReadyAt=0;
static bool carbonBreakerTriggered=false;
static bool carbonBreakerAutoTrigger=false;
#include "CarbonBreakerPoliceTest.h"
#include "CarbonBreakerFamilies.generated.h"
#include "CarbonBreakerMoments.h"
#include "CarbonBreakerRoute.h"
static void* (__cdecl* carbonBreakerOriginalCreate)(void*,void*,unsigned,float,float);
static std::map<unsigned,unsigned> sourceEngineRoutes;
static bool CarbonFamilyPlacement(const NativeProp* p){
 const float* position=reinterpret_cast<const float*>(p->record+16);
 for(auto& site:carbonFamilySites){
  float dx=site.x-position[2],dy=site.y+position[0],dz=site.z-position[1];
  if(dx*dx+dy*dy+dz*dz<=150.f*150.f)return true;
 }
 return false;
}
static void* __cdecl CarbonBreakerCreate(void* object,void* context,unsigned name,float time,float rate){
 auto p=propConstructing?propConstructing:propFactorySelected;
 if(carbonBreakerFamilies&&!p&&context)for(auto& owner:propModelOwners){auto candidate=owner.second;if(candidate->spawner&&context==static_cast<BYTE*>(candidate->spawner)+0x40&&object==static_cast<BYTE*>(candidate->spawner)+0x10){p=candidate;break;}}
 if(sourceRuntime){
  if(p
#ifdef W2C_PUBLIC_BUILD
     &&propPursuitParameters.count(*reinterpret_cast<unsigned*>(p->record+32))
#endif
  ){
   auto route=sourceEngineRoutes.find(name);
   if(route!=sourceEngineRoutes.end()){
    if(propPursuitDiagnostics)Log("SOURCE_PURSUIT_ENGINE id=%u source=%08X target=%08X",p->id,name,route->second);
    name=route->second;
   }
  }
  return carbonBreakerOriginalCreate(object,context,name,time,rate);
 }
 if(p&&carbonBreakerFamilies){
  bool booth=CarbonBreakerCraneBoothRoute(p->id,name,*reinterpret_cast<unsigned*>(p->record+32),*reinterpret_cast<unsigned*>(p->record+36),*reinterpret_cast<unsigned*>(p->record+40));
  for(auto& route:carbonFamilyRoutes)if(name==route.source&&(name!=0x919191C6||booth)){
   if(route.native&&!booth&&!CarbonFamilyPlacement(p)){if(worldLogEnabled)Log("CARBON_PB_NATIVE_ROUTE_PRESERVED id=%u engine=%08X",p->id,name);break;}
   name=route.target;if(worldLogEnabled)Log("CARBON_PB_FAMILY_ROUTE_ACTIVE id=%u original=%08X target=%08X",p->id,route.source,route.target);break;
  }
 }else if(p&&CarbonBreakerCraneBoothRoute(p->id,name,*reinterpret_cast<unsigned*>(p->record+32),*reinterpret_cast<unsigned*>(p->record+36),*reinterpret_cast<unsigned*>(p->record+40))){
  name=0x7AC8CCEE;Log("CARBON_PB_ENGINE_ROUTE_ACTIVE id=%u original=tollbooth_seq target=w2mw_cb_tollbooth_seq",p->id);
 }
 return carbonBreakerOriginalCreate(object,context,name,time,rate);
}
static void* CarbonBreakerModel(unsigned handle,unsigned& id){
 // gEventDynamicData stores HMODEL, not IModel*. The EN1.3 Instanceable
 // constructor (006B0C30) writes the handle at IModel+8. Inspect only live
 // interfaces already tracked by the prop bridge; never dereference a handle.
 id=0xFFFFFFFF;
 if(!handle)return nullptr;
 for(auto& p:propModelOwners)if(*reinterpret_cast<unsigned*>(static_cast<BYTE*>(p.first)+8)==handle){id=p.second->id;return p.first;}
 for(auto& p:propPayloadPieces)if(*reinterpret_cast<unsigned*>(static_cast<BYTE*>(p.first)+8)==handle){id=p.second->id;return p.first;}
 return nullptr;
}
static unsigned CarbonBreakerEventOwner(){
 unsigned id;
 CarbonBreakerModel(*reinterpret_cast<unsigned*>(0x91F1C4),id);
 return id;
}
static void CarbonBreakerSceneAvailability(const char* name){
 auto candidate=*reinterpret_cast<BYTE**>(0x90D9D0);unsigned scene=reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x460BF0)(name);
 int type=-1;unsigned count=candidate?*reinterpret_cast<unsigned*>(candidate+0x14):0;
 if(count>64){Log("CARBON_PB_NIS_AVAILABILITY_REFUSED candidate_count=%u",count);return;}
 for(unsigned i=0;i<count;i++)if(*reinterpret_cast<unsigned*>(candidate+0xC9C+i*8)==scene)type=*reinterpret_cast<int*>(candidate+0xCA0+i*8);
 const char* marker=reinterpret_cast<const char*(__cdecl*)(int)>(0x445630)(type);
 unsigned markerHash=reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x460BF0)(marker),found=0,visited=0;
 auto head=reinterpret_cast<BYTE*>(0x9AA494),node=*reinterpret_cast<BYTE**>(head);
 for(;node&&node!=head&&visited<16384;node=*reinterpret_cast<BYTE**>(node),++visited)if(*reinterpret_cast<unsigned*>(node+8)==markerHash)++found;
 Log("CARBON_PB_NIS_AVAILABILITY_ACTIVE name=%s scene=%08X candidate_count=%u marker_type=%d marker_name=%s markers=%u visited=%u",name,scene,count,type,marker,found,visited);
}
static void* (__thiscall* carbonBreakerOriginalClosestMarker)(void*,unsigned,void*,int*,float*,unsigned short);
static void* __fastcall CarbonBreakerClosestMarker(void* self,void*,unsigned scene,void* position,int* id,float* distance,unsigned short angle){
 auto result=CarbonMomentClosest(scene,position,id,distance,angle);
 if(!result)result=carbonBreakerOriginalClosestMarker(self,scene,position,id,distance,angle);
 Log("CARBON_PB_NIS_MARKER_LOOKUP_ACTIVE scene=%08X result=%p distance=%.3f",scene,result,result&&distance?*distance:-1.f);return result;
}
static void CarbonBreakerText(unsigned address,char* out){
 unsigned i=0;__try{for(;i<63;i++){char c=reinterpret_cast<const char*>(address)[i];out[i]=c;if(!c)return;}out[63]=0;}
 __except(EXCEPTION_EXECUTE_HANDLER){sprintf_s(out,64,"<unreadable:%08X>",address);}
}
static void (__cdecl* carbonBreakerOriginalMoment)(const void*);
static void __cdecl CarbonBreakerMoment(const void* data){
 if(worldLogEnabled&&data){auto p=static_cast<const unsigned*>(data);char name[64]{};CarbonBreakerText(p[1],name);Log("CARBON_PB_NIS_REQUEST_ACTIVE kind=moment id=%u name=%s model=%p",CarbonBreakerEventOwner(),name,*reinterpret_cast<void**>(0x91F1C4));if(name[0]!='<')CarbonBreakerSceneAvailability(name);}
 carbonBreakerOriginalMoment(data);
}
static void (__cdecl* carbonBreakerOriginalNIS)(const void*);
#ifdef W2C_PUBLIC_BUILD
static bool InitialPlayerCenter(Vec3&);
static DWORD sourceMomentPlayedAt=0;

static bool IsPublicCarbonMoment(const unsigned* args){
 const auto name=reinterpret_cast<const char*>(args[2]);
 const auto type=reinterpret_cast<const char*>(args[3]);
 if(!name||!type||strcmp(type,"Moment")||
    !CarbonMomentScene(reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x460BF0)(name)))
  return false;
 return true;
}
static bool PublicCarbonMomentEligible(unsigned id){
 if(id>=nativeProps.size())return false;
 const auto& prop=*nativeProps[id];
 if(!prop.nativeTouched||!propPursuitParameters.count(*reinterpret_cast<const unsigned*>(prop.record+32))||
    !W2MW_IsWorldActiveV1()||*reinterpret_cast<void**>(0x9885C8))
  return false;
 auto options=*reinterpret_cast<BYTE**>(0x91CF90);
 auto profile=options?*reinterpret_cast<BYTE**>(options+0x10):nullptr;
 auto manager=*reinterpret_cast<void**>(0x92C67C);
 bool active=manager&&*reinterpret_cast<unsigned*>(manager)==0x891900&&
  reinterpret_cast<bool(__thiscall*)(void*)>(0x426D10)(manager);
 Vec3 player{};
 const auto raw=reinterpret_cast<const float*>(prop.record+16);
 const bool foundPlayer=InitialPlayerCenter(player);
 const auto offset=Sub(player,{raw[2],-raw[0],raw[1]});
 const bool nearby=foundPlayer&&Dot(offset,offset)<50.f*50.f;
 Log("PUBLIC_PURSUIT_MOMENT_CHECK id=%u enabled=%u pursuit=%u nearby=%u",
     id,unsigned(profile&&profile[0x47]),unsigned(active),unsigned(nearby));
 if(!profile||!profile[0x47]||!active||!nearby||
    reinterpret_cast<bool(__thiscall*)(void*)>(0x469120)(options)||*reinterpret_cast<BYTE*>(0x911055))
  return false;
 auto race=*reinterpret_cast<BYTE**>(0x91E000);
 auto event=race?*reinterpret_cast<void**>(race+0x1968):nullptr;
 if(event&&!reinterpret_cast<bool(__thiscall*)(void*)>(0x5FBE70)(event))return false;
 return !sourceMomentPlayedAt||GetTickCount()-sourceMomentPlayedAt>=5000;
}
#endif
static void __cdecl CarbonBreakerNIS(const void* data){
 if(worldLogEnabled&&data){auto p=static_cast<const unsigned*>(data);char name[64]{},type[64]{};CarbonBreakerText(p[2],name);CarbonBreakerText(p[3],type);Log("CARBON_PB_NIS_REQUEST_ACTIVE kind=play id=%u name=%s type=%s marker=%08X",CarbonBreakerEventOwner(),name,type,p[1]);}
#ifdef W2C_PUBLIC_BUILD
 // Carbon's event order can omit the stock two-second activation timestamp.
 // Supply only that timestamp. Never force the camera: MW checks that a live
 // police vehicle targeting this player is inside its native distance cone.
 if(data&&IsPublicCarbonMoment(static_cast<const unsigned*>(data))){
  const auto id=CarbonBreakerEventOwner();
  if(!PublicCarbonMomentEligible(id))return;
  auto& trigger=*reinterpret_cast<float*>(0x90D9D4);
  auto& bypass=*reinterpret_cast<bool*>(0x90D9D8);
  const float previous=trigger;
  const bool previousBypass=bypass;
  trigger=reinterpret_cast<float(__cdecl*)()>(0x6E8DE0)();
  bypass=false;
  __try{carbonBreakerOriginalNIS(data);}
  __finally{trigger=previous;bypass=previousBypass;}
  const bool played=*reinterpret_cast<void**>(0x9885C8)!=nullptr;
  if(played)sourceMomentPlayedAt=GetTickCount();
  Log("PUBLIC_PURSUIT_MOMENT_RESULT id=%u played=%u native_police_gate=1",id,unsigned(played));
  return;
 }
 carbonBreakerOriginalNIS(data);
#else
 // Isolate converted camera validity from the native pursuit/cooldown gate.
 // This opt-in lab diagnostic is never read by installed mode or logging OFF.
 bool force=carbonBreakerLab&&carbonBreakerFamilies&&worldLogEnabled&&data&&GetPrivateProfileIntA("Probe","TestMode",0,Path("W2CMWv2Bootstrap.ini").c_str())==1&&CarbonBreakerFlag("W2CMWv2Bootstrap.ini","Probe","CarbonBreakerCameraGateTest");
 if(force){auto p=static_cast<const unsigned*>(data);const char* name=reinterpret_cast<const char*>(p[2]);force=name&&CarbonMomentScene(reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x460BF0)(name));}
 if(force){bool saved=*reinterpret_cast<bool*>(0x90D9D8);*reinterpret_cast<bool*>(0x90D9D8)=true;carbonBreakerOriginalNIS(data);*reinterpret_cast<bool*>(0x90D9D8)=saved;Log("CARBON_PB_CAMERA_GATE_TEST_ACTIVE diagnostic_only=1");}
 else carbonBreakerOriginalNIS(data);
#endif
}
static void (__cdecl* carbonBreakerOriginalArea)(const void*);
static void __cdecl CarbonBreakerArea(const void* data){
 if(worldLogEnabled&&data){auto p=static_cast<const unsigned*>(data);Log("CARBON_PB_AREA_ACTIVE id=%u system=%08X stimulus=%08X radius=%.3f",CarbonBreakerEventOwner(),p[1],p[2],reinterpret_cast<const float*>(p)[4]);}
 carbonBreakerOriginalArea(data);
}
static void (__cdecl* carbonBreakerOriginalExplosion)(const void*);
static void __cdecl CarbonBreakerExplosion(const void* data){
 if(worldLogEnabled&&data){auto p=static_cast<const unsigned*>(data);Log("CARBON_PB_EXPLOSION_ACTIVE position=%08X speed=%.3f radius=%.3f flags=%u,%u,%u,%u,%u id=%u",p[1],reinterpret_cast<const float*>(p)[2],reinterpret_cast<const float*>(p)[3],p[5],p[6],p[7],p[8],p[9],CarbonBreakerEventOwner());}
 carbonBreakerOriginalExplosion(data);
}
#ifdef W2C_PUBLIC_BUILD
static void (__thiscall* carbonBreakerOriginalApplyExplosion)(void*);
static void __fastcall CarbonBreakerApplyExplosion(void* self,void*){
 auto raw=static_cast<BYTE*>(self);
 auto markerField=reinterpret_cast<unsigned*>(raw+8);
 const auto marker=*markerField;
 const auto values=reinterpret_cast<const float*>(raw+0xC);
 const auto flags=reinterpret_cast<const unsigned*>(raw+0x18);
 const bool mwProfile=values[2]==0.f&&flags[0]==1&&flags[1]==0&&flags[2]==0&&flags[3]==1&&flags[4]==0&&
     ((values[0]==10.f&&values[1]==16.f)||(values[0]==17.f&&values[1]==15.f));
 if(!mwProfile&&!(worldLogEnabled&&propPursuitDiagnostics)){carbonBreakerOriginalApplyExplosion(self);return;}
 unsigned id=0;
 auto model=CarbonBreakerModel(*reinterpret_cast<const unsigned*>(raw+0x2C),id);
 bool useOrigin=false;
 if(model){
  const auto originKey=*reinterpret_cast<const unsigned*>(0x91DDBC);
  const bool origin=marker==originKey;
  auto vtable=*reinterpret_cast<void***>(model);
  auto graphics=origin?nullptr:reinterpret_cast<void*(__thiscall*)(void*)>(vtable[16])(model);
  const bool found=origin||(graphics&&reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(0x620C60)(graphics,marker));
  // Native MW drops the entire explosion when a fragment model lacks the
  // Carbon marker. Use the same prop's native origin for adapted blasts.
  useOrigin=mwProfile&&!found&&id<nativeProps.size()&&
      propPursuitParameters.count(*reinterpret_cast<const unsigned*>(nativeProps[id]->record+32));
  if(useOrigin)*markerField=originKey;
  if(worldLogEnabled&&propPursuitDiagnostics)
   Log("PUBLIC_PURSUIT_EXPLOSION_RESOLVE id=%u marker=%08X resolved=%u origin=%u fallback=%u speed=%.4f outer=%.4f inner=%.4f tick=%.5f",
       id,marker,unsigned(found),unsigned(origin),unsigned(useOrigin),values[0],values[1],values[2],reinterpret_cast<float(__cdecl*)()>(0x6E8DA0)());
 }
 __try{carbonBreakerOriginalApplyExplosion(self);}
 __finally{if(useOrigin)*markerField=marker;}
}
#endif
static bool CarbonBreakerDigest(const BYTE* data,unsigned size){
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[32]{};DWORD length=32;
 bool ok=CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,data,size,0)&&CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0);
 char hex[65]{};for(unsigned i=0;i<32;i++)sprintf_s(hex+i*2,3,"%02x",digest[i]);
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
 return ok&&(carbonBreakerFamilies?(size==carbonFamilyPackBytes&&!strcmp(hex,carbonFamilyPackSha)):((size==5256&&!strcmp(hex,"44819270c741c4950b633f5cdca1c995498bed10ed172ffc717ca0bf230b862d"))||(size==7896&&!strcmp(hex,"23aa9c02df82dbbf9b2758329f79efa63547a8b4890ae089497d8e0f6ab8dd99"))));
}
#include "SourceSequences.h"
static void LoadCarbonBreakerPrototype(){
 if(sourceRuntime){if(!carbonBreakerTried){carbonBreakerTried=true;LoadSourceSequences();}return;}
 if(carbonBreakerTried)return;carbonBreakerTried=true;
 carbonBreakerLab=CarbonBreakerFlag("W2CMWv2Bootstrap.ini","Probe","CarbonBreakerPrototype");
 carbonBreakerInstalled=CarbonBreakerFlag("World2MWCarbonBreakers.ini","World","Enabled");
 if(!carbonBreakerLab&&!carbonBreakerInstalled)return;
 carbonBreakerFamilies=(carbonBreakerLab&&CarbonBreakerFlag("W2CMWv2Bootstrap.ini","Probe","CarbonBreakerAllFamilies"))||(carbonBreakerInstalled&&CarbonBreakerFlag("World2MWCarbonBreakers.ini","World","AllFamilies"));
 const BYTE guard[]={0x56,0x8B,0x74,0x24,0x08,0x8B,0x06,0x3D,0x11,0xB8,0x03,0x00,0x57};
 if(memcmp(reinterpret_cast<void*>(0x648210),guard,sizeof(guard))){Log("CARBON_PB_REFUSED native loader guard");return;}
 FILE* file=nullptr;fopen_s(&file,Path(carbonBreakerFamilies?"PursuitPayload/carbon-families-sequences-mw.bun":"PursuitPayload/carbon-sequences-mw.bun").c_str(),"rb");if(!file){Log("CARBON_PB_REFUSED missing pack");return;}
 fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
 BYTE* data=(carbonBreakerFamilies?size==carbonFamilyPackBytes:(size==5256||size==7896))?static_cast<BYTE*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)):nullptr;
 bool valid=data&&fread(data,1,size,file)==unsigned(size)&&fgetc(file)==EOF;fclose(file);
 valid=valid&&CarbonBreakerDigest(data,unsigned(size));
 if(!valid){if(data)VirtualFree(data,0,MEM_RELEASE);Log("CARBON_PB_REFUSED pack identity");return;}
 if(carbonBreakerFamilies&&!InitCarbonBackground()){VirtualFree(data,0,MEM_RELEASE);carbonBreakerFamilies=false;Log("CARBON_PB_REFUSED state-entry bridge");return;}
 if(!reinterpret_cast<int(__cdecl*)(void*)>(0x648210)(data)){VirtualFree(data,0,MEM_RELEASE);Log("CARBON_PB_REFUSED registration");return;}
 // Native tag/engine pointers reference this allocation for the process life.
 carbonBreakerPack=data;carbonBreakerPrototype=true;
 LoadCarbonBreakerMoments();
 Log("CARBON_PB_ACTIVE engines=%u existing_engines_replaced=0 pack_bytes=%ld",carbonBreakerFamilies?unsigned(std::size(carbonFamilyRoutes)):(size==5256?3u:5u),size);
 carbonBreakerAutoTrigger=carbonBreakerLab&&GetPrivateProfileIntA("Probe","TestMode",0,Path("W2CMWv2Bootstrap.ini").c_str())==1&&CarbonBreakerFlag("W2CMWv2Bootstrap.ini","Probe","CarbonBreakerAutoTrigger");
 char policeTest[8]{};GetPrivateProfileStringA("Probe","CarbonBreakerPoliceTest","0",policeTest,sizeof(policeTest),Path("W2CMWv2Bootstrap.ini").c_str());
 carbonBreakerPoliceTest=carbonBreakerLab&&worldLogEnabled&&GetPrivateProfileIntA("Probe","TestMode",0,Path("W2CMWv2Bootstrap.ini").c_str())==1&&!strcmp(policeTest,"1");
 carbonBreakerPoliceAnchor=GetPrivateProfileIntA("Probe","CarbonBreakerPoliceAnchor",23814,Path("W2CMWv2Bootstrap.ini").c_str());if(carbonBreakerPoliceAnchor!=23814&&carbonBreakerPoliceAnchor!=23740&&!(carbonBreakerFamilies&&carbonBreakerPoliceAnchor<100000))carbonBreakerPoliceTest=false;
 auto create=reinterpret_cast<void*>(0x6459E0);const BYTE createGuard[]={0x51,0x8B,0x44,0x24,0x10,0x8D,0x4C,0x24,0x10};
 if(memcmp(create,createGuard,sizeof(createGuard))||MH_CreateHook(create,CarbonBreakerCreate,reinterpret_cast<void**>(&carbonBreakerOriginalCreate))!=MH_OK||MH_EnableHook(create)!=MH_OK){Log("CARBON_PB_REFUSED create route");return;}
 if(worldLogEnabled&&propPursuitDiagnostics){
  auto target=reinterpret_cast<void*>(0x6318C0);const BYTE callbackGuard[]={0x51,0x8B,0x0D,0x90,0x03,0x92,0x00};
  if(!memcmp(target,callbackGuard,sizeof(callbackGuard))&&MH_CreateHook(target,CarbonBreakerExplosion,reinterpret_cast<void**>(&carbonBreakerOriginalExplosion))==MH_OK)MH_EnableHook(target);
  const BYTE momentGuard[]={0x8B,0x0D,0x90,0x03,0x92,0x00};
  auto moment=reinterpret_cast<void*>(0x632080),area=reinterpret_cast<void*>(0x62FB90),nis=reinterpret_cast<void*>(0x641520);
  const BYTE nisGuard[]={0x6A,0xFF,0x68,0xFF,0x97,0x87,0x00};
  if(!memcmp(moment,momentGuard,sizeof(momentGuard))&&MH_CreateHook(moment,CarbonBreakerMoment,reinterpret_cast<void**>(&carbonBreakerOriginalMoment))==MH_OK)MH_EnableHook(moment);
  if(!memcmp(area,momentGuard,sizeof(momentGuard))&&MH_CreateHook(area,CarbonBreakerArea,reinterpret_cast<void**>(&carbonBreakerOriginalArea))==MH_OK)MH_EnableHook(area);
  if(!memcmp(nis,nisGuard,sizeof(nisGuard))&&MH_CreateHook(nis,CarbonBreakerNIS,reinterpret_cast<void**>(&carbonBreakerOriginalNIS))==MH_OK)MH_EnableHook(nis);
 }
 if(carbonBreakerMoments||(worldLogEnabled&&propPursuitDiagnostics)){
  auto closest=reinterpret_cast<void*>(0x4491E0);const BYTE closestGuard[]={0x8B,0x51,0x14,0x83,0xEC,0x08};
  if(!memcmp(closest,closestGuard,sizeof(closestGuard))&&MH_CreateHook(closest,CarbonBreakerClosestMarker,reinterpret_cast<void**>(&carbonBreakerOriginalClosestMarker))==MH_OK&&MH_EnableHook(closest)==MH_OK){}else{carbonBreakerMoments=false;Log("CARBON_PB_REFUSED moment marker hook");}
 }
}
static void TickCarbonBreakerPrototype(){
 TickCarbonBreakerPoliceTest();
 TraceCarbonMoment();
 if(!carbonBreakerPrototype||carbonBreakerTriggered||!carbonBreakerAutoTrigger)return;
 for(auto p:activeProps){
  if(p->id!=23814||!p->spawner||!p->initialized)continue;
  auto sequence=*reinterpret_cast<void**>(static_cast<BYTE*>(p->spawner)+0x6C);
  if(!sequence){Log("CARBON_PB_REFUSED missing runtime engine id=%u",p->id);carbonBreakerTriggered=true;return;}
  if(!carbonBreakerReadyAt)carbonBreakerReadyAt=GetTickCount();
  if(GetTickCount()-carbonBreakerReadyAt<8000)return;
  carbonBreakerTriggered=true;
  // Original droppedfromcrane_seq stimulus: drop_triggered (0x682CAF30).
  // Same IEngine slot, native simulation clock and QUEUE_ALLOW as
  // PhysicsObject::ProcessStimulus (00669860); do not pass an engine to the
  // System::ProcessStimulus entry point used by the diagnostic hook.
  auto vtable=*reinterpret_cast<void***>(sequence);
  float now=reinterpret_cast<float(__cdecl*)()>(0x6E8DE0)();
  bool accepted=reinterpret_cast<bool(__thiscall*)(void*,unsigned,float,void*,unsigned)>(vtable[13])(sequence,0x682CAF30,now,nullptr,2);
  Log("CARBON_PB_STIMULUS_ACTIVE id=%u accepted=%u sequence=%p",p->id,unsigned(accepted),sequence);
 }
}
