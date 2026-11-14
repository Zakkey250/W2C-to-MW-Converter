#pragma once
#include <NFSPluginSDK/Game.MW05/Types/IPlayer.h>
#include <NFSPluginSDK/Game.MW05/Types/FEngHud.h>
#include <NFSPluginSDK/Game.MW05/Types/Minimap.h>
#include <NFSPluginSDK/Game.MW05/Types/TrackInfo.h>
#include "WorldRadarPolicy.h"
struct WorldRadarEntry {unsigned identity=0;world_radar::State state;};
static std::map<NFSPluginSDK::MW05::TrackInfo*,WorldRadarEntry> worldRadarStates;
// The full W2C atlas belongs to the world loader. Its calibration must also
// work when an optional Probe cannot attach to another MOD's frame observer.
static bool WorldRadarReadable(const void* pointer,size_t bytes){
 if(!pointer)return false;MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(pointer,&region,sizeof(region))||region.State!=MEM_COMMIT||(region.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 uintptr_t begin=uintptr_t(pointer),end=uintptr_t(region.BaseAddress)+region.RegionSize;return begin<=end&&bytes<=end-begin;
}
static void TickWorldRadarUnsafe(){
 if(editorStock||!loaderActive||*reinterpret_cast<unsigned*>(0x925E90)!=6)return;
 auto race=*reinterpret_cast<BYTE**>(0x91E000);if(!WorldRadarReadable(race,0x1964))return;
 unsigned mode=*reinterpret_cast<unsigned*>(race+0x1960);
 using namespace NFSPluginSDK::MW05;
 auto count=*reinterpret_cast<unsigned*>(0x92D884);auto players=*reinterpret_cast<IPlayer***>(0x92D87C);
 if(!count||count>8||!WorldRadarReadable(players,count*4)||!WorldRadarReadable(players[0],4))return;
 auto hud=static_cast<FEngHud*>(players[0]->GetHud());if(!WorldRadarReadable(hud,sizeof(FEngHud)))return;
 auto map=static_cast<Minimap*>(hud->pMinimap);if(!WorldRadarReadable(map,sizeof(Minimap)))return;
 auto track=map->CurrentTrack;if(!WorldRadarReadable(track,sizeof(TrackInfo))||track->TrackNumber<0||track->TrackNumber>9999)return;
 auto manager=*reinterpret_cast<BYTE**>(0x91CEFC);if(!WorldRadarReadable(manager,72))return;
 char prefix[65]{};memcpy(prefix,manager+8,64);
 auto source=world_radar::Identify(mode,prefix);
 unsigned identity=unsigned(track->TrackNumber);for(unsigned i=0;i<64;++i)identity=identity*33+unsigned(reinterpret_cast<unsigned char*>(track)[i]);
 auto& entry=worldRadarStates[track];if(entry.identity!=identity){entry={};entry.identity=identity;}
 world_radar::Calibration current{track->TrackMapCalibrationUpperLeft.x,track->TrackMapCalibrationUpperLeft.y,track->TrackMapCalibrationMapWidthMetres,track->TrackMapCalibrationRotation},previous=current;
 if(!std::isfinite(current.x)||!std::isfinite(current.y)||!std::isfinite(current.width)||current.width<1)return;
 if(!entry.state.Update(source,current))return;
 track->TrackMapCalibrationUpperLeft.x=current.x;track->TrackMapCalibrationUpperLeft.y=current.y;track->TrackMapCalibrationMapWidthMetres=current.width;track->TrackMapCalibrationRotation=current.rotation;
 Log("WORLD_RADAR_SOURCE_CALIBRATION prefix=%s mode=%u source=%s track=%d old=%.4f,%.4f width=%.4f new=%.4f,%.4f width=%.4f original_restored=%u",prefix,mode,source==world_radar::Source::WorldRoaming?"W2C_ROAMING":"NATIVE_COURSE",int(track->TrackNumber),previous.x,previous.y,previous.width,current.x,current.y,current.width,unsigned(source!=world_radar::Source::WorldRoaming));
}
static void TickWorldRadar(){__try{TickWorldRadarUnsafe();}__except(EXCEPTION_EXECUTE_HANDLER){static DWORD last=0;auto now=GetTickCount();if(now-last>5000){last=now;Log("WORLD_RADAR_PENDING fault=%08X",unsigned(GetExceptionCode()));}}}
