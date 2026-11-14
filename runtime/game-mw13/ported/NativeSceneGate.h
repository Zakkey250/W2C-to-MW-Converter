#pragma once

// English MW 1.3 NISActivity::OnTask retries Play while the activity is ready.
// Keep that native retry and its ownership checks. Patching only the caller
// also preserves any companion module's hook on NISActivity::Play itself.
static constexpr unsigned NativeScenePlayAddress=0x6F5750;
static constexpr unsigned NativeSceneCameraAddress=0x6EA490;
static constexpr unsigned NativeSceneCallAddress=0x6F595E;
static constexpr unsigned NativeSceneInterfaceOffset=0x48;
static constexpr unsigned NativeSceneStateOffset=0x2F4;
static constexpr unsigned NativeSceneOriginOffset=0x3BC;
static constexpr unsigned NativeSceneHashOffset=0x394;
static constexpr unsigned NativeSceneReady=3,NativeScenePlaying=5;
static constexpr float NativeSceneRadius=750.f;

struct NativeSceneGate {
 BYTE* activity=nullptr;
 unsigned hash=0;
 Vec3 camera{},origin{};
 WorldLoadStable stable{};
 DWORD started=0,lastCheck=0,lastProgress=0;
 bool playing=false;

 bool Matches(const BYTE* self,unsigned sceneHash) const {
  return activity==self&&hash==sceneHash;
 }
 bool Live() const {
  return activity&&*reinterpret_cast<BYTE**>(0x9885C8)==activity+NativeSceneInterfaceOffset&&
   *reinterpret_cast<const unsigned*>(activity+NativeSceneHashOffset)==hash&&
   *reinterpret_cast<const unsigned*>(activity+NativeSceneStateOffset)==(playing?NativeScenePlaying:NativeSceneReady);
 }
};
static NativeSceneGate nativeSceneGate;

static bool IsNativeScenePlaying(){
 return nativeSceneGate.playing&&nativeSceneGate.Live();
}
static bool NativeSceneFocus(Vec3& focus){
 if(nativeSceneGate.playing||!nativeSceneGate.stable.centerSeen||!nativeSceneGate.Live())return false;
 focus=nativeSceneGate.camera;
 return true;
}
static bool NativeSceneNeedsSection(const Header& section){
 Vec3 camera{};
 if(!NativeSceneFocus(camera))return false;
#ifdef W2C_PUBLIC_BUILD
 // Authored native scenes may drive or cut from their first camera to a
 // distant scene origin. Preload the visible route between those anchors
 // as well as the first shot, before either entry gate releases the scene.
 auto span=Sub(nativeSceneGate.origin,camera);float distance=std::sqrt(Dot(span,span));
 if(distance>250.f&&distance<2500.f){
  const unsigned steps=unsigned(std::ceil(distance/250.f));
  for(unsigned i=1;i<=steps;++i){float t=float(i)/steps;
   Vec3 sample{camera.x+span.x*t,camera.y+span.y*t,camera.z+span.z*t};
   if(Distance(section,sample)<=650.f*650.f)return true;
  }
 }
#endif
 return Distance(section,camera)<=NativeSceneRadius*NativeSceneRadius||
  Distance(section,nativeSceneGate.origin)<=float(initialRadius*initialRadius);
}
static bool ValidNativeSceneCenter(Vec3 p){
 return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
  fabs(p.x)<100000&&fabs(p.y)<100000&&fabs(p.z)<100000;
}
static bool NativeSceneMoved(Vec3 a,Vec3 b){
 auto delta=Sub(a,b);
 return Dot(delta,delta)>25.f;
}
static void __fastcall NativeScenePlay(BYTE* self,void*){
 const auto play=reinterpret_cast<void(__thiscall*)(BYTE*)>(NativeScenePlayAddress);
 const auto flow=InitialGameFlow();
 if(!loaderActive||editorStock||(flow!=5&&flow!=6)){
  nativeSceneGate={};
  play(self);
  return;
 }

 auto& gate=nativeSceneGate;
 const auto hash=*reinterpret_cast<const unsigned*>(self+NativeSceneHashOffset);
 const auto now=GetTickCount();
 if(!gate.Matches(self,hash)||gate.playing){
  gate={};
  gate.activity=self;
  gate.hash=hash;
  gate.started=now;
  Log("WORLD_NATIVE_SCENE_HOLD hash=%08X flow=%u",hash,flow);
 }
 if(gate.lastCheck&&now-gate.lastCheck<100)return;
 gate.lastCheck=now;

 // The game resolves keyframe zero in world coordinates, including cameras
 // relative to the animation origin. The native scene origin covers scenes
 // that have no independently resolvable camera track.
 auto origin=WorldLoadCenterFromNative(reinterpret_cast<const float*>(self+NativeSceneOriginOffset));
 if(!ValidNativeSceneCenter(origin)&&!InitialPlayerCenter(origin))return;
 float raw[3]{};
 const auto getCamera=reinterpret_cast<bool(__thiscall*)(BYTE*,float*)>(NativeSceneCameraAddress);
 auto camera=getCamera(self,raw)?WorldLoadCenterFromNative(raw):origin;
 if(!ValidNativeSceneCenter(camera))camera=origin;
 if(!gate.stable.centerSeen||NativeSceneMoved(camera,gate.camera)||NativeSceneMoved(origin,gate.origin)){
  gate.camera=camera;
  gate.origin=origin;
  gate.stable.Moved(now);
 }

 cameraPosition=gate.camera;
 Select();
 device=InitialDevice();
 if(device){
  sceneUploadBudget.Begin(Clock(),true);
  Pump(true);
#ifdef W2C_PUBLIC_BUILD
  if(!nativeProps.empty())TickNativeProps();
#else
  TickNativeProps();
#endif
 }
 auto readiness=CheckWorldReadiness(gate.camera,true,[&](const Header& section){
  if(initialWorldHold&&initialStable.centerSeen)return InitialSectionNeeded(section);
  return NativeSceneNeedsSection(section);
 });
 if(!gate.stable.Update(now,readiness)){
  if(worldLogEnabled&&(!gate.lastProgress||now-gate.lastProgress>=1000)){
   gate.lastProgress=now;
   Log("WORLD_NATIVE_SCENE_WAIT hash=%08X flow=%u camera=(%.2f,%.2f,%.2f) origin=(%.2f,%.2f,%.2f) missing_sections=%u missing_textures=%u elapsed_ms=%u",
    hash,flow,gate.camera.x,gate.camera.y,gate.camera.z,gate.origin.x,gate.origin.y,gate.origin.z,
    readiness.missingSections,readiness.missingTextures,now-gate.started);
  }
  return;
 }
 // Let the native activation callback dismiss its loading screen when it
 // owns this transition. Some in-world scenes have no activation callback.
 if(flow==5&&initialHoldKind==InitialHoldKind::Activation)return;

 Log("WORLD_NATIVE_SCENE_READY_ACTIVE hash=%08X flow=%u sections=%u textures=%u elapsed_ms=%u",
  hash,flow,readiness.sections,readiness.textures,now-gate.started);
 gate.playing=true;
 play(self);
}

static bool InstallNativeSceneGate(){
 const BYTE callGuard[]{0x8B,0xCF,0xE8,0xED,0xFD,0xFF,0xFF,0x5F,0xB0,0x01,0x5E,0xC2,0x08,0x00};
 const BYTE cameraGuard[]{0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0xE0,0,0,0};
 auto site=reinterpret_cast<BYTE*>(NativeSceneCallAddress);
 if(memcmp(site-2,callGuard,sizeof(callGuard))||
    memcmp(reinterpret_cast<const void*>(NativeSceneCameraAddress),cameraGuard,sizeof(cameraGuard))){
  Log("REFUSED native scene loading byte guards");
  return false;
 }
 DWORD protection=0;
 if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)){
  Log("REFUSED native scene loading call protection");
  return false;
 }
 const unsigned displacement=unsigned(NativeScenePlay)-unsigned(site+5);
 memcpy(site+1,&displacement,sizeof(displacement));
 DWORD ignored=0;
 VirtualProtect(site,5,protection,&ignored);
 FlushInstructionCache(GetCurrentProcess(),site,5);
 Log("WORLD_NATIVE_SCENE_GATE_INSTALLED call=%08X radius_m=%.0f",NativeSceneCallAddress,NativeSceneRadius);
 return true;
}
