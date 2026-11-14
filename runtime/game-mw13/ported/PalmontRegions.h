#pragma once
#include "WorldVehiclePosition.h"
#include "PalmontRoadPolicy.h"
static std::vector<palmont_road::Segment> palmontRoads;
static void LoadPalmontRoads(){
 if(sourceRuntime){LoadSourceRoads();return;}
 char path[MAX_PATH];strcpy_s(path,root);strcat_s(path,"World2MW-police-roads.bin");FILE* f=nullptr;fopen_s(&f,path,"rb");if(!f){Log("PALMONT_ROADS_UNAVAILABLE missing_pack");return;}
 unsigned h[4]{};bool ok=fread(h,sizeof(h),1,f)==1&&h[0]==0x44523257&&h[1]==1&&h[2]>0&&h[2]<=20000&&h[3]==sizeof(palmont_road::Segment);
 if(ok){palmontRoads.resize(h[2]);ok=fread(palmontRoads.data(),sizeof(palmont_road::Segment),h[2],f)==h[2]&&fgetc(f)==EOF;}
 fclose(f);
 for(const auto& s:palmontRoads){ok=ok&&std::isfinite(s.ax)&&std::isfinite(s.ay)&&std::isfinite(s.az)&&std::isfinite(s.bx)&&std::isfinite(s.by)&&std::isfinite(s.bz)&&s.voice<=101&&s.area>=0x0CF9C3C9&&s.area<=0x0CF9C3CC;}
 if(!ok){palmontRoads.clear();Log("PALMONT_ROADS_REFUSED invalid_pack");return;}
 Log("PALMONT_ROADS_ACTIVE city_segments=%u position_source=physical_player named_voice_available=0",unsigned(palmontRoads.size()));
}
// Carbon's exact FE area triangulations, converted to the native track plane.
// Queries outside those meshes retain MW's existing GRegionManager behavior.
struct PalmontTriangle {float x[3],y[3];};
struct PalmontArea {unsigned hash;float minX,minY,maxX,maxY;std::vector<PalmontTriangle> triangles;};
static std::vector<PalmontArea> palmontAreas;
static unsigned palmontCurrentHash=0,palmontPendingHash=0;
static DWORD palmontPendingTime=0,palmontRadioTime=0,palmontRadioAttempt=0;
static bool palmontPursuit=false;
static void* (__thiscall* palmontRegionQueryOriginal)(void*,float*,int,void*);
static void (__thiscall* palmontNeighborhoodOriginal)(void*);
static int (__cdecl* palmontSpeechOriginal)(unsigned,void*,void*,void*,void*);
static bool PalmontContains(const PalmontTriangle& t,float x,float y){
 const double area=(double(t.x[1])-t.x[0])*(double(t.y[2])-t.y[0])-(double(t.y[1])-t.y[0])*(double(t.x[2])-t.x[0]);
 if(std::abs(area)<.001)return false; // FE meshes also contain zero-area padding triangles.
 const double a=(double(t.x[1])-t.x[0])*(y-t.y[0])-(double(t.y[1])-t.y[0])*(x-t.x[0]);
 const double b=(double(t.x[2])-t.x[1])*(y-t.y[1])-(double(t.y[2])-t.y[1])*(x-t.x[1]);
 const double c=(double(t.x[0])-t.x[2])*(y-t.y[2])-(double(t.y[0])-t.y[2])*(x-t.x[2]);
 return !((a<-.001||b<-.001||c<-.001)&&(a>.001||b>.001||c>.001));
}
static unsigned PalmontLookup(float x,float y){
 if(!std::isfinite(x)||!std::isfinite(y))return 0;
 for(auto& a:palmontAreas){
  if(x<a.minX||x>a.maxX||y<a.minY||y>a.maxY)continue;
  for(auto& t:a.triangles)if(PalmontContains(t,x,y))return a.hash;
 }
 return 0;
}
// A native GRegion-compatible record. The native type-11 consumers read its
// localization hash at +0x30. Keep a valid polygon/size for other native users.
struct PalmontRegionRecord {unsigned type;float center[2],direction[2],radius;unsigned flags,reserved;float bounds[4];unsigned hash,data[3];unsigned short count,size;float polygon[6];};
static_assert(sizeof(PalmontRegionRecord)==92);
static std::array<PalmontRegionRecord,4> palmontRecords{};
static void* __fastcall PalmontRegionQuery(void* self,void*,float* p,int type,void* previous){
 if(type==11&&p){unsigned h=PalmontLookup(p[0],p[1]);
  for(unsigned i=0;i<palmontAreas.size();++i)if(palmontAreas[i].hash==h)return &palmontRecords[i];
  // A synthetic previous record must never enter the stock manager's caches.
  for(auto& r:palmontRecords)if(previous==&r){previous=nullptr;break;}
 }
 return palmontRegionQueryOriginal(self,p,type,previous);
}
static bool PalmontHash(unsigned h){for(auto& a:palmontAreas)if(a.hash==h)return true;return false;}
static bool PalmontActivePursuit(){
 // Same guarded native surface used by ArmsAssist. LocalPlayer's last-pursuit
 // handle is historical and must not be treated as an active pursuit flag.
 auto manager=*reinterpret_cast<BYTE**>(0x993CC8);if(!manager)return false;
 auto pursuit=*reinterpret_cast<BYTE**>(manager+0x130);if(!pursuit)return false;
 auto table=*reinterpret_cast<unsigned**>(pursuit);unsigned address=unsigned(table);
 if(address<0x889000||address>=0x900000||table[3]!=0x433C90||table[0x114/4]!=0x433B60)return false;
 int status=*reinterpret_cast<int*>(pursuit+0x218);return status==0||status==1;
}
static void __fastcall PalmontNeighborhood(void* self,void*){
 palmontNeighborhoodOriginal(self);
 unsigned h=*reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+0x70);
 palmontPursuit=PalmontActivePursuit();
 if(h!=palmontCurrentHash){
  unsigned previous=palmontCurrentHash;palmontCurrentHash=h;
  palmontPendingHash=PalmontHash(h)&&palmontPursuit?h:0;palmontPendingTime=GetTickCount();
  Log("PALMONT_REGION_TRANSITION previous=%08X current=%08X pursuit=%u radio_pending=%u",previous,h,palmontPursuit,palmontPendingHash!=0);
 }
 if(!palmontPursuit)palmontPendingHash=0;
}
#include "../integration/MWNativeSpeechPolicy.h"
static bool PalmontSpeechRead(const void* from,void* to,size_t size){__try{memcpy(to,from,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool PalmontSpeechEventNameEquals(const void* event,const char* expected){
 const char* name=nullptr;if(!event||!expected||!PalmontSpeechRead(event,&name,sizeof(name))||!name)return false;
 // Event declarations are native game objects. Compare through the guarded read
 // surface instead of retaining or directly dereferencing their name pointer.
 for(size_t i=0;i<64;++i){char actual=0;if(!PalmontSpeechRead(name+i,&actual,1))return false;
  if(actual!=expected[i])return false;if(!actual)return true;
 }
 return false;
}
static int __cdecl PalmontSpeech(unsigned bytes,void* data,void* event,void* registration,void* source){
 if(!mw_native_speech::Available(event,PalmontSpeechRead)){
  static unsigned missing=0;if(missing++<16)Log("NATIVE_SPEECH_EVENT_UNAVAILABLE event=%p bytes=%u reason=empty_or_unreadable_name native_dispatch_called=0",event,bytes);
  return 0;
 }
 DWORD now=GetTickCount();
 int result=0;
 bool handledLocation=false;
 if(PalmontSpeechEventNameEquals(event,"Setup_LocationReport")||PalmontSpeechEventNameEquals(event,"LocationReport")){
  Vec3 position{};
  if(*reinterpret_cast<unsigned*>(0x925E90)==6&&W2CReadPlayerPosition(position)){
   auto road=palmont_road::Locate(palmontRoads,position.x,position.y,position.z);
   unsigned area=PalmontLookup(position.z,-position.x);if(!area&&road.found)area=road.area;
   if(area){
    // Use a fresh physical position, not the cached HUD/Rockport name. Carbon
    // Japanese has no 0x0072 named-road bank: never send its IDs to MW's bank.
    handledLocation=true;result=1;
    static unsigned previousArea=0,previousRoad=~0u;static DWORD previousTime=0;
    bool changed=area!=previousArea||(road.found&&road.voice!=previousRoad);
    unsigned speaker=0;
    if(changed&&(!previousTime||now-previousTime>=20000)&&source&&bytes>=4&&PalmontSpeechRead(data,&speaker,sizeof(speaker))){
     auto declaration=reinterpret_cast<void*>(0x901EBC);unsigned params[2]={speaker,1};
     if(mw_native_speech::Available(declaration,PalmontSpeechRead)){
      int queued=palmontSpeechOriginal(sizeof(params),params,declaration,reinterpret_cast<void*>(0x9923BC),source);
      if(queued){previousArea=area;previousRoad=road.found?road.voice:~0u;previousTime=now;palmontPendingHash=0;palmontRadioTime=now;}
      Log("PALMONT_LOCATION_RESOLVED area=%08X carbon_road=%u segment=%u distance=%.2f native_fallback=DispJurisShift queued=%d",area,road.voice,road.segment,road.distance,queued);
     }
    }
   }
  }
 }
 if(!handledLocation)result=palmontSpeechOriginal(bytes,data,event,registration,source);
 if(worldLogEnabled){
  // Observe through the existing Speech owner instead of installing a second
  // detour in Event Loader. The exported role contract is already used by
  // the racer controller; no audio request is created by this diagnostic.
  auto module=GetModuleHandleW(L"MWEventLoader.asi");
  auto scope=module?reinterpret_cast<unsigned(__cdecl*)(unsigned)>(GetProcAddress(module,"MWEL_RpdAIScopeV1")):nullptr;
  unsigned race=0,suspect=0;
  if(scope&&PalmontSpeechRead(reinterpret_cast<void*>(0x91E000),&race,4)&&race&&
     PalmontSpeechRead(reinterpret_cast<void*>(race+0x18+0x194),&suspect,4)&&suspect&&scope(suspect)){
   char name[80]{};const char* text=nullptr;
   if(PalmontSpeechRead(event,&text,sizeof(text))&&text)for(unsigned i=0;i<sizeof(name)-1;++i){if(!PalmontSpeechRead(text+i,name+i,1)||!name[i])break;}
   Log("RPD_NATIVE_SPEECH_REQUEST name=%s bytes=%u result=%d native_dispatch=1",name,bytes,result);
  }
 }
 // Feed the existing native mixer/priority/dispatcher queue using a context
 // from this current native call. Never retain a radio entity pointer, never
 // use a separate WAV player, and never announce in free roam without pursuit.
 if(palmontPendingHash&&palmontPursuit&&palmontPendingHash==palmontCurrentHash&&source&&data&&bytes>=4
    &&event!=reinterpret_cast<void*>(0x901EBC)&&now-palmontPendingTime<15000
    &&(!palmontRadioAttempt||now-palmontRadioAttempt>=1000)
    &&(!palmontRadioTime||now-palmontRadioTime>=15000)&&*reinterpret_cast<int*>(0x925E90)==6){
  // Selector 1 is emitted by the native jurisdiction caller at 0071D475.
  unsigned params[2]={*static_cast<unsigned*>(data),1};unsigned h=palmontPendingHash;palmontRadioAttempt=now;
  auto declaration=reinterpret_cast<void*>(0x901EBC);
  int queued=mw_native_speech::Available(declaration,PalmontSpeechRead)?palmontSpeechOriginal(sizeof(params),params,declaration,reinterpret_cast<void*>(0x9923BC),source):0;
  if(queued){palmontPendingHash=0;palmontRadioTime=now;}Log("PALMONT_REGION_RADIO region=%08X native_event=DispJurisShift result=%d",h,queued);
 }
 if(palmontPendingHash&&now-palmontPendingTime>=15000)palmontPendingHash=0;
 return result;
}
static bool InstallPalmontRegions(){
 LoadPalmontRoads();
 bool valid=true;if(sourceRuntime){valid=LoadSourceRegions();}else{
 char path[MAX_PATH];strcpy_s(path,root);strcat_s(path,"World2MW-regions.bin");FILE* f=nullptr;fopen_s(&f,path,"rb");if(!f){Log("PALMONT_REGIONS_UNAVAILABLE missing_pack");return false;}
 unsigned header[3];valid=fread(header,4,3,f)==3&&header[0]==0x47523257&&header[1]==1&&header[2]==4;
 for(unsigned i=0;valid&&i<4;++i){unsigned h,n;valid=fread(&h,4,1,f)==1&&fread(&n,4,1,f)==1&&n>0&&n<2048;if(!valid)break;
  PalmontArea a{};a.hash=h;a.minX=a.minY=1e9f;a.maxX=a.maxY=-1e9f;
  for(unsigned j=0;valid&&j<n;++j){float p[6];valid=fread(p,4,6,f)==6;PalmontTriangle t{};
   for(unsigned k=0;k<3&&valid;++k){valid=std::isfinite(p[k*2])&&std::isfinite(p[k*2+1]);t.x[k]=p[k*2];t.y[k]=p[k*2+1];a.minX=std::min(a.minX,t.x[k]);a.maxX=std::max(a.maxX,t.x[k]);a.minY=std::min(a.minY,t.y[k]);a.maxY=std::max(a.maxY,t.y[k]);}
   a.triangles.push_back(t);
  }
  if(valid){auto& r=palmontRecords[i];r.type=11;r.hash=h;r.center[0]=(a.minX+a.maxX)*.5f;r.center[1]=(a.minY+a.maxY)*.5f;r.direction[1]=1;r.bounds[0]=a.minX;r.bounds[1]=a.minY;r.bounds[2]=a.maxX;r.bounds[3]=a.maxY;r.count=3;r.size=92;
   auto& t=a.triangles.front();for(unsigned k=0;k<3;++k){r.polygon[k*2]=t.x[k];r.polygon[k*2+1]=t.y[k];}palmontAreas.push_back(std::move(a));}
 }
 valid=valid&&fgetc(f)==EOF;fclose(f);}if(!valid){palmontAreas.clear();Log("PALMONT_REGIONS_REFUSED pack_format");return false;}
 const BYTE q[]={0x51,0x8B,0x44,0x24,0x0C,0x6B,0xC0,0x4C},n[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x14},s[]={0x64,0xA1,0,0,0,0,0x6A,0xFF,0x68,0x81,0,0x88,0};
 void* addresses[]={reinterpret_cast<void*>(0x727480),reinterpret_cast<void*>(0x6EE360),reinterpret_cast<void*>(0x713B20)};
 if(memcmp(addresses[0],q,sizeof(q))||memcmp(addresses[1],n,sizeof(n))||memcmp(addresses[2],s,sizeof(s))){Log("PALMONT_REGIONS_REFUSED executable_guard");return false;}
 auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
 void* hooks[]={PalmontRegionQuery,PalmontNeighborhood,PalmontSpeech};void** originals[]={reinterpret_cast<void**>(&palmontRegionQueryOriginal),reinterpret_cast<void**>(&palmontNeighborhoodOriginal),reinterpret_cast<void**>(&palmontSpeechOriginal)};
 unsigned created=0;for(;created<3;++created)if(MH_CreateHook(addresses[created],hooks[created],originals[created])!=MH_OK)break;
 bool active=created==3;for(unsigned i=0;active&&i<3;++i)active=MH_EnableHook(addresses[i])==MH_OK;
 if(!active){for(unsigned i=0;i<created;++i){MH_DisableHook(addresses[i]);MH_RemoveHook(addresses[i]);}Log("PALMONT_REGIONS_REFUSED hook_transaction");return false;}
 unsigned count=0;for(auto& a:palmontAreas)count+=unsigned(a.triangles.size());
 Log("PALMONT_REGIONS_ACTIVE areas=4 triangles=%u native_hud=1 native_radio_queue=1 translated_strings=34 stock_queries_preserved=1",count);return true;
}
