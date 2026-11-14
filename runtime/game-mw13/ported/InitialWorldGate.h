#pragma once
#include "WorldLoadReadiness.h"
#include "../integration/WorldLoadPlanV1.h"
// Native loading callback: before flow=6 and loaded-world notifications.
// Return to the loading loop while CPU workers and upload servicing continue.
static void (__cdecl* originalWorldActivation)();
static void (__thiscall* originalLoadingAdvance)(void*);
static void (__cdecl* originalInitialWorldFrame)();
static void (__cdecl* originalLoadingDismiss)();
static bool presentationArmed=false,presentationPending=false,presentationDrawReady=false,presentationSceneOnly=false;
static InitialFrontendLease presentationLease{};
static void* presentationRace=nullptr,*presentationManager=nullptr,*presentationPackage=nullptr;
static unsigned presentationFrame=0,presentationGeneration=0,presentationSceneGeneration=0;
static unsigned CurrentSceneGeneration(){auto module=GetModuleHandleA("MWEventLoader.asi");if(!module)module=GetModuleHandleA("NFSMWEventLoader.asi");auto query=module?reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(module,"MWEL_WorldSceneGenerationV1")):nullptr;return query?query():0;}
static DWORD presentationStarted=0,presentationProgress=0;
static void ResetInitialPresentation(){presentationArmed=presentationPending=presentationDrawReady=presentationSceneOnly=false;presentationRace=presentationManager=presentationPackage=nullptr;}
static bool InitialPresentationValid(){
 if(InitialGameFlow()!=6||!presentationRace||presentationRace!=InitialRace()||!presentationSceneGeneration||presentationSceneGeneration!=CurrentSceneGeneration())return false;
 if(presentationSceneOnly)return true;
 InitialFrontendLease current{};if(presentationManager!=InitialFrontendManager()||!presentationPackage||presentationPackage!=InitialLoadingPackage()||!ReadInitialFrontendLease(current))return false;
 // Animation, screen links and queue words are mutable during loading.
 // Ownership is the same manager, engine, loading package, race and scene
 // generation. Comparing the whole animated screen list dropped our deferred
 // native close command and left both LOADING and its ultrawide bars alive.
 return current.words[1]==presentationLease.words[1];
}
static void __cdecl LoadingDismissHook(){
 if(presentationArmed&&_ReturnAddress()==reinterpret_cast<void*>(0x662881)&&InitialGameFlow()==6&&presentationRace==InitialRace()){
  presentationArmed=false;presentationManager=InitialFrontendManager();presentationPackage=InitialLoadingPackage();
  if(presentationManager&&presentationPackage&&ReadInitialFrontendLease(presentationLease)){presentationPending=true;presentationDrawReady=false;presentationStarted=GetTickCount();presentationProgress=0;++presentationGeneration;Log("WORLD_PRESENTATION_HOLD generation=%u source_camera_required=1 native_close_message_deferred=1",presentationGeneration);return;}
 }
 originalLoadingDismiss();
}
static void ObserveInitialPresentationDraw(unsigned meshes){
 if(!presentationPending||presentationDrawReady||!meshes)return;
 auto module=GetModuleHandleA("MWEventLoader.asi");if(!module)module=GetModuleHandleA("NFSMWEventLoader.asi");
 auto ready=module?reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(module,"MWEL_WorldPresentationReadyV1")):nullptr;
 if(ready&&ready()){presentationDrawReady=true;Log("WORLD_PRESENTATION_DRAW_READY generation=%u meshes=%u",presentationGeneration,meshes);}
}
static void TickInitialPresentation(){
 ++presentationFrame;if(!presentationFrame)++presentationFrame;
 if(!presentationPending)return;
 if(!InitialPresentationValid()){Log("WORLD_PRESENTATION_CANCEL stale_transition=1");ResetInitialPresentation();return;}
 if(presentationDrawReady){InitialFrontendLease current{};if(!presentationSceneOnly&&(!ReadInitialFrontendLease(current)||current.words[4]||current.words[7]))return;auto generation=presentationGeneration;bool sceneOnly=presentationSceneOnly;ResetInitialPresentation();if(!sceneOnly)originalLoadingDismiss();Log("WORLD_PRESENTATION_RELEASE generation=%u native_close_message_replayed=%u",generation,unsigned(!sceneOnly));return;}
 if(worldLogEnabled&&(!presentationProgress||GetTickCount()-presentationProgress>=1000)){presentationProgress=GetTickCount();Log("WORLD_PRESENTATION_WAIT generation=%u elapsed_ms=%u",presentationGeneration,GetTickCount()-presentationStarted);}
}
enum class InitialHoldKind {None,Activation,Frontend,Scene};
static InitialHoldKind initialHoldKind=InitialHoldKind::None;
static void* initialLoadingController=nullptr;
static DWORD initialHoldStart=0,initialProgress=0,initialService=0;
static WorldLoadStable initialStable;
static Vec3 initialCenter{};
static mw_world_load::Plan initialScenePlan{};
static bool initialScenePlanInvalid=false,initialScenePlanPending=false;
static unsigned initialRadius=350,initialTimeout=120000;
static bool initialServicing=false;
#ifdef W2C_PUBLIC_BUILD
static bool initialLoadingFrameLogged=false;
static void DrawInitialWorldLoadingFrame(IDirect3DDevice9* renderingDevice){
 // The stock career can expose its actors while activation is still in
 // flow 5. Cover that incomplete frame until the native callback can run.
 if(initialHoldKind!=InitialHoldKind::Activation||InitialGameFlow()!=5||v2Lost||initialScenePlan.count)return;
 IDirect3DSurface9* backBuffer=nullptr;
 if(FAILED(renderingDevice->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer)))return;
 D3DSURFACE_DESC surface{};
 if(SUCCEEDED(backBuffer->GetDesc(&surface))&&
    SUCCEEDED(renderingDevice->ColorFill(backBuffer,nullptr,D3DCOLOR_XRGB(16,18,15)))){
  const LONG width=std::min<LONG>(surface.Width/4,240);
  const LONG height=std::max<LONG>(surface.Height/220,2);
  const LONG left=(LONG(surface.Width)-width)/2;
  const LONG top=LONG(surface.Height)*9/10;
  RECT track{left,top,left+width,top+height};
  renderingDevice->ColorFill(backBuffer,&track,D3DCOLOR_XRGB(58,62,52));
  // Indeterminate indicator. Streaming completion controls the transition.
  const LONG span=width/4;
  const DWORD phase=(GetTickCount()-initialHoldStart)%1600;
  const LONG travel=(width-span)*LONG(phase<=800?phase:1600-phase)/800;
  RECT pulse{left+travel,top,left+travel+span,top+height};
  renderingDevice->ColorFill(backBuffer,&pulse,D3DCOLOR_XRGB(209,229,144));
  if(worldLogEnabled&&!initialLoadingFrameLogged){
   initialLoadingFrameLogged=true;
   Log("WORLD_LOAD_PRESENTATION_HOLD flow=5 incomplete_frame_covered=1");
  }
 }
 backBuffer->Release();
}
#endif
static void ResetInitialWorldGate(){
 initialLoadingController=nullptr;initialWorldHold=false;initialHoldKind=InitialHoldKind::None;initialStable.Reset();initialScenePlan={};initialScenePlanInvalid=initialScenePlanPending=false;
#ifdef W2C_PUBLIC_BUILD
 initialLoadingFrameLogged=false;
#endif
}
static void BeginInitialWorldGate(InitialHoldKind kind,void* controller=nullptr){
 V2Cancel();ResetInitialPresentation();ResetInitialWorldGate();initialHoldKind=kind;initialLoadingController=controller;
 initialWorldHold=true;initialHoldStart=GetTickCount();initialProgress=initialService=0;
 Log("WORLD_LOAD_HOLD kind=%u flow=%u radius_m=%u timeout_fail_open=0",unsigned(kind),InitialGameFlow(),initialRadius);
}
static void ObserveInitialWorldCenter(Vec3){
 Vec3 center{};if(!initialWorldHold||!InitialPlayerCenter(center))return;
 if(!initialStable.centerSeen||Dot(Sub(center,initialCenter),Sub(center,initialCenter))>25){
  initialCenter=center;initialStable.Moved(GetTickCount());
 }
}
static bool NativeSceneFocus(Vec3&);
static bool NativeSceneNeedsSection(const Header&);
static Vec3 InitialLoadFocus(){
 if(initialScenePlan.count){const auto& p=initialScenePlan.points[0];return {p.x,p.y,p.z};}
 Vec3 focus{};
 if(NativeSceneFocus(focus))return focus;
 return initialCenter;
}
static void RefreshInitialScenePlan(){
 auto module=GetModuleHandleA("MWEventLoader.asi");if(!module)module=GetModuleHandleA("NFSMWEventLoader.asi");
 auto query=module?reinterpret_cast<mw_world_load::Query>(GetProcAddress(module,"MWEL_WorldLoadPlanV1")):nullptr;
 if(!query)return;mw_world_load::Plan plan{};unsigned status=query(&plan,sizeof(plan));
 if(status==mw_world_load::Pending){if(!initialScenePlanPending)Log("WORLD_LOAD_SCENE_WAIT native_stage_pending=1");initialScenePlanPending=true;initialStable.readySeen=false;return;}
 initialScenePlanPending=false;
 if(status==mw_world_load::Inactive)return;
 bool valid=status==1&&plan.version==mw_world_load::Version&&plan.count&&plan.count<=mw_world_load::Capacity;
 if(valid)for(unsigned i=0;i<plan.count;++i){const auto& p=plan.points[i];if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||fabs(p.x)>100000||fabs(p.y)>100000||p.kind>mw_world_load::Camera){valid=false;break;}}
 if(!valid){if(!initialScenePlanInvalid)Log("WORLD_LOAD_SCENE_REFUSED invalid_plan=1");initialScenePlanInvalid=true;return;}
 if(memcmp(&initialScenePlan,&plan,sizeof(plan))){initialScenePlan=plan;initialStable.readySeen=false;auto p=InitialLoadFocus();Log("WORLD_LOAD_SCENE_PLAN generation=%u points=%u focus=(%.2f,%.2f,%.2f) native_car_controls_acquired=0",plan.generation,plan.count,p.x,p.y,p.z);}
 initialScenePlanInvalid=false;
}
static bool InitialSectionNeeded(const Header& h){
 if(NativeSceneNeedsSection(h))return true;
 if(Distance(h,initialCenter)<=float(initialRadius*initialRadius))return true;
 for(unsigned i=0;i<initialScenePlan.count;++i){const auto& p=initialScenePlan.points[i];
  // Plan deduplication is bounded to 100m. Pad by the same distance so
  // intermediate poses retain the complete 650m camera draw radius.
  float radius=(p.kind==mw_world_load::Camera?650.f:float(initialRadius))+100.f;
  if(Distance(h,{p.x,p.y,p.z})<=radius*radius)return true;
 }return false;
}
template<class NeedsSection>
static WorldLoadReadiness CheckWorldReadiness(Vec3 focus,bool center,NeedsSection needed){
 WorldLoadReadiness r;r.center=center;r.collision=InitialCollisionReady();r.shaders=InitialShadersReady();
 if(!r.center)return r;
 std::set<unsigned> needs;
 auto material=[&](unsigned h){
  if(!h||h==0xD2E09151||h==0x29D9478C||v2Store.originalUnavailableTexture(h))return;
  needs.insert(h);auto e=envMaterials.find(h);if(e!=envMaterials.end()&&e->second.mask)needs.insert(e->second.mask);
 };
 std::vector<std::pair<float,unsigned>> missing;
 for(auto& [id,s]:sections){
  if(id!=26000&&!needed(s.header))continue;
  ++r.sections;s.used=GetTickCount();if(!s.vb||!s.ib){++r.missingSections;if(!s.queued)missing.push_back({Distance(s.header,focus),id});continue;}
  // EnvironmentMeshLights contains envLights indices (including ~0u), not
  // texture hashes. Only draw materials and their masks are upload assets.
  for(auto& mesh:s.meshes)material(mesh.texture);
 }
 InitialPropReadiness(focus,r,needs);
 r.textures=unsigned(needs.size());for(auto h:needs)if(!Texture(h))++r.missingTextures;
 // Previously queued distant-view assets must not stay ahead of the start
 // area's dependencies after travel/retry. CPU workers retain queue ownership.
 {std::lock_guard lock(ioMutex);
  // Keep the bounded terrain queue on this load's union of start/camera
  // regions. This includes regions beyond Select's single 900m center.
  for(auto it=pending.begin();it!=pending.end();){auto& s=sections[*it];if(*it!=26000&&!needed(s.header)){s.queued=false;it=pending.erase(it);}else ++it;}
  std::sort(missing.begin(),missing.end());for(const auto& entry:missing){if(pending.size()>=16||!V2RequestRoom())break;sections[entry.second].queued=true;pending.push_back(entry.second);}
  std::stable_partition(texturePending.begin(),texturePending.end(),[&](unsigned h){return needs.count(h)!=0;});
  std::stable_sort(pending.begin(),pending.end(),[&](unsigned a,unsigned b){return Distance(sections[a].header,focus)<Distance(sections[b].header,focus);});
 }
 ioSignal.notify_all();
 return r;
}
static WorldLoadReadiness InitialWorldReadiness(){
 return CheckWorldReadiness(InitialLoadFocus(),initialStable.centerSeen&&!initialScenePlanInvalid&&!initialScenePlanPending,InitialSectionNeeded);
}
#include "NativeSceneGate.h"
static bool InitialWorldHoldValid(){
 if(initialHoldKind==InitialHoldKind::Activation)return InitialGameFlow()==5;
 if(initialHoldKind==InitialHoldKind::Scene)return InitialGameFlow()==6&&presentationRace&&presentationRace==InitialRace()&&initialScenePlan.generation==CurrentSceneGeneration();
 return initialHoldKind==InitialHoldKind::Frontend&&InitialGameFlow()==6&&
  initialLoadingController&&initialLoadingController==InitialFrontendController()&&
  *reinterpret_cast<unsigned*>(static_cast<BYTE*>(initialLoadingController)+4)==6;
}
static bool ServiceInitialWorldGate(){
 if(!initialWorldHold||initialServicing)return false;
 if(!InitialWorldHoldValid()){Log("WORLD_LOAD_CANCEL stale_transition=1");ResetInitialWorldGate();return false;}
 initialServicing=true;bool complete=false;DWORD now=GetTickCount();
 ObserveInitialWorldCenter({});
 if(initialStable.centerSeen){
  RefreshInitialScenePlan();cameraPosition=InitialLoadFocus();Select();device=InitialDevice();
  if(device){sceneUploadBudget.Begin(Clock(),true);Pump();
#ifdef W2C_PUBLIC_BUILD
   if(!nativeProps.empty())TickNativeProps();
#else
   TickNativeProps();
#endif
  }
  if(!initialService||now-initialService>=100){
   initialService=now;auto r=InitialWorldReadiness();complete=initialStable.Update(now,r);
   if(worldLogEnabled&&(!initialProgress||now-initialProgress>=1000)){
    initialProgress=now;Log("WORLD_LOAD_PROGRESS flow=%u center=(%.2f,%.2f,%.2f) sections=%u missing_sections=%u textures=%u missing_textures=%u props=%u missing_props=%u refused_props=%u collision=%u elapsed_ms=%u stalled=%u",InitialGameFlow(),initialCenter.x,initialCenter.y,initialCenter.z,r.sections,r.missingSections,r.textures,r.missingTextures,r.props,r.missingProps,r.refusedProps,unsigned(r.collision),now-initialHoldStart,unsigned(now-initialHoldStart>=initialTimeout));
   }
   if(complete)Log("WORLD_LOAD_READY flow=%u sections=%u textures=%u props=%u refused_props=%u scene_points=%u elapsed_ms=%u",InitialGameFlow(),r.sections,r.textures,r.props,r.refusedProps,initialScenePlan.count,now-initialHoldStart);
  }
 }
 else if(worldLogEnabled&&(!initialProgress||now-initialProgress>=1000)){
  initialProgress=now;Log("WORLD_LOAD_PROGRESS flow=%u center_ready=0 elapsed_ms=%u stalled=%u",InitialGameFlow(),now-initialHoldStart,unsigned(now-initialHoldStart>=initialTimeout));
 }
 initialServicing=false;return complete;
}
static void __cdecl WorldActivationHook(){
 if(worldLogEnabled&&initialHoldKind==InitialHoldKind::None){Vec3 p{};bool found=InitialPlayerCenter(p);Log("WORLD_LOAD_ACTIVATION_ENTRY flow=%u caller=%p player=%u center=(%.2f,%.2f,%.2f)",InitialGameFlow(),_ReturnAddress(),unsigned(found),p.x,p.y,p.z);}
 if(!loaderActive||editorStock||InitialGameFlow()!=5){originalWorldActivation();return;}
 if(initialHoldKind!=InitialHoldKind::Activation)BeginInitialWorldGate(InitialHoldKind::Activation);
 if(!ServiceInitialWorldGate()){
  if(initialWorldHold&&!RearmInitialWorldActivation()){Log("WORLD_LOAD_CANCEL competing_transition=1");ResetInitialWorldGate();}
  return;
 }
 const bool scenePresentation=initialScenePlan.count!=0;ResetInitialWorldGate();
 if(scenePresentation){presentationArmed=true;presentationRace=InitialRace();presentationSceneGeneration=CurrentSceneGeneration();}
 originalWorldActivation();presentationArmed=false;
 Log("WORLD_LOAD_ACTIVATED flow=%u",InitialGameFlow());
}
static void __fastcall LoadingAdvanceHook(void* self,void*){
 if(worldLogEnabled&&self==InitialFrontendController()){Vec3 p{};bool found=InitialPlayerCenter(p);Log("WORLD_LOAD_FRONTEND_ADVANCE state=%u flow=%u caller=%p player=%u center=(%.2f,%.2f,%.2f)",*reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+4),InitialGameFlow(),_ReturnAddress(),unsigned(found),p.x,p.y,p.z);}
 const bool dismissal=_ReturnAddress()==reinterpret_cast<void*>(0x62264B);
 if(dismissal&&loaderActive&&!editorStock&&InitialGameFlow()==6&&self==InitialFrontendController()&&*reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+4)==6){
  // No process-lifetime completion flag: retry, travel and a new race may
  // require another destination even after the first world became ready.
  if(!initialWorldHold){BeginInitialWorldGate(InitialHoldKind::Frontend,self);ObserveInitialWorldCenter({});RefreshInitialScenePlan();auto r=InitialWorldReadiness();if(r.Complete()){ResetInitialWorldGate();originalLoadingAdvance(self);return;}}
  return;
 }
 originalLoadingAdvance(self);
}
static void TickInitialWorldGate(){
 if(initialHoldKind!=InitialHoldKind::Frontend)return;
 if(!ServiceInitialWorldGate())return;
 auto controller=initialLoadingController;ResetInitialWorldGate();originalLoadingAdvance(controller);
}
static bool RequestScenePreparation(const mw_world_load::Plan* plan,unsigned bytes){
 if(!loaderActive||editorStock||InitialGameFlow()!=6||!plan||bytes!=sizeof(*plan)||plan->version!=mw_world_load::Version||!plan->count||plan->count>mw_world_load::Capacity)return false;
 for(unsigned i=0;i<plan->count;++i){const auto& p=plan->points[i];if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||fabs(p.x)>100000||fabs(p.y)>100000||p.kind>mw_world_load::Camera)return false;}
 if(!plan->generation||plan->generation!=CurrentSceneGeneration())return false;
 if(initialWorldHold&&initialHoldKind!=InitialHoldKind::Scene)return true; // retain the existing native load gate
 if(initialHoldKind==InitialHoldKind::Scene&&presentationRace==InitialRace()&&initialScenePlan.generation==plan->generation)return true;
 const bool deferredNativeClose=presentationPending&&!presentationSceneOnly&&presentationRace==InitialRace();
 if(deferredNativeClose){ResetInitialWorldGate();initialHoldKind=InitialHoldKind::Scene;initialWorldHold=true;initialHoldStart=GetTickCount();initialProgress=initialService=0;presentationDrawReady=false;Log("WORLD_PRESENTATION_RETAIN native_close_message=1 scene_reprepare=1");}
 else BeginInitialWorldGate(InitialHoldKind::Scene);
 presentationRace=InitialRace();presentationSceneGeneration=plan->generation;initialScenePlan=*plan;return true;
}
static bool SourceCameraActive(){
 static HMODULE module=nullptr;static unsigned(__cdecl* query)()=nullptr;
 if(!module){module=GetModuleHandleA("MWEventLoader.asi");if(!module)module=GetModuleHandleA("NFSMWEventLoader.asi");if(module)query=reinterpret_cast<decltype(query)>(GetProcAddress(module,"MWEL_WorldPresentationReadyV1"));}
 return query&&(query()&2u);
}
static void __cdecl InitialWorldFrame(){
 originalInitialWorldFrame();if(initialHoldKind==InitialHoldKind::Frontend)TickInitialWorldGate();
 if(initialHoldKind==InitialHoldKind::Scene&&ServiceInitialWorldGate()){
  auto race=presentationRace;auto generation=initialScenePlan.generation;const bool deferredNativeClose=presentationPending&&!presentationSceneOnly;ResetInitialWorldGate();presentationSceneGeneration=generation;presentationPending=true;presentationSceneOnly=!deferredNativeClose;presentationRace=race;presentationStarted=GetTickCount();presentationProgress=0;++presentationGeneration;
  Log("WORLD_PRESENTATION_HOLD generation=%u source_camera_required=1 warm_retry=1",presentationGeneration);
 }
 TickInitialPresentation();
 // Activation is replayed by the native loading callback, never by drawing.
}
static bool InstallInitialWorldGate(){
 const BYTE advanceGuard[]={0x64,0xA1,0,0,0,0,0x6A,0xFF,0x68,0x7F,0xF5,0x86,0,0x50,0x64,0x89,0x25,0,0,0,0};
 const BYTE activateGuard[]={0xA1,0x14,0x2C,0x9B,0,0x85,0xC0,0xC7,0x05,0x90,0x5E,0x92,0,6,0,0,0};
 const BYTE frameGuard[]={0xC3,0xCC,0xCC,0xCC,0xCC};
 const BYTE dismissGuard[]={0x6A,0xFF,0x68,0xAF,0x9F,0x87,0};
 auto advance=reinterpret_cast<void*>(0x5302C0),activate=reinterpret_cast<void*>(0x6627E0),frame=reinterpret_cast<void*>(0x6CB3D0),dismiss=reinterpret_cast<void*>(0x64B4F0);
 initialRadius=std::clamp(GetPrivateProfileIntA("Streaming","InitialReadyRadius",350,Path("W2CMWv2.ini").c_str()),100u,650u);
 initialTimeout=std::clamp(GetPrivateProfileIntA("Streaming","InitialReadyTimeoutMs",120000,Path("W2CMWv2.ini").c_str()),10000u,300000u);
 if(memcmp(advance,advanceGuard,sizeof(advanceGuard))||memcmp(activate,activateGuard,sizeof(activateGuard))||memcmp(frame,frameGuard,sizeof(frameGuard))||memcmp(dismiss,dismissGuard,sizeof(dismissGuard))){Log("REFUSED world loading byte guards");return false;}
 bool ok=MH_CreateHook(advance,LoadingAdvanceHook,reinterpret_cast<void**>(&originalLoadingAdvance))==MH_OK&&MH_CreateHook(activate,WorldActivationHook,reinterpret_cast<void**>(&originalWorldActivation))==MH_OK&&MH_CreateHook(frame,InitialWorldFrame,reinterpret_cast<void**>(&originalInitialWorldFrame))==MH_OK&&MH_CreateHook(dismiss,LoadingDismissHook,reinterpret_cast<void**>(&originalLoadingDismiss))==MH_OK;
 if(ok)ok=MH_EnableHook(advance)==MH_OK&&MH_EnableHook(activate)==MH_OK&&MH_EnableHook(frame)==MH_OK&&MH_EnableHook(dismiss)==MH_OK;
 if(ok)ok=InstallNativeSceneGate();
 if(!ok){for(auto p:{advance,activate,frame,dismiss}){MH_DisableHook(p);MH_RemoveHook(p);}Log("REFUSED world loading hooks");return false;}
 Log("WORLD_LOAD_GATE_INSTALLED activation=006627E0 frontend=005302C0 radius_m=%u every_load=1 fail_open=0",initialRadius);return true;
}
