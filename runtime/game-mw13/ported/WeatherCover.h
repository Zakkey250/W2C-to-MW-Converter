#pragma once
#include "WeatherCoverPolicy.h"
#include "WeatherZonePolicy.h"
static std::vector<W2CWeatherZone> w2cWeatherZones;
static std::map<std::pair<int,int>,std::vector<unsigned>> w2cWeatherZoneGrid;
static float W2CSourceSunExposure(Vec3 renderPosition){
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
#ifdef W2C_PUBLIC_BUILD
static bool w2cPlayerInTunnel=false;
#endif
static void(__cdecl* w2cRainDrawOriginal)(bool);
static void InstallW2CRainDraw();
static void W2CUpdateWeatherCover(){
 __try{
  w2cSourceCovered=false;
#ifdef W2C_PUBLIC_BUILD
  w2cPlayerInTunnel=false;
#endif
  if(!W2MW_IsWorldActiveV1()||*reinterpret_cast<int*>(0x925E90)!=6)return;
  auto rain=*reinterpret_cast<BYTE**>(0x9196B8);
  auto camera=*reinterpret_cast<D3DMATRIX**>(0x919650);
  if(!rain||!camera)return;
  const auto& view=*camera;
  Vec3 renderEye{-(view._41*view._11+view._42*view._12+view._43*view._13),
   -(view._41*view._21+view._42*view._22+view._43*view._23),-(view._41*view._31+view._42*view._32+view._43*view._33)};
  Vec3 position{-renderEye.y,renderEye.z,renderEye.x};bool physicalPlayer=W2CReadPlayerPosition(position);
  // Chase cameras and the debug race camera may be outside a tunnel while
  // the actual player is inside it. Prefer the physical player position.
  auto cover=W2CSourceWeatherCover(w2cWeatherZones,position);
  bool sourceCovered=cover.roof;
  if(!cover.roof)cover=W2CWorldWeatherCover(collision,position);
  w2cSourceCovered=cover.roof;
#ifdef W2C_PUBLIC_BUILD
  w2cPlayerInTunnel=physicalPlayer&&cover.tunnel;
#endif
  // Rain layout from SunSet Rain.h: 0x8F integers precede these native flags.
  *reinterpret_cast<int*>(rain+0x23C)=cover.tunnel?1:0;
  *reinterpret_cast<int*>(rain+0x240)=cover.roof&&!cover.tunnel?1:0;
  static DWORD next=0;DWORD now=worldLogEnabled?GetTickCount():0;if(worldLogEnabled&&now>=next){next=now+2000;
   Log("W2C_WEATHER_COVER roof=%u tunnel=%u roof_height=%.3f position=%.3f,%.3f,%.3f physical_player=%u source_zone=%u native_flags_updated_before_SunSet=1",
    unsigned(cover.roof),unsigned(cover.tunnel),cover.roofHeight,position.x,position.y,position.z,unsigned(physicalPlayer),unsigned(sourceCovered));}
 }__except(EXCEPTION_EXECUTE_HANDLER){static bool reported=false;if(!reported){reported=true;Log("W2C_WEATHER_COVER_FAULT code=%08X",GetExceptionCode());}}
}
static void __cdecl W2CWeatherCull(void* cull){w2cWeatherCullOriginal(cull);W2CUpdateWeatherCover();if(W2MW_IsWorldActiveV1())InstallW2CRainDraw();}
static void __cdecl W2CRainDraw(bool reflection){
 // Native Rain::Update can overwrite the shelter flags after scenery culling.
 // Re-evaluate at the native particle compositor without changing wetness.
 W2CUpdateWeatherCover();
 if(w2cSourceCovered){static DWORD next=0;DWORD now=GetTickCount();if(now>=next){next=now+2000;Log("W2C_RAIN_DRAW_SUPPRESSED source_or_collision_cover=1 reflection=%u",unsigned(reflection));}return;}
 w2cRainDrawOriginal(reflection);
}
static void InstallW2CRainDraw(){
 // XboxRainDroplets discovers this original instruction signature during
 // ASI initialization. Install only once all ASIs have initialized.
 static bool tried=false;if(tried)return;tried=true;
 const BYTE rainGuard[]={0xA1,0xB8,0x96,0x91,0x00,0x8B,0x08,0x81,0xEC,0x8C,0x00,0x00,0x00};auto rainSite=reinterpret_cast<void*>(0x6D4620);
 if(memcmp(rainSite,rainGuard,sizeof(rainGuard))){Log("W2C_RAIN_DRAW_REFUSED ABI_guard");return;}
 if(MH_CreateHook(rainSite,W2CRainDraw,reinterpret_cast<void**>(&w2cRainDrawOriginal))!=MH_OK||MH_EnableHook(rainSite)!=MH_OK){Log("W2C_RAIN_DRAW_REFUSED hook");return;}
 Log("W2C_RAIN_DRAW_ACTIVE cdecl_bool=1 native_wetness_preserved=1 W2C_only=1");
}
static void InstallW2CWeatherCover(){
 LoadW2CWeatherZones();
 const BYTE guard[]={0x57,0x8B,0x7C,0x24,0x08};auto site=reinterpret_cast<void*>(0x6BFDD0);
 if(memcmp(site,guard,sizeof(guard))){Log("W2C_WEATHER_COVER_REFUSED ABI_guard");return;}
 if(MH_CreateHook(site,W2CWeatherCull,reinterpret_cast<void**>(&w2cWeatherCullOriginal))!=MH_OK)return;
 if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);return;}
 Log("W2C_WEATHER_COVER_ACTIVE native_rain_flags=023C,0240 SunSet_weather_preserved=1 W2C_only=1");
}
