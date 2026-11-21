#pragma once
static HRESULT (STDMETHODCALLTYPE* v2OriginalReset)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)=nullptr;
static HRESULT (STDMETHODCALLTYPE* v2OriginalPresent)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)=nullptr;
static IDirect3DDevice9* v2BoundDevice=nullptr;static bool v2DeviceHooks=false;
static void V2InvalidateDeviceCaches();
static void V2ObservePresentBegin();
static void V2ObservePresentEnd(HRESULT);
#ifdef W2C_PUBLIC_BUILD
static void DrawInitialWorldLoadingFrame(IDirect3DDevice9*);
#endif
struct V2PublicStatus {unsigned magic=0x33563257,version=1,bytes=sizeof(V2PublicStatus),flags=0,error=0,owner=0,world=1,device=1,failures=0,stale=0,published=0,resets=0;std::uint64_t presentFrames=0,maximumGeometryBytesPerFrame=0,readBytes=0,readPeak=0,stageBytes=0,stagePeak=0,metadataBytes=0,geometryBytes=0,textureBytes=0;};
extern "C" __declspec(dllexport) V2PublicStatus W2V2Status{};
#pragma comment(linker,"/EXPORT:W2V2Status=_W2V2Status")
static void V2RefreshStatus(){
 if(!worldLogEnabled)return;auto& s=W2V2Status;s.flags=1|(v2DeviceHooks?2:0)|(loaderActive?4:0)|(v2Lost?8:0)|16;s.owner=v2OwnerThread;s.world=unsigned(v2WorldEpoch.load());s.device=unsigned(v2DeviceEpoch.load());s.failures=v2Failures.load();s.stale=v2StaleDrops.load();s.published=v2Published;s.readBytes=v2ReadBudget.used();s.readPeak=v2ReadBudget.peak();s.stageBytes=v2StageBudget.used();s.stagePeak=v2StageBudget.peak();s.metadataBytes=v2MetadataBudget.used();s.geometryBytes=v2GeometryBudget.used();s.textureBytes=v2TextureBudget.used();
}
static HRESULT STDMETHODCALLTYPE V2Present(IDirect3DDevice9* self,const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty){
 geometryWorkMs=textureWorkMs=0;
 if(renderWorkProfiling)V2ObservePresentBegin();
 if(worldLogEnabled){W2V2Status.maximumGeometryBytesPerFrame=std::max(W2V2Status.maximumGeometryBytesPerFrame,v2UploadBytes);++W2V2Status.presentFrames;}v2UploadBytes=0;v2UploadStarted=0;v2TextureUploadBytes=0;v2TextureUploadStarted=0;
#ifdef W2C_PUBLIC_BUILD
 if(initialWorldHold&&self==v2BoundDevice&&V2Owner())DrawInitialWorldLoadingFrame(self);
#endif
 auto result=v2OriginalPresent(self,source,destination,window,dirty);if(renderWorkProfiling)V2ObservePresentEnd(result);if(result==D3DERR_DEVICELOST){v2Lost=true;ioSignal.notify_all();}return result;
}
static HRESULT STDMETHODCALLTYPE V2Reset(IDirect3DDevice9* self,D3DPRESENT_PARAMETERS* parameters){
 bool owner=V2Owner();v2Lost=true;++v2DeviceEpoch;
 if(owner){V2Cancel();V2ReleaseDeviceTextures();V2InvalidateDeviceCaches();}else{++v2Failures;W2V2Status.error=2;}
 auto result=v2OriginalReset(self,parameters);++W2V2Status.resets;
 if(SUCCEEDED(result)&&owner){v2Lost=false;v2UploadBytes=0;for(auto& [id,s]:sections)if(s.page)s.page->epoch=V2Epoch();V2InvalidateDeviceCaches();ioSignal.notify_all();}
 V2RefreshStatus();return result;
}
static bool V2DeviceState(){
 if(!device||!V2Owner())return false;
 if(v2BoundDevice&&v2BoundDevice!=device){v2Lost=true;++v2Failures;W2V2Status.error=3;return false;}
 if(!v2DeviceHooks){auto table=*reinterpret_cast<void***>(device);auto reset=table[16],present=table[17];
  if(MH_CreateHook(reset,V2Reset,reinterpret_cast<void**>(&v2OriginalReset))!=MH_OK)return false;
  if(MH_CreateHook(present,V2Present,reinterpret_cast<void**>(&v2OriginalPresent))!=MH_OK){MH_RemoveHook(reset);return false;}
  if(MH_EnableHook(reset)!=MH_OK||MH_EnableHook(present)!=MH_OK){MH_DisableHook(reset);MH_DisableHook(present);MH_RemoveHook(reset);MH_RemoveHook(present);return false;}
  v2BoundDevice=device;v2DeviceHooks=true;W2V2Status.flags|=2;W2V2Status.owner=v2OwnerThread;Log("V2_DEVICE_ACTIVE owner=%u reset=%p present=%p managed_geometry=1 default_texture_strips=1",v2OwnerThread,reset,present);
 }
 V2RefreshStatus();return !v2Lost;
}
