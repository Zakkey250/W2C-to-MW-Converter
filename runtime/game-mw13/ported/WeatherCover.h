#pragma once
#include "WeatherCoverPolicy.h"
#include "../../format/RoadEnvironment.h"
static w2road::Environment authoredRoadEnvironment;
#include "WeatherZonePolicy.h"
static std::vector<W2CWeatherZone> w2cWeatherZones;
static std::map<std::pair<int,int>,std::vector<unsigned>> w2cWeatherZoneGrid;
static float W2CSourceSunExposure(Vec3 renderPosition){
 if(authoredRoadEnvironment.at({renderPosition.x,renderPosition.y,renderPosition.z}).tunnel)return 0;
 auto cell=w2cWeatherZoneGrid.find({int(std::floor(renderPosition.x/64)),int(std::floor(renderPosition.y/64))});
 if(cell==w2cWeatherZoneGrid.end())return 1;
 Vec3 physics{-renderPosition.y,renderPosition.z,renderPosition.x};
 for(auto index:cell->second){const auto& zone=w2cWeatherZones[index];
  if(physics.y>zone.elevation)continue;float x=physics.z,y=-physics.x;bool inside=false;
  for(unsigned i=0,j=unsigned(zone.polygon.size()-1);i<zone.polygon.size();j=i++){
   auto a=zone.polygon[j],b=zone.polygon[i];
   if((a[1]>y)!=(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])inside=!inside;}
  if(inside)return 0;
 }return 1;
}
static bool LoadW2CWeatherZones(){
 if(sourceRuntime){try{auto input=SourceInput(51,"environment/weather-zones");using namespace w2v2disk;need(input.get<unsigned>()==0x315a4d57&&input.get<unsigned>()==1,"source weather magic");auto n=input.get<unsigned>();need(n>0&&n<10000,"source weather count");std::vector<W2CWeatherZone> staged;std::map<std::pair<int,int>,std::vector<unsigned>> grid;U refs=0;
  for(unsigned i=0;i<n;++i){W2CWeatherZone z;z.type=input.get<unsigned>();auto points=input.get<unsigned>();z.elevation=input.get<float>();need(z.type>=3&&z.type<=5&&points>=3&&points<=128&&std::isfinite(z.elevation),"source weather fields");z.polygon.resize(points);input.read(z.polygon.data(),points*8);for(const auto& p:z.polygon)for(auto x:p)need(std::isfinite(x)&&std::abs(x)<100000,"source weather polygon");int a,b,c,d;need(W2CWeatherGridBounds(z,a,b,c,d),"source weather grid bounds");refs+=U(c-a+1)*U(d-b+1);need(refs<=1000000,"source weather grid references");for(int x=a;x<=c;++x)for(int y=b;y<=d;++y)grid[{x,y}].push_back(i);staged.push_back(std::move(z));}input.end();w2cWeatherZones=std::move(staged);w2cWeatherZoneGrid=std::move(grid);return true;
 }catch(const std::exception& error){Log("SOURCE_WEATHER_REFUSED reason=%s",error.what());return false;}}
 FILE* file=nullptr;fopen_s(&file,Path("Environment/weather-zones.bin").c_str(),"rb");if(!file)return false;
 unsigned header[3]{};bool valid=fread(header,4,3,file)==3&&header[0]==0x315A4D57&&header[1]==1&&header[2]>0&&header[2]<10000;
 std::vector<W2CWeatherZone> zones;
 for(unsigned i=0;valid&&i<header[2];++i){W2CWeatherZone zone;unsigned count=0;
  valid=fread(&zone.type,4,1,file)==1&&fread(&count,4,1,file)==1&&fread(&zone.elevation,4,1,file)==1;
  valid=valid&&(zone.type>=3&&zone.type<=5)&&count>=3&&count<=128&&std::isfinite(zone.elevation);
  if(!valid)break;zone.polygon.resize(count);valid=fread(zone.polygon.data(),8,count,file)==count;
  for(auto point:zone.polygon)for(float value:point)valid=valid&&std::isfinite(value)&&fabs(value)<100000;
  zones.push_back(std::move(zone));
 }
 valid=valid&&fgetc(file)==EOF;fclose(file);
 std::vector<std::array<int,4>> bounds;uint64_t references=0;
 for(const auto& zone:zones){int loX,loY,hiX,hiY;if(!W2CWeatherGridBounds(zone,loX,loY,hiX,hiY)){valid=false;break;}
  references+=uint64_t(hiX-loX+1)*uint64_t(hiY-loY+1);if(references>1000000){valid=false;break;}bounds.push_back({loX,loY,hiX,hiY});}
 if(valid){w2cWeatherZones=std::move(zones);w2cWeatherZoneGrid.clear();
  for(unsigned i=0;i<bounds.size();++i){auto b=bounds[i];for(int x=b[0];x<=b[2];++x)for(int y=b[1];y<=b[3];++y)w2cWeatherZoneGrid[{x,y}].push_back(i);}}
 Log("W2C_WEATHER_SOURCE_ZONES loaded=%u count=%u source=original_W2C_trackpaths gameplay_modified=0",unsigned(valid),unsigned(w2cWeatherZones.size()));return valid;
}
static void(__cdecl* w2cWeatherCullOriginal)(void*);
static bool w2cSourceCovered=false;
static bool w2cSourceTunnel=false;
#ifdef W2C_PUBLIC_BUILD
static bool w2cPlayerInTunnel=false;
#endif
static void(__cdecl* w2cRainDrawOriginal)(bool);
static void InstallW2CRainDraw();

static W2CWeatherCover W2CCoverAt(Vec3 position){
 W2CWeatherCover cover;
 const auto authored=authoredRoadEnvironment.at({position.z,-position.x,position.y});
 if(authored.tunnel)return {true,true,authored.roof};
 const float x=position.z,y=-position.x;
 if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(position.y)||
    std::abs(x)>=100000||std::abs(y)>=100000||std::abs(position.y)>=100000)return cover;
 const auto cell=w2cWeatherZoneGrid.find({int(std::floor(x/64)),int(std::floor(y/64))});
 if(cell!=w2cWeatherZoneGrid.end()){
  for(auto index:cell->second){
   const auto& zone=w2cWeatherZones[index];
   if(position.y>zone.elevation||!W2CPointInWeatherZone(zone,x,y))continue;
   cover.roofHeight=cover.roof?std::max(cover.roofHeight,zone.elevation):zone.elevation;
   cover.roof=true;
   cover.tunnel|=zone.type==3;
  }
 }
 // This BVH includes the active collision AddOn, including new tunnel roofs.
 return cover.roof?cover:W2CWorldWeatherCover(collision,position);
}

static void W2CUpdateWeatherCover(){
 __try{
  w2cSourceCovered=false;
  w2cSourceTunnel=false;
#ifdef W2C_PUBLIC_BUILD
  w2cPlayerInTunnel=false;
#endif
  if(!W2MW_IsWorldActiveV1())return;
  Vec3 position{};
  const bool physicalPlayer=W2CReadPlayerPosition(position);
  if(!physicalPlayer){
   const auto camera=*reinterpret_cast<D3DMATRIX**>(0x919650);
   if(!camera)return;
   const auto& view=*camera;
   position={view._41*view._21+view._42*view._22+view._43*view._23,
    -(view._41*view._31+view._42*view._32+view._43*view._33),
    -(view._41*view._11+view._42*view._12+view._43*view._13)};
  }
  static Vec3 lastPosition{FLT_MAX,FLT_MAX,FLT_MAX};
  static W2CWeatherCover cover;
  if(position.x!=lastPosition.x||position.y!=lastPosition.y||position.z!=lastPosition.z){
   cover=W2CCoverAt(position);
   lastPosition=position;
  }
  w2cSourceCovered=cover.roof;
  w2cSourceTunnel=cover.tunnel;
#ifdef W2C_PUBLIC_BUILD
  w2cPlayerInTunnel=physicalPlayer&&cover.tunnel;
#endif
  if(worldLogEnabled){
   static DWORD lastReport=0;static unsigned lastFlags=~0u;
   const auto now=GetTickCount();const unsigned flags=unsigned(cover.roof)|(unsigned(cover.tunnel)<<1);
   if(flags!=lastFlags||now-lastReport>=2000){
    lastReport=now;lastFlags=flags;
    Log("W2C_WEATHER_COVER roof=%u tunnel=%u roof_height=%.3f position=%.3f,%.3f,%.3f physical_player=%u",
     unsigned(cover.roof),unsigned(cover.tunnel),cover.roofHeight,position.x,position.y,position.z,unsigned(physicalPlayer));
   }
  }
 }__except(EXCEPTION_EXECUTE_HANDLER){static bool reported=false;if(!reported){reported=true;Log("W2C_WEATHER_COVER_FAULT code=%08X",GetExceptionCode());}}
}
static void(__thiscall* w2cRainPrepareOriginal)(BYTE*,float);
static int(__cdecl* w2cRainViewOriginal)(void*,int);
static BYTE* w2cWorldRain=nullptr;
static void* w2cRainParticleMaskOriginal=nullptr;
static const uintptr_t w2cRainParticleMaskSet=0x757D63;
static const uintptr_t w2cRainParticleMaskClear=0x757D69;

static void __fastcall W2CRainPrepare(BYTE* rain,void*,float delta){
 // The native shelter flags imply a valid native polygon at Rain+0x244.
 // Keep W2C cover independent and pass it only to the particle-mask branch.
 if(rain==*reinterpret_cast<BYTE**>(0x9196B8)){
  W2CUpdateWeatherCover();
  w2cWorldRain=W2MW_IsWorldActiveV1()?rain:nullptr;
  if(worldLogEnabled){
   static unsigned last=~0u;
   const bool nativeRoof=*reinterpret_cast<int*>(rain+0x23C)!=0||
                         *reinterpret_cast<int*>(rain+0x240)!=0;
   const unsigned flags=unsigned(w2cSourceCovered)|(unsigned(nativeRoof)<<1);
   if(flags!=last){last=flags;
    Log("W2C_RAIN_PARTICLE_COVER roof=%u native_roof=%u tunnel=%u native_zone=%p",
        unsigned(w2cSourceCovered),unsigned(nativeRoof),unsigned(w2cSourceTunnel),
        *reinterpret_cast<void**>(rain+0x244));}
  }
 }
 w2cRainPrepareOriginal(rain,delta);
}

static void __declspec(naked) W2CRainParticleMask(){
 __asm {
  cmp esi, dword ptr [w2cWorldRain]
  jne original
  cmp byte ptr [w2cSourceCovered], 0
  je outside
  jmp dword ptr [w2cRainParticleMaskSet]
 outside:
  jmp dword ptr [w2cRainParticleMaskClear]
 original:
  jmp dword ptr [w2cRainParticleMaskOriginal]
 }
}

static int __cdecl W2CRainView(void* view,int includeOverpass){
 if(view==reinterpret_cast<void*>(0x919650)&&W2MW_IsWorldActiveV1()){
  W2CUpdateWeatherCover();
  return includeOverpass?int(w2cSourceCovered):int(w2cSourceTunnel);
 }
 return w2cRainViewOriginal(view,includeOverpass);
}

static void InstallW2CRainCoverHooks(){
 auto install=[](uintptr_t address,const BYTE* guard,size_t size,void* detour,void** original){
  auto site=reinterpret_cast<void*>(address);
  if(memcmp(site,guard,size)||MH_CreateHook(site,detour,original)!=MH_OK)return false;
  if(MH_EnableHook(site)==MH_OK)return true;
  MH_RemoveHook(site);
  return false;
 };
 const BYTE prepareGuard[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x51,0x53,0x56,0x8B,0xF1,0x8B,0x8E,0x88,0x02,0x00,0x00};
 const BYTE maskGuard[]={0x39,0xBE,0x40,0x02,0x00,0x00,0x75,0x08,0x39,0xBE,0x3C,0x02,0x00,0x00,0x74,0x06,0x89,0x9E,0x34,0x02,0x00,0x00};
 const BYTE viewGuard[]={0x8B,0x44,0x24,0x04,0x8B,0x40,0x68,0x85,0xC0,0x74,0x1C,0x8B,0x4C,0x24,0x08};
 const bool prepare=install(0x753B50,prepareGuard,sizeof(prepareGuard),
  W2CRainPrepare,reinterpret_cast<void**>(&w2cRainPrepareOriginal));
 const bool mask=prepare&&install(0x757D53,maskGuard,sizeof(maskGuard),
  W2CRainParticleMask,&w2cRainParticleMaskOriginal);
 const bool view=install(0x73CFE0,viewGuard,sizeof(viewGuard),
  W2CRainView,reinterpret_cast<void**>(&w2cRainViewOriginal));
 Log("W2C_RAIN_COVER_HOOKS particle_prepare=%u view_query=%u particle_mask=%u",unsigned(prepare),unsigned(view),unsigned(mask));
}

static void __cdecl W2CWeatherCull(void* cull){w2cWeatherCullOriginal(cull);W2CUpdateWeatherCover();}
static void __cdecl W2CRainDraw(bool reflection){
 // Refresh cover at the particle compositor without changing road wetness.
 W2CUpdateWeatherCover();
 if(worldLogEnabled){
  static unsigned lastDecision=~0u;
  const auto decision=unsigned(w2cSourceCovered);
  if(decision!=lastDecision){lastDecision=decision;
   const auto rain=*reinterpret_cast<const BYTE**>(0x9196B8);
   Log("W2C_RAIN_DRAW cover=%u reflection=%u native_particle_mask=%d",
       decision,unsigned(reflection),rain?*reinterpret_cast<const int*>(rain+0x234):-1);}
 }
 if(w2cSourceCovered)return;
 w2cRainDrawOriginal(reflection);
}
static void InstallW2CRainDraw(){
 // XboxRainDroplets discovers this original instruction signature during
 // ASI initialization. Install only once all ASIs have initialized.
 static bool tried=false;if(tried)return;tried=true;
 InstallW2CRainCoverHooks();
 const BYTE rainGuard[]={0xA1,0xB8,0x96,0x91,0x00,0x8B,0x08,0x81,0xEC,0x8C,0x00,0x00,0x00};auto rainSite=reinterpret_cast<void*>(0x6D4620);
 if(memcmp(rainSite,rainGuard,sizeof(rainGuard))){Log("W2C_RAIN_DRAW_REFUSED ABI_guard");return;}
 if(MH_CreateHook(rainSite,W2CRainDraw,reinterpret_cast<void**>(&w2cRainDrawOriginal))!=MH_OK){Log("W2C_RAIN_DRAW_REFUSED hook");return;}
 if(MH_EnableHook(rainSite)!=MH_OK){MH_RemoveHook(rainSite);Log("W2C_RAIN_DRAW_REFUSED enable");return;}
 Log("W2C_RAIN_DRAW_ACTIVE cdecl_bool=1 native_wetness_preserved=1 W2C_only=1");
}
static void InstallW2CWeatherCover(){
 LoadW2CWeatherZones();
 const BYTE guard[]={0x57,0x8B,0x7C,0x24,0x08};auto site=reinterpret_cast<void*>(0x6BFDD0);
 if(memcmp(site,guard,sizeof(guard))){Log("W2C_WEATHER_COVER_REFUSED ABI_guard");return;}
 if(MH_CreateHook(site,W2CWeatherCull,reinterpret_cast<void**>(&w2cWeatherCullOriginal))!=MH_OK)return;
 if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);return;}
 Log("W2C_WEATHER_COVER_ACTIVE native_rain_flags_preserved=1 SunSet_weather_preserved=1 W2C_only=1");
}
