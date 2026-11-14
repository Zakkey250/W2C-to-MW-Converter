#pragma once
#include "../../source-format/NativeSequences.h"
static bool sourceSequencesReady=false;
static bool LoadSourceSequences(){try{
 using namespace w2v2disk;
 auto sequences=SourceBytes(61,"native/sequences");auto names=w2source::validateNativeSequences(sequences);
 auto cameras=SourceBytes(61,"native/cameras");auto scenes=w2source::validateNativeCameras(cameras);
 auto markers=SourceBytes(61,"native/camera-markers");auto markerCount=w2source::validateNativeMarkers(markers,scenes);
 auto plan=parse(SourceBytes(61,"native/sequence-routes"),256*1024);
 need(uintv(plan["schema"])==1&&plan["engines"].is_array()&&plan["engines"].size()<=4096&&plan["cameraScenes"].is_array(),"source sequence route schema/count");
 std::map<unsigned,unsigned> routes;std::set<unsigned> targets,plannedScenes;
 for(const auto& r:plan["engines"]){unsigned from=unsigned(uintv(r["sourceHash"])),to=unsigned(uintv(r["targetHash"]));need(from!=to&&names.count(to)&&names.at(to)==str(r["target"],128)&&routes.emplace(from,to).second&&targets.insert(to).second,"source sequence route identity");}
 need(targets.size()==names.size(),"source sequence route closure");
 for(const auto& r:plan["cameraScenes"]){auto name=str(r["name"],128);unsigned hash=unsigned(uintv(r["hash"]));need(name.rfind("w2v2_cam_",0)==0&&w2source::cameraNameHash(name)==hash&&plannedScenes.insert(hash).second,"source camera private namespace");}need(plannedScenes==scenes,"source camera route closure");
 const BYTE sequenceGuard[]={0x56,0x8b,0x74,0x24,0x08,0x8b,0x06,0x3d,0x11,0xb8,0x03,0,0x57};
 const BYTE cameraGuard[]={0x51,0x55,0x56,0x8b,0x74,0x24,0x10,0x8b};
 const BYTE createGuard[]={0x51,0x8b,0x44,0x24,0x10,0x8d,0x4c,0x24,0x10};
 const BYTE closestGuard[]={0x8b,0x51,0x14,0x83,0xec,0x08};
 need(!memcmp(reinterpret_cast<void*>(0x648210),sequenceGuard,sizeof(sequenceGuard))&&!memcmp(reinterpret_cast<void*>(0x4787c0),cameraGuard,sizeof(cameraGuard))&&!memcmp(reinterpret_cast<void*>(0x6459e0),createGuard,sizeof(createGuard))&&!memcmp(reinterpret_cast<void*>(0x4491e0),closestGuard,sizeof(closestGuard)),"source sequence/camera native guards");
 for(unsigned scene:scenes)need(!reinterpret_cast<int(__thiscall*)(void*,unsigned)>(0x46e710)(reinterpret_cast<void*>(0x8ee320),scene),"source camera namespace already registered");
 need(InitCarbonBackground(),"source functional state-entry bridge");
 auto create=reinterpret_cast<void*>(0x6459e0),closest=reinterpret_cast<void*>(0x4491e0);
 need(MH_CreateHook(create,CarbonBreakerCreate,reinterpret_cast<void**>(&carbonBreakerOriginalCreate))==MH_OK,"source engine route hook");
 need(MH_CreateHook(closest,CarbonBreakerClosestMarker,reinterpret_cast<void**>(&carbonBreakerOriginalClosestMarker))==MH_OK,"source camera placement hook");
#ifdef W2C_PUBLIC_BUILD
 auto cameraEvent=reinterpret_cast<void*>(0x641520);
 auto applyEvent=reinterpret_cast<void*>(0x63B120);
 const BYTE cameraEventGuard[]={0x6A,0xFF,0x68,0xFF,0x97,0x87,0x00};
 const BYTE applyGuard[]={0x6A,0xFF,0x68,0xAB,0x94,0x87,0x00};
 need(!memcmp(cameraEvent,cameraEventGuard,sizeof(cameraEventGuard))&&
      MH_CreateHook(cameraEvent,CarbonBreakerNIS,reinterpret_cast<void**>(&carbonBreakerOriginalNIS))==MH_OK,
      "source camera event hook");
 need(!memcmp(applyEvent,applyGuard,sizeof(applyGuard))&&
      MH_CreateHook(applyEvent,CarbonBreakerApplyExplosion,reinterpret_cast<void**>(&carbonBreakerOriginalApplyExplosion))==MH_OK,
      "source pursuit explosion hook");
#endif
 auto retain=[](const B& b){auto p=static_cast<BYTE*>(VirtualAlloc(nullptr,b.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));need(p!=nullptr,"source native sequence allocation");memcpy(p,b.data(),b.size());return p;};
 // Registration fixes pointers in place and retains them for process life.
 carbonBreakerPack=retain(sequences);need(reinterpret_cast<int(__cdecl*)(void*)>(0x648210)(carbonBreakerPack)!=0,"source native sequence registration");
 carbonMomentCameraPack=retain(cameras);reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4787c0)(reinterpret_cast<void*>(0x8ee320),carbonMomentCameraPack);
 for(unsigned scene:scenes)need(reinterpret_cast<int(__thiscall*)(void*,unsigned)>(0x46e710)(reinterpret_cast<void*>(0x8ee320),scene)==1,"source native camera registration");
 static_assert(sizeof(CarbonMomentMarker)==52);carbonMomentMarkers.resize(markerCount);memcpy(carbonMomentMarkers.data(),markers.data()+16,markerCount*52);
 sourceEngineRoutes=std::move(routes);sourceCameraScenes=std::move(scenes);carbonBreakerFamilies=true;carbonBreakerMoments=true;
 need(MH_EnableHook(create)==MH_OK&&MH_EnableHook(closest)==MH_OK,"source functional hook enable");
#ifdef W2C_PUBLIC_BUILD
 need(MH_EnableHook(cameraEvent)==MH_OK&&MH_EnableHook(applyEvent)==MH_OK,"source camera/explosion hook enable");
 if(propPursuitDiagnostics){
  const BYTE eventGuard[]={0x51,0x8B,0x0D,0x90,0x03,0x92,0x00};
  const BYTE momentGuard[]={0x8B,0x0D,0x90,0x03,0x92,0x00};
  auto trace=[](uintptr_t address,const BYTE* guard,size_t bytes,void* replacement,void** original){
   auto target=reinterpret_cast<void*>(address);
   if(memcmp(target,guard,bytes)||MH_CreateHook(target,replacement,original)!=MH_OK)return;
   if(MH_EnableHook(target)!=MH_OK)MH_RemoveHook(target);
  };
  trace(0x6318c0,eventGuard,sizeof(eventGuard),reinterpret_cast<void*>(CarbonBreakerExplosion),reinterpret_cast<void**>(&carbonBreakerOriginalExplosion));
  trace(0x632080,momentGuard,sizeof(momentGuard),reinterpret_cast<void*>(CarbonBreakerMoment),reinterpret_cast<void**>(&carbonBreakerOriginalMoment));
 }
#endif
 sourceSequencesReady=true;
 Log("SOURCE_SEQUENCES_ACTIVE aliases=%u cameras=%u markers=%u quarantined_original_fragment=0A8471EC logging_required=0",unsigned(names.size()),unsigned(sourceCameraScenes.size()),markerCount);return true;
 }catch(const std::exception& e){++v2Failures;W2V2Status.error=1;Log("SOURCE_SEQUENCES_REFUSED reason=%s",e.what());return false;}}
