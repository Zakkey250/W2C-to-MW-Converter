#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <share.h>
#include <wincrypt.h>
#pragma comment(lib,"Advapi32.lib")
#include <chrono>
#include <array>
#include <intrin.h>
#include <utility>
#include <cctype>
#include "PackFormat.h"
#include "CollisionWorld.h"
#include "../integration/World2MWPortableHost.h"
using namespace w2mw;
#ifdef MW_EDITOR_NATIVE_ONLY
static constexpr bool editorStock=true;
#else
static bool editorStock=false;
#endif
static bool sourceRuntime=true;
static bool LoadSourceRegions();
static void LoadSourceRoads();
static FILE* logFile;static char root[MAX_PATH];static LARGE_INTEGER frequency;
#include "BufferedDebugLog.h"
static double Clock(){LARGE_INTEGER q;QueryPerformanceCounter(&q);return double(q.QuadPart)*1000/frequency.QuadPart;}
#include "RenderWork.h"
static CollisionWorld collision;
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_IsWorldActiveV1();
static bool YellowEditorWallCorridor(Vec3,Vec3,float,bool=false,float=.2f,const Vec3* =nullptr);
#include "WorldRaceNavigation.h"
#include "CheckpointExtension.h"
#include "PalmontRegions.h"
#include "NativeMaterialGuard.h"
#include "SunSetPassCoherence.h"
#include "NativeScenePass.h"
#include "PalmontFacilities.h"
struct Material {unsigned usage=0,blend=0,flags=1,tile=3;};
struct NormalColor {Vec3 normal;DWORD color;};
struct EnvironmentVertex {Vec3 position;Vec3 normal;DWORD color;float u,v;float sunExposure=1;};
struct EnvironmentMeshLights {std::array<unsigned,8> day{},night{};};
struct ShadowIndexSpan {unsigned owner,firstIndex,indexCount;};
#include "PagedTypes.h"
struct CpuSection {w2v2::Lease stage;Header header;std::vector<Mesh> meshes;std::vector<Vertex> vertices;std::vector<uint32_t> indices;std::vector<NormalColor> normals;std::vector<EnvironmentVertex> gpuVertices;std::vector<EnvironmentMeshLights> lights;std::vector<unsigned> triangleOverlap,meshOverlap,shadowIndices;std::vector<ShadowIndexSpan> shadowSpans;std::array<std::vector<unsigned>,2> drawMeshes;};
struct Section {std::shared_ptr<PageGpu> page;w2v2::Lease gpuLease;Header header{};IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;std::vector<Mesh> meshes;std::vector<EnvironmentMeshLights> lights;std::vector<unsigned> meshOverlap;std::vector<ShadowIndexSpan> shadowSpans;std::array<std::vector<unsigned>,2> drawMeshes;IDirect3DIndexBuffer9* shadowIB=nullptr;unsigned shadowIndices=0;bool queued=false;DWORD used=0;uint64_t bytes=0;};
struct Ready {unsigned id;std::unique_ptr<CpuSection> data;double ms;w2v2::Epoch epoch;};
struct TextureData {unsigned hash;std::vector<BYTE> bytes;w2v2::Lease stage;w2v2::Epoch epoch;bool full=false;};
struct Zone {unsigned id;std::vector<Vec3> boundary;std::set<unsigned> sections;Vec3 low{FLT_MAX,FLT_MAX,0},high{-FLT_MAX,-FLT_MAX,0};};
static std::map<unsigned,Section> sections;static std::map<unsigned,Material> materials;
// Materials are immutable once streaming starts. Missing entries use the same
// default as before, without inserting into a map read by CPU workers.
static Material MaterialOf(unsigned hash){const auto& table=materials;auto it=table.find(hash);return it==table.end()?Material{}:it->second;}
#include "MeshDrawLists.h"
#include "ShadowIndexBatches.h"
static std::map<unsigned,IDirect3DTexture9*> textures;static std::vector<Zone> zones;
static std::map<unsigned,w2v2::Lease> v2TextureLeases;
static std::map<unsigned,DWORD> textureUsed;static std::map<unsigned,uint64_t> textureBytes;static uint64_t textureResident=0;
static std::mutex ioMutex;static std::condition_variable ioSignal;static std::deque<V2Request> pending;static std::deque<Ready> ready;
static std::deque<std::unique_ptr<PageCpu>> v2PageReady;
static std::deque<V2Request> texturePending;static std::deque<TextureData> textureReady;static std::set<unsigned> textureRequested;
static std::deque<V2Request> propReadPending;static std::deque<Ready> propReadReady;
static unsigned v2ActiveRequests=0;
static bool V2RequestRoom(){return pending.size()+texturePending.size()+propReadPending.size()+v2ActiveRequests<64;}
static bool quit=false,loaderActive=false,ioPrepared=false,dualStreaming=false;static HANDLE ioThread=nullptr,assetThread=nullptr;static uint64_t resident=0;
static unsigned frameCounter=0,drawCounter=0,zoneIndex=~0u;static DWORD lastSelect=0,lastReport=0;static Vec3 cameraPosition{};
#ifndef MW_EDITOR_NATIVE_ONLY
#include "WorldRadar.h"
#endif
static double totalReadMs=0,totalUploadMs=0;static unsigned readCount=0,uploadCount=0;static IDirect3DDevice9* device=nullptr;

static unsigned renderStage=0;
static D3DFORMAT sceneTargetFormat=D3DFMT_UNKNOWN;
#include "CollisionBridge.h"
#include "HeliClearance.h"
static std::string Path(const char* tail){
#ifdef W2C_PUBLIC_BUILD
 if(!strcmp(tail,"W2CMWv2.ini"))tail="W2CMWPublic\\runtime.ini";
 if(!strcmp(tail,"W2CMWv2.log"))tail="W2CMWPublic\\runtime.log";
#endif
 return std::string(root)+tail;
}
#include "SourceInput.h"
#include "PublicPalmontDistricts.h"
#ifndef W2C_PUBLIC_BUILD
#include "PreviousBarrierAppearance.h"
#endif
#include "SourceMwTolls.h"
#ifdef W2C_PUBLIC_BUILD
#include "SourceMwPropFallback.h"
#endif
#include "SourceRegions.h"
#include "NativeIOTrace.h"
#include "WeatherCover.h"
static float Distance(const Header&,Vec3);
#include "OverlapOwnership.h"
static bool worldRenderTelemetry=false;
// Main scene frame intervals include AI, streaming and native rendering;
// keep this separate from the CPU time spent submitting our own geometry.
static void WorldObserveFrame(){
 if(!worldRenderTelemetry)return;
 static double previous=0,reported=0,total=0,worst=0;static unsigned samples=0,slow33=0,slow100=0;
 double now=Clock();if(previous){double dt=now-previous;total+=dt;worst=std::max(worst,dt);++samples;slow33+=dt>33.3;slow100+=dt>100;
  static double hitchReported=0;if(dt>100&&now-hitchReported>1000){hitchReported=now;auto race=*reinterpret_cast<BYTE**>(0x91E000);unsigned live=race&&*reinterpret_cast<unsigned*>(race+0x1960)==1&&race[0x198A]&&race[0x1978];Log("WORLD_FRAME_HITCH interval_ms=%.3f race_live=%u frame=%u",dt,live,frameCounter);}
 }previous=now;
 if(!reported)reported=now;
 if(now-reported>=2000&&samples){Log("WORLD_FRAME_PERF mean_ms=%.3f max_ms=%.3f frames=%u over33=%u over100=%u",total/samples,worst,samples,slow33,slow100);reported=now;total=worst=0;samples=slow33=slow100=0;}
}
static void RemovePropGhosts(CpuSection& data);
static bool ReadEnvironmentNormals(unsigned id,CpuSection& data,std::vector<NormalColor>& normals);
static bool environmentInstalled=false;
static void BuildEnvironmentTiles(CpuSection& data);
static void InstallSunSetCompatibility();
static void LoadEnvironmentDynamics();
#ifdef W2C_PUBLIC_BUILD
static bool sourceCarbonPursuit=false;
#endif
static bool SupportedExe(){
 struct Build{DWORD bytes;const char* sha256;};
 static constexpr Build builds[]={
  {6029312,"B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06"},
#ifdef W2C_PUBLIC_BUILD
  // Redux 3.04 maps the same game code and data at the same addresses.
  {5926912,"0C5675A08CD71FD6D31CA87E992A915054BD8B80D268BFF0561D7ECC2067E342"},
#else
  {6029312,"80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C"},
#endif
 };
 wchar_t exe[MAX_PATH];
 const DWORD length=GetModuleFileNameW(nullptr,exe,MAX_PATH);
 if(!length||length>=MAX_PATH)return false;
 HANDLE file=CreateFileW(exe,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
 if(file==INVALID_HANDLE_VALUE)return false;
 HCRYPTPROV provider=0;
 HCRYPTHASH hash=0;
 BYTE buffer[65536],digest[32];
 DWORD read=0,digestSize=sizeof(digest);
 LARGE_INTEGER fileSize{};
 bool ok=GetFileSizeEx(file,&fileSize)&&
  CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&
  CryptCreateHash(provider,CALG_SHA_256,0,0,&hash);
 while(ok){
  ok=ReadFile(file,buffer,sizeof(buffer),&read,nullptr)!=0;
  if(!ok||!read)break;
  ok=CryptHashData(hash,buffer,read,0)!=0;
 }
 ok=ok&&CryptGetHashParam(hash,HP_HASHVAL,digest,&digestSize,0)&&digestSize==sizeof(digest);
 if(hash)CryptDestroyHash(hash);
 if(provider)CryptReleaseContext(provider,0);
 CloseHandle(file);
 if(!ok)return false;
 char hex[65]{};
 for(unsigned n=0;n<sizeof(digest);++n)sprintf_s(hex+n*2,3,"%02X",digest[n]);
 for(const auto& build:builds)
  if(fileSize.QuadPart==build.bytes&&!strcmp(hex,build.sha256))return true;
 return false;
 }
#include "SourceLegacyDiscovery.h"
static void PrepareShadowIndices(CpuSection& data){if(data.normals.size()==data.header.vertices)BuildShadowIndexBatches(data);}


static bool ReadPropPack(const char*,CpuSection&);
static bool V2ReadPage(unsigned,w2v2::Epoch);
static void V2CancelPropReads();
static void V2CancelTextureUpload();
static std::vector<unsigned> V2PageTextureKeys(const std::vector<PageDraw>&);
static bool V2PageTexturesReady(const PageGpu&);
static IDirect3DTexture9* Texture(unsigned);
#include "StreamingWork.h"
static void ReadStreamJob(StreamJob job,unsigned id,w2v2::Epoch requestEpoch){
 if(requestEpoch!=V2Epoch()){++v2StaleDrops;return;}
 if(job==StreamJob::Prop){auto start=worldLogEnabled?Clock():0;auto data=std::make_unique<CpuSection>();char name[90];sprintf_s(name,"NativeProps/%u.wmw",id);auto source=std::filesystem::path(root)/name;std::error_code error;auto size=std::filesystem::file_size(source,error);if(error||size>16ull*1048576){++v2Failures;return;}w2v2::Lease lease(v2StageBudget,size*2);if(!lease){++v2BudgetWaits;{std::lock_guard lock(ioMutex);if(requestEpoch==V2Epoch())propReadPending.emplace_back(id,requestEpoch);}Sleep(2);return;}data->stage=std::move(lease);if(!ReadPropPack(name,*data))data.reset();{std::lock_guard lock(ioMutex);propReadReady.push_back({id,std::move(data),start?Clock()-start:0,requestEpoch});}return;}
 if(job==StreamJob::Texture){
  TextureData result{id,{}};result.epoch=requestEpoch;
  try{const auto* entry=v2Store.texture(id);
   w2v2disk::need(entry!=nullptr,"texture absent from approved source generation");
   const w2v2disk::B* previous=nullptr;
#ifndef W2C_PUBLIC_BUILD
   previous=previous_barrier_appearance::Find(id);
#endif
   auto bytes=previous?previous->size():entry->bytes;w2v2disk::need(bytes>=128&&bytes<=16ull*1048576,"bounded texture source");w2v2::Lease input(v2ReadBudget,bytes),stage(v2StageBudget,bytes);
   if(!input||!stage){++v2BudgetWaits;{std::lock_guard lock(ioMutex);if(requestEpoch==V2Epoch())texturePending.emplace_back(id,requestEpoch);}Sleep(2);return;}
   result.bytes=previous?*previous:v2Store.read(*entry);result.stage=std::move(stage);
  }catch(const std::exception& error){++v2Failures;Log("V2_TEXTURE_REFUSED hash=%08X reason=%s",id,error.what());}
  // Cancellation already removed the old request. A current request for the
  // same hash may have been queued while this read was in flight.
  {std::lock_guard lock(ioMutex);if(result.epoch!=V2Epoch())++v2StaleDrops;else textureReady.push_back(std::move(result));}return;
 }

#ifndef MW_EDITOR_NATIVE_ONLY
 if(!V2ReadPage(id,requestEpoch)){std::lock_guard lock(ioMutex);if(requestEpoch==V2Epoch())pending.emplace_back(id,requestEpoch);}
#endif
}
static DWORD StreamLoop(bool assets){unsigned cursor=0;
 for(;;){unsigned id=0;w2v2::Epoch requestEpoch;StreamJob job=StreamJob::None;
  {std::unique_lock lock(ioMutex);ioSignal.wait(lock,[assets]{return quit||(!v2Lost&&ioPrepared&&((!assets&&!pending.empty()&&v2PageReady.size()<4)||((assets||!dualStreaming)&&((!texturePending.empty()&&textureReady.size()<4)||(!propReadPending.empty()&&propReadReady.size()<4)))));});if(quit)return 0;
   job=PickStreamJob(!assets&&!pending.empty()&&v2PageReady.size()<4,(assets||!dualStreaming)&&!texturePending.empty()&&textureReady.size()<4,(assets||!dualStreaming)&&!propReadPending.empty()&&propReadReady.size()<4,cursor);
   auto& queue=job==StreamJob::Terrain?pending:(job==StreamJob::Texture?texturePending:propReadPending);id=queue.front().id;requestEpoch=queue.front().epoch;queue.pop_front();++v2ActiveRequests;
  }
  ReadStreamJob(job,id,requestEpoch);{std::lock_guard lock(ioMutex);--v2ActiveRequests;}ioSignal.notify_all();
 }
}
static DWORD WINAPI Worker(void*){
 if(!editorStock){
  if(!sourceRuntime)LoadOverlapGroups();
#ifndef W2C_PUBLIC_BUILD
  for(unsigned n=0;n<100&&!GetModuleHandleA("SunSet.asi");++n)Sleep(50);
  InstallSunSetCompatibility();
#endif
  LoadEnvironmentDynamics();
 }
 {std::lock_guard lock(ioMutex);ioPrepared=true;}
 ioSignal.notify_all();return StreamLoop(false);
}
static DWORD WINAPI AssetWorker(void*){return StreamLoop(true);}
static w2v2::Lease v2TextureScratch;
static bool StartStreaming(){
 // Both workers execute CPU reads/preparation only. D3D and native spawns stay
 // on the game thread. If an optional worker cannot start, retain fair serial IO.
 // Runtime workers own module code until process exit; an ASI cannot be
 // unloaded while its CPU workers are still executing.
 v2TextureScratch=w2v2::Lease(v2StageBudget,512ull*1024);if(!v2TextureScratch)return false;
 HMODULE lifetime=nullptr;if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCSTR>(&StartStreaming),&lifetime))return false;
 dualStreaming=!editorStock&&GetPrivateProfileIntA("Streaming","ParallelAssets",1,Path("W2CMWv2.ini").c_str())!=0;
 if(dualStreaming){assetThread=CreateThread(nullptr,0,AssetWorker,nullptr,0,nullptr);if(!assetThread)dualStreaming=false;}
 ioThread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
 if(!ioThread){std::lock_guard lock(ioMutex);quit=true;ioSignal.notify_all();return false;}
 Log("STREAMING_WORKERS terrain=1 assets=%u cpu_only=1 completed_queue_limit=4",unsigned(dualStreaming));return true;
}

static bool Inside(const Zone& zone,float x,float y){bool inside=false;size_t j=zone.boundary.size()-1;for(size_t i=0;i<zone.boundary.size();j=i++){auto a=zone.boundary[i],b=zone.boundary[j];if((a.y>y)!=(b.y>y)&&x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x)inside=!inside;}return inside;}
static float Distance(const Header& h,Vec3 p){float x=std::max({h.minimum.x-p.x,0.f,p.x-h.maximum.x}),y=std::max({h.minimum.y-p.y,0.f,p.y-h.maximum.y});return x*x+y*y;}
#include "PagedStreaming.h"
static void Select(){V2Select();}
static bool initialWorldHold=false;
static bool Upload(CpuSection& data,Section& section){auto& h=data.header;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* buffer=nullptr;
 bool hasNormals=environmentInstalled&&data.normals.size()==h.vertices;
 unsigned stride=hasNormals?sizeof(EnvironmentVertex):sizeof(Vertex);
 if(!v2UploadStarted)v2UploadStarted=Clock();std::uint64_t transfer=std::uint64_t(h.vertices)*stride+std::uint64_t(h.indices)*4+std::uint64_t(data.shadowIndices.size())*4;if(v2UploadBytes+transfer>1048576||(!initialWorldHold&&Clock()-v2UploadStarted>=1.0))return false;
 w2v2::Lease gpu(v2GeometryBudget,std::uint64_t(h.vertices)*stride+std::uint64_t(h.indices)*4+std::uint64_t(data.shadowIndices.size())*4);if(!gpu)return false;
 if(FAILED(device->CreateVertexBuffer(h.vertices*stride,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&vb,nullptr)))return false;
 if(FAILED(device->CreateIndexBuffer(h.indices*4,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr))){vb->Release();return false;}
 bool ok=SUCCEEDED(vb->Lock(0,0,&buffer,0));if(ok){if(hasNormals){if(data.gpuVertices.size()==h.vertices)memcpy(buffer,data.gpuVertices.data(),h.vertices*sizeof(EnvironmentVertex));else{auto gpu=(EnvironmentVertex*)buffer;for(unsigned n=0;n<h.vertices;++n){auto& v=data.vertices[n];auto& nc=data.normals[n];gpu[n]={v.position,nc.normal,nc.color,v.u,v.v,W2CSourceSunExposure(v.position)};}}}else memcpy(buffer,data.vertices.data(),h.vertices*sizeof(Vertex));vb->Unlock();ok=SUCCEEDED(ib->Lock(0,0,&buffer,0));if(ok){memcpy(buffer,data.indices.data(),h.indices*4);ib->Unlock();}}
 if(!ok){vb->Release();ib->Release();return false;}section.header=h;section.vb=vb;section.ib=ib;section.meshes=std::move(data.meshes);section.drawMeshes=std::move(data.drawMeshes);section.lights=std::move(data.lights);section.meshOverlap=std::move(data.meshOverlap);section.used=GetTickCount();section.bytes=uint64_t(h.vertices)*stride+uint64_t(h.indices)*4;
 // Opaque shadow depth is independent of material/light/UV state. A private
 // index stream joins those triangles without touching the visible geometry.
 if(hasNormals){const auto& shadow=data.shadowIndices;
  if(!shadow.empty()&&SUCCEEDED(device->CreateIndexBuffer(unsigned(shadow.size()*4),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&section.shadowIB,nullptr))){void* shadowBuffer=nullptr;if(SUCCEEDED(section.shadowIB->Lock(0,0,&shadowBuffer,0))){memcpy(shadowBuffer,shadow.data(),shadow.size()*4);section.shadowIB->Unlock();section.shadowIndices=unsigned(shadow.size());section.shadowSpans=std::move(data.shadowSpans);section.bytes+=shadow.size()*4;}else{section.shadowIB->Release();section.shadowIB=nullptr;}}
 }

 section.gpuLease=std::move(gpu);v2UploadBytes+=transfer;resident+=section.bytes;return true;
}
#include "TextureStreaming.h"
#include "DeviceLifecycle.h"
static SceneUploadBudget sceneUploadBudget;
static void Pump(bool loading=initialWorldHold){if(!V2DeviceState())return;if(editorStock)sceneUploadBudget.Begin(Clock(),loading);
#ifndef MW_EDITOR_NATIVE_ONLY
if(!v2UploadStarted)v2UploadStarted=Clock();V2PumpTerrain(loading);
#endif
 // A scene preparation gate can also be entered during an active race.
 // Keep the driving budget in that case, even while presentation is held.
 auto race=*reinterpret_cast<BYTE**>(0x91E000);
 bool live=race&&*reinterpret_cast<unsigned*>(race+0x1960)==1&&race[0x198A]&&race[0x1978];
 if(!V2PumpTextures(loading&&!live))W2V2Status.error=4;
}
static IDirect3DTexture9* Texture(unsigned hash){if(!hash||v2Store.originalUnavailableTexture(hash))return nullptr;auto it=textures.find(hash);if(it!=textures.end()&&it->second){textureUsed[hash]=GetTickCount();return it->second;}
 std::lock_guard lock(ioMutex);if(!textureRequested.count(hash)&&V2RequestRoom()){textureRequested.insert(hash);texturePending.push_back(hash);ioSignal.notify_all();}return nullptr;
}
static void TrimTextures(){
 static DWORD last=0;const DWORD now=GetTickCount();if(now-last<1000)return;last=now;
 for(auto it=textures.begin();it!=textures.end();){
  const auto hash=it->first;
  const auto age=now-textureUsed[hash];
  if(!it->second||v2TexturePins.count(hash)||age<=5000||(textureResident<=384ull*1048576&&age<=15000)){++it;continue;}
  it->second->Release();textureResident-=textureBytes[hash];v2TextureLeases.erase(hash);
  textureBytes.erase(hash);textureUsed.erase(hash);it=textures.erase(it);
  std::lock_guard lock(ioMutex);textureRequested.erase(hash);
 }
}
#include "RenderFrustum.h"
#include "RenderBounds.h"
static bool VisibleBox(Vec3 low,Vec3 high,const D3DMATRIX& matrix){return RenderFrustum(matrix).Visible(low,high);}
#include "Environment.h"
static std::vector<unsigned> V2PageTextureKeys(const std::vector<PageDraw>& draws){
 std::vector<unsigned> bases,needed;bases.reserve(draws.size());
 for(const auto& draw:draws)if(draw.texture&&draw.texture!=0xD2E09151&&draw.texture!=0x29D9478C)bases.push_back(draw.texture);
 auto unique=[](auto& keys){std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());};
 unique(bases);
 auto add=[&](unsigned hash){if(hash&&!v2Store.originalUnavailableTexture(hash))needed.push_back(hash);};
 for(auto hash:bases){if(v2Store.originalUnavailableTexture(hash))continue;needed.push_back(hash);
  auto env=envMaterials.find(hash);if(env!=envMaterials.end())add(env->second.mask);
  auto anim=envFrames.find(hash);if(anim!=envFrames.end())for(auto frame:envAnimations[anim->second.first].frames)add(frame);
 }
 unique(needed);return needed;
}
static bool V2PageTexturesReady(const PageGpu& page){
 bool ready=true;for(auto hash:page.pins.keys)if(!Texture(hash))ready=false;return ready;
}

static void SetMaterial(unsigned hash){auto r=MaterialOf(hash);device->SetRenderState(D3DRS_DEPTHBIAS,0);device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);device->SetRenderState(D3DRS_ALPHATESTENABLE,r.usage==1||hash==0x57FC91B7);device->SetRenderState(D3DRS_ALPHAREF,hash==0x57FC91B7?64:11);device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);device->SetRenderState(D3DRS_ALPHABLENDENABLE,(!envDepthPass&&r.blend!=0));device->SetRenderState(D3DRS_ZWRITEENABLE,envDepthPass||r.blend==0);device->SetRenderState(D3DRS_CULLMODE,(r.usage==1||r.blend||(r.flags&2))?D3DCULL_NONE:(envRoadReflectionPass?D3DCULL_CCW:D3DCULL_CW));
 DWORD src=D3DBLEND_ONE,dst=D3DBLEND_ZERO;switch(r.blend){case 1:case 4:src=D3DBLEND_SRCALPHA;dst=D3DBLEND_INVSRCALPHA;break;case 2:src=D3DBLEND_SRCALPHA;dst=D3DBLEND_ONE;break;case 3:src=D3DBLEND_ZERO;dst=D3DBLEND_INVSRCCOLOR;break;case 5:src=D3DBLEND_DESTALPHA;dst=D3DBLEND_INVDESTALPHA;break;case 6:src=D3DBLEND_DESTALPHA;dst=D3DBLEND_ONE;break;}
 device->SetRenderState(D3DRS_SRCBLEND,src);device->SetRenderState(D3DRS_DESTBLEND,dst);device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
 device->SetSamplerState(0,D3DSAMP_ADDRESSU,(r.tile&4)?D3DTADDRESS_MIRROR:((r.tile&1)?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));device->SetSamplerState(0,D3DSAMP_ADDRESSV,(r.tile&8)?D3DTADDRESS_MIRROR:((r.tile&2)?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));
 auto texture=Texture(hash);device->SetTexture(0,texture);device->SetTextureStageState(0,D3DTSS_COLOROP,texture?D3DTOP_MODULATE:D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,texture?D3DTOP_MODULATE:D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
}
static void SetWorldTextureFiltering(){
 // A preceding native pass may leave a positive mip bias or a coarse maximum
 // mip level. Establish our own sampler state inside the captured state block.
 D3DCAPS9 caps{};unsigned anisotropy=1;
 if(SUCCEEDED(device->GetDeviceCaps(&caps))&&(caps.TextureFilterCaps&D3DPTFILTERCAPS_MINFANISOTROPIC))anisotropy=std::min(8u,unsigned(caps.MaxAnisotropy));
 device->SetSamplerState(0,D3DSAMP_MINFILTER,anisotropy>1?D3DTEXF_ANISOTROPIC:D3DTEXF_LINEAR);
 device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
 device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
 device->SetSamplerState(0,D3DSAMP_MAXANISOTROPY,anisotropy);
 device->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0);
 device->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);
 static bool reported=false;if(!reported){reported=true;Log("WORLD_TEXTURE_FILTER anisotropy=%u mip_bias=0 max_mip=0",anisotropy);}
}
#include "EnvironmentDynamics.h"
#ifdef W2C_PUBLIC_BUILD
static bool EnsureSourcePublicPursuitVault();
#endif
#include "NativeProps.h"
static void V2CancelPropReads(){for(auto& p:nativeProps){p->readQueued=false;p->queuedEpoch=V2Epoch();}}
#include "EnvironmentParticles.h"
#include "RenderTiming.h"
static void (__thiscall* nativeOriginalDraw)(void*,void*,void*);
#include "RaceBarriers.h"
#include "EventBridge.h"
static void (__thiscall* propReferenceOriginalDraw)(void*,void*,void*);
static void __fastcall PropReferenceDraw(void* self,void*,void* view,void* flags){propReferenceOriginalDraw(self,view,flags);if(view==reinterpret_cast<void*>(0x919650))TraceNativePropChildren();}
static bool InitPropNativeReference(){
 if(!GetPrivateProfileIntA("Probe","PropNativeReference",0,Path("W2CMWv2Bootstrap.ini").c_str()))return false;
 if(!LoadLibraryA("d3dx9_43.dll")){Log("PROP_REFERENCE_REFUSED D3DX");return true;}
 auto mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED)return true;
 const BYTE drawGuard[]={0x83,0xEC,0x14,0x53,0x8B,0x5C,0x24,0x20,0x56};auto draw=reinterpret_cast<void*>(0x723FA0);
 if(memcmp(draw,drawGuard,sizeof(drawGuard))||MH_CreateHook(draw,PropReferenceDraw,reinterpret_cast<void**>(&propReferenceOriginalDraw))!=MH_OK||MH_EnableHook(draw)!=MH_OK)return true;
 propNativeReference=true;InitNativeProps();
 const BYTE groundGuard[]={0x8B,0x44,0x24,0x10,0x53,0x55,0x8B,0x6C,0x24,0x0C};auto ground=reinterpret_cast<void*>(0x789CC0);
 if(!memcmp(ground,groundGuard,sizeof(groundGuard))&&MH_CreateHook(ground,w2mw::TraceContactGround,reinterpret_cast<void**>(&w2mw::originalContactGround))==MH_OK)MH_EnableHook(ground);
 return true;
}
#ifdef MW_EDITOR_NATIVE_ONLY
// Native scenery remains owned by MW. Only authored event walls need a draw pass.
static void Render(void* targetView,unsigned){
 if(*reinterpret_cast<int*>(0x925E90)!=6||targetView!=reinterpret_cast<void*>(0x919650))return;
 device=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);auto camera=*reinterpret_cast<BYTE**>(targetView);if(!device||!camera)return;
 Pump();envWorldTime=float(Clock()/1000);D3DMATRIX view{},projection{},identity{},clip{};memcpy(&view,camera,64);memcpy(&projection,camera+64,64);identity._11=identity._22=identity._33=identity._44=1;
 Vec3 eye={-(view._41*view._11+view._42*view._12+view._43*view._13),-(view._41*view._21+view._42*view._22+view._43*view._23),-(view._41*view._31+view._42*view._32+view._43*view._33)};
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)clip.m[r][c]+=view.m[r][k]*projection.m[k][c];
 IDirect3DStateBlock9* state=nullptr;if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return; // CreateStateBlock already captures the current D3D9 state.

 device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);device->SetTransform(D3DTS_WORLD,&identity);device->SetTransform(D3DTS_VIEW,&view);device->SetTransform(D3DTS_PROJECTION,&projection);
 device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
 DrawRaceBarriers(clip,eye,true);state->Apply();state->Release();
}
#else
#ifdef W2C_PUBLIC_BUILD
#include "SourcePublicDeployment.h"
#endif
#include "PagedInitialization.h"
#include "SourceNativeResources.h"
#ifdef W2C_PUBLIC_BUILD
#include "SourcePublicPursuitVault.h"
#endif
#include "PagedRender.h"
#include "RenderTransformState.h"
#include "InitialWorldBindings.h"
#include "InitialWorldGate.h"
static void RenderBody(void* targetView,unsigned drawFlags){
 const auto flow=InitialGameFlow();
 const bool nativeScenePlaying=IsNativeScenePlaying();
 if((flow!=6&&!(flow==5&&nativeScenePlaying))||v2Lost)return;
 const bool mainView=targetView==reinterpret_cast<void*>(0x919650);unsigned viewId=*reinterpret_cast<unsigned*>(static_cast<BYTE*>(targetView)+4);const bool reflection=viewId>=18&&viewId<=23;envRoadReflectionPass=viewId==4;double passStart=worldLogEnabled?Clock():0;// The normal SunSet pass may retain drawFlags=0x20. The R32F target,
 // observed only in the depth pass, is the authoritative discriminator.
 envDepthPass=mainView&&sceneTargetFormat==D3DFMT_R32F;
 if(mainView&&(envDepthPass||!envDepthFrameReady))WorldObserveFrame();
 const bool updateScene=mainView&&(envDepthPass||!envDepthFrameReady);if(mainView){if(envDepthPass)envDepthFrameReady=true;else envDepthFrameReady=false;}if(updateScene)++frameCounter;device=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);auto camera=*reinterpret_cast<BYTE**>(targetView);if(!device||!camera)return;
 // Game eView stores world-to-view and projection matrices in render axes.
 D3DMATRIX view{},projection{},identity{};memcpy(&view,camera,64);memcpy(&projection,camera+0x40,64);identity._11=identity._22=identity._33=identity._44=1;
 Vec3 passPosition={-(view._41*view._11+view._42*view._12+view._43*view._13),-(view._41*view._21+view._42*view._22+view._43*view._23),-(view._41*view._31+view._42*view._32+view._43*view._33)};
 double selectMs=0,pumpMs=0,propsMs=0,stageStart=worldLogEnabled?Clock():0;
 if(updateScene&&!editorStock){ObserveInitialWorldCenter(passPosition);
 cameraPosition=initialWorldHold&&initialStable.centerSeen?InitialLoadFocus():passPosition;
 if(nativeScenePlaying)cameraPosition=passPosition;
 else if(!initialScenePlan.count)NativeSceneFocus(cameraPosition);
 renderStage=1;Select();if(worldLogEnabled)selectMs=Clock()-stageStart;stageStart=worldLogEnabled?Clock():0;sceneUploadBudget.Begin(Clock(),initialWorldHold);
 auto pumpScene=[&](){renderStage=2;double begin=worldLogEnabled?Clock():0;Pump();if(begin)pumpMs+=Clock()-begin;};
#ifdef W2C_PUBLIC_BUILD
 pumpScene();
 if(!source_mw_tolls::placements.empty()){renderStage=3;TickNativeProps();}
 if(flow==6&&!initialWorldHold){renderStage=3;TickAmbientParticles();}
#else
 auto propsScene=[&](){renderStage=3;double begin=worldLogEnabled?Clock():0;TickNativeProps();TickAmbientParticles();if(begin)propsMs+=Clock()-begin;};
 if(frameCounter&1){propsScene();pumpScene();}else{pumpScene();propsScene();}
#endif
 }renderStage=4;D3DMATRIX clip{};for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)for(unsigned k=0;k<4;++k)clip.m[row][column]+=view.m[row][k]*projection.m[k][column];

 device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);device->SetTransform(D3DTS_WORLD,&identity);device->SetTransform(D3DTS_VIEW,&view);device->SetTransform(D3DTS_PROJECTION,&projection);
 device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
 if(editorStock){Pump();DrawRaceBarriers(clip,passPosition,mainView);return;}
 InstallRenderTiming();SetWorldTextureFiltering();bool environment=environmentInstalled&&BeginEnvironment(clip,passPosition,updateScene);if(environmentInstalled&&updateScene)TickEnvironmentMotions();if(environment&&envDepthPass)device->SetVertexShaderConstantF(9,&view._11,4);
 if(environmentInstalled&&!environment){device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1);}
 // Authored cameras retain the same zone visibility as normal driving.
 // Resident preload sections are not all visible sections.
 const RenderFrustum frustum(clip);auto pageStats=DrawPagedTerrain(passPosition,frustum,clip,environment,reflection);unsigned drawn=pageStats.sections,meshCount=pageStats.meshes,drawCalls=pageStats.calls,nightLayersDrawn=pageStats.nightDrawn,nightLayersSkipped=pageStats.nightSkipped,overlapSkipped=pageStats.overlapSkipped;
 if(environment||(!envDepthPass&&environmentInstalled))DrawEnvironmentMotions(clip,environment);renderStage=5;if(!envDepthPass){device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);}
#ifndef W2C_PUBLIC_BUILD
 DrawNativeProps(clip);
#endif
 if(!envDepthPass)DrawRaceBarriers(clip,passPosition,mainView);renderStage=6;++drawCounter;if(renderWorkProfiling){renderFrameSubmitMs+=Clock()-passStart;++renderFramePasses;}
 if(mainView&&!envDepthPass)ObserveInitialPresentationDraw(meshCount);
 if(envRoadReflectionPass&&meshCount){envRoadReflectionReady=true;static DWORD reported=0;DWORD now=GetTickCount();if(worldLogEnabled&&now-reported>10000){reported=now;Log("ROAD_REFLECTION_WORLD meshes=%u resident_world=1 recursion=0",meshCount);}}
 if(reflection&&worldLogEnabled){static DWORD reported[6]{};DWORD now=GetTickCount();if(meshCount&&now-reported[viewId-18]>10000){reported[viewId-18]=now;Log("REFLECTION_WORLD face=%u meshes=%u eye=%.1f,%.1f,%.1f submit_ms=%.3f",viewId-18,meshCount,passPosition.x,passPosition.y,passPosition.z,Clock()-passStart);}}
 static double mainSubmitTotal=0,depthSubmitTotal=0;static unsigned mainSubmitCount=0,depthSubmitCount=0;
 if(mainView&&worldRenderTelemetry){double elapsed=Clock()-passStart;static unsigned initialPasses=0;static DWORD reportedSlow=0;DWORD tick=GetTickCount();
  if(initialPasses++<8||(elapsed>(renderWorkProfiling?3:33)&&tick-reportedSlow>1000)){reportedSlow=tick;Log("MW_NEOAI_RENDER_PERF depth=%u total_ms=%.3f select_ms=%.3f pump_ms=%.3f props_ms=%.3f draw_ms=%.3f meshes=%u",unsigned(envDepthPass),elapsed,selectMs,pumpMs,propsMs,elapsed-selectMs-pumpMs-propsMs,meshCount);}
 }
 if(mainView&&worldLogEnabled){if(envDepthPass){depthSubmitTotal+=Clock()-passStart;++depthSubmitCount;}else{mainSubmitTotal+=Clock()-passStart;++mainSubmitCount;}}
 if(!mainView||envDepthPass)return;TrimTextures();if(!worldLogEnabled)return;DWORD now=GetTickCount();if(now-lastReport>2000){lastReport=now;Log("ENVIRONMENT_BATCH depth=%u meshes=%u draws=%u",unsigned(envDepthPass),meshCount,drawCalls);Log("WINDOW_LAYERS night=%.1f drawn=%u skipped_day=%u masked_material_binds=%u",envNight,nightLayersDrawn,nightLayersSkipped,envEmissiveMasks);Log("OVERLAP_FRAME suppressed_batches=%u",overlapSkipped);ReportNativePassCoherence();Log("ENVIRONMENT_PERF main_submit_ms=%.3f depth_submit_ms=%.3f color_frames=%u depth_frames=%u",mainSubmitCount?mainSubmitTotal/mainSubmitCount:0,depthSubmitCount?depthSubmitTotal/depthSubmitCount:0,mainSubmitCount,depthSubmitCount);mainSubmitTotal=depthSubmitTotal=0;mainSubmitCount=depthSubmitCount=0;size_t queued,completed;{std::lock_guard lock(ioMutex);queued=pending.size();completed=ready.size();}Log("STREAM frame=%u sections=%u meshes=%u resident_mb=%.2f textures=%u texture_mb=%.2f queued=%u ready=%u read_mean_ms=%.3f upload_mean_ms=%.3f camera=%.1f,%.1f,%.1f zone=%u",frameCounter,drawn/2,meshCount,resident/1048576.,unsigned(textures.size()),textureResident/1048576.,unsigned(queued),unsigned(completed),readCount?totalReadMs/readCount:0,uploadCount?totalUploadMs/uploadCount:0,cameraPosition.x,cameraPosition.y,cameraPosition.z,zoneIndex);if(environmentInstalled)Log("ENVIRONMENT_FRAME sunset=%u time=%.5f night=%.1f ambient=%.3f,%.3f,%.3f diffuse=%.3f,%.3f,%.3f animated=%u lit=%u flares=%u motions=%u vehicle_lights=%u",environment,envTOD,envNight,envAmbient[0],envAmbient[1],envAmbient[2],envDiffuse[0],envDiffuse[1],envDiffuse[2],envAnimationDraws,envLitDraws,envFlareDraws,envMotionDraws,envVehicleLights);}
}
// Native resource updates and custom draws are covered by one restore, including
// exception exits. Keep shader constants, samplers and the native car pipeline.
static void Render(void* targetView,unsigned flags){if(v2Lost)return;
 const auto flow=InitialGameFlow();
 if(flow!=6&&!(flow==5&&IsNativeScenePlaying()))return;
 static bool passCoherenceRetried=false;if(!passCoherenceRetried){passCoherenceRetried=true;InstallSunSetPassCoherence();}
 auto dev=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);if(!dev)return;
 IDirect3DStateBlock9* state=nullptr;double start=renderWorkProfiling?Clock():0;if(FAILED(dev->CreateStateBlock(D3DSBT_ALL,&state)))return;
 RenderTransformState transforms;transforms.Capture(dev);
 if(start)renderFrameStateMs+=Clock()-start;
 __try{RenderBody(targetView,flags);}__finally{
  double restore=renderWorkProfiling?Clock():0;
  if(!v2Lost){
   auto status=state->Apply();
   static DWORD lastAudit=0;
   auto audit=worldLogEnabled&&GetTickCount()-lastAudit>=2000;
   auto before=audit?transforms.Differences(dev):0;
   transforms.Restore(dev);
   if(audit){lastAudit=GetTickCount();Log("WORLD_TRANSFORM_RESTORE captured=%x after_block=%x after_explicit=%x state=%08X",transforms.valid,before,transforms.Differences(dev),unsigned(status));}
  }
  state->Release();if(restore)renderFrameStateMs+=Clock()-restore;
 }
}

#endif
static void TraceDrawPass(void* view,void* flags){auto dev=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};if(dev&&SUCCEEDED(dev->GetRenderTarget(0,&target))){target->GetDesc(&desc);target->Release();}sceneTargetFormat=desc.Format;if(!worldLogEnabled)return;auto rt=*reinterpret_cast<BYTE**>(0x982A20);unsigned viewId=view?*reinterpret_cast<unsigned*>(static_cast<BYTE*>(view)+4):~0u,rtId=rt?*reinterpret_cast<unsigned*>(rt+4):~0u;static std::set<std::array<unsigned,6>> calls;std::array<unsigned,6> key={unsigned(view),unsigned(flags),desc.Width,desc.Height,rtId,unsigned(desc.Format)};if(calls.insert(key).second)Log("DRAW_PASS view=%p id=%u flags=%p rt_id=%u target=%ux%u format=%u",view,viewId,flags,rtId,desc.Width,desc.Height,unsigned(desc.Format));}
static int RenderFault(EXCEPTION_POINTERS* fault){static DWORD previous=0;static unsigned count=0;DWORD now=GetTickCount();if(count++<8||now-previous>=1000){previous=now;Log("RENDER_FAULT code=%08X address=%p stage=%u ecx=%08X eax=%08X read=%p",fault->ExceptionRecord->ExceptionCode,fault->ExceptionRecord->ExceptionAddress,renderStage,fault->ContextRecord->Ecx,fault->ContextRecord->Eax,reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));}return EXCEPTION_EXECUTE_HANDLER;}
#ifndef MW_EDITOR_NATIVE_ONLY
#include "WorldShadowBridge.h"
#include "PlanarReflection.h"
#endif
static void V2InvalidateDeviceCaches(){envSurfaceSet=false;envDepthFrameReady=false;envShadowReady=false;envRoadReflectionReady=false;worldPendingShadowView=nullptr;worldPendingShadowTarget=nullptr;w2cPendingRoadView=nullptr;w2cPendingRoadTarget=nullptr;}
#ifdef MW_EDITOR_NATIVE_ONLY
static void __fastcall DrawHook(void* self,void*,void* view,void* flags){
 nativeOriginalDraw(self,view,flags);
 __try{if(view==reinterpret_cast<void*>(0x919650)){Render(view,unsigned(flags));}}
 __except(RenderFault(GetExceptionInformation())){}
}
#else
static void __fastcall DrawHook(void* self,void*,void* view,void* flags){if(v2Lost)return;if(editorStock)nativeOriginalDraw(self,view,flags);__try{AmbientFlow();TraceDrawPass(view,flags);if(view==reinterpret_cast<void*>(0x919650)){
#ifdef W2C_PUBLIC_BUILD
 static bool sunsetChecked=false;
 if(!editorStock&&!sunsetChecked&&GetModuleHandleA("SunSet.asi")){sunsetChecked=true;InstallSunSetCompatibility();}
#endif
 TickWorldRadar();}
 // MW's six vehicle cubemap faces are views 18..23, not a rear mirror.
 // All faces must contain the same resident source world as the player.
 auto address=unsigned(view);bool cube=address>=0x919DC0&&address<=0x919FF0&&(address-0x919DC0)%0x70==0;
 // Verified HD Reflections rear-view target: eView 3 at 00919730.
 // Reuse resident scenery and pass-local camera matrices; never stream twice.
 const bool mirror=view==reinterpret_cast<void*>(0x919730)&&*reinterpret_cast<unsigned*>(static_cast<BYTE*>(view)+4)==3;
 if(mirror&&worldLogEnabled&&GetTickCount()-w2cMirrorAuditLast>10000){w2cMirrorAuditLast=GetTickCount();w2cMirrorAuditPending=true;TraceMirrorSurface("before_custom_world");}
 // Native road reflections use eView 4, separate from cubemaps and the rear mirror.
 const bool roadReflection=view==reinterpret_cast<void*>(0x9197A0)&&*reinterpret_cast<unsigned*>(static_cast<BYTE*>(view)+4)==4;
 if(view!=reinterpret_cast<void*>(0x919650)&&!cube&&!mirror&&!roadReflection)return;if(roadReflection){DrawExternalEventScene(view);QueueRoadReflection(view,unsigned(flags));return;}if(view==reinterpret_cast<void*>(0x919650)){if(!editorStock){TickEventBridge();TraceEventBarrierCull(self,view);}}DrawExternalEventScene(view);if(!editorStock){DrawEventBarriers(self,view,flags);SubmitPursuitPieces(view,unsigned(flags));}Render(view,unsigned(flags));if(view==reinterpret_cast<void*>(0x919650))TickInitialWorldGate();
 }__except(RenderFault(GetExceptionInformation())){}}
#endif
#pragma comment(linker,"/EXPORT:W2MW_IsWorldActiveV1=_W2MW_IsWorldActiveV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_IsWorldActiveV1(){
 bool blocked=initialWorldHold;
#ifndef MW_EDITOR_NATIVE_ONLY
 blocked=blocked&&initialHoldKind!=InitialHoldKind::Scene;
#endif
 return loaderActive&&!propNativeReference&&!blocked&&*reinterpret_cast<unsigned*>(0x925E90)==6;
}
#pragma comment(linker,"/EXPORT:W2MW_InitialPresentationFrameV1=_W2MW_InitialPresentationFrameV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_InitialPresentationFrameV1(){
#ifndef MW_EDITOR_NATIVE_ONLY
 return presentationPending||initialHoldKind==InitialHoldKind::Scene?presentationFrame:0;
#else
 return 0;
#endif
}
#pragma comment(linker,"/EXPORT:W2MW_RequestScenePreparationV1=_W2MW_RequestScenePreparationV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_RequestScenePreparationV1(const mw_world_load::Plan* plan,unsigned bytes){
#ifndef MW_EDITOR_NATIVE_ONLY
 return RequestScenePreparation(plan,bytes);
#else
 return 0;
#endif
}
#pragma comment(linker,"/EXPORT:W2MW_IsScenePreparationReadyV1=_W2MW_IsScenePreparationReadyV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_IsScenePreparationReadyV1(unsigned generation){
#ifndef MW_EDITOR_NATIVE_ONLY
 return generation&&generation==presentationSceneGeneration&&!initialWorldHold&&W2MW_IsWorldActiveV1();
#else
 return W2MW_IsWorldActiveV1();
#endif
}
#include "EventGeometryService.h"
static thread_local double geometryQueryStart=0;
static thread_local CollisionWorld::ProfileCounters geometryProfileBefore{};
static void GeometryProfileBegin(unsigned previous){if(renderWorkProfiling&&previous==0){collision.profileEnabled=true;geometryProfileBefore=collision.profile;geometryQueryStart=Clock();}}
static void GeometryProfileEnd(unsigned previous){if(previous==0&&geometryQueryStart){double elapsed=Clock()-geometryQueryStart;renderFrameQueryMs+=elapsed;++renderFrameQueries;geometryQueryStart=0;static DWORD last=0;DWORD now=GetTickCount();if(worldLogEnabled&&elapsed>3&&now-last>=1000){last=now;const auto& p=collision.profile;const auto& b=geometryProfileBefore;Log("W2C_QUERY_PROFILE ms=%.3f lookups=%u hits=%u fallback=%u probes=%u rays=%u triangles=%u boxes=%u candidate_rays=%u box_cache_hits=%u",elapsed,p.lookups-b.lookups,p.hits-b.hits,p.fallbacks-b.fallbacks,p.probes-b.probes,p.rays-b.rays,p.triangles-b.triangles,p.boxes-b.boxes,p.candidateRays-b.candidateRays,p.boxCacheHits-b.boxCacheHits);}}}
extern "C" __declspec(dllexport) unsigned W2MWLoaderVersion(){return propNativeReference?2:(loaderActive?Version:0);}
#pragma comment(linker,"/EXPORT:W2CMWv2_SourceAssetsV1=_W2CMWv2_SourceAssetsV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2CMWv2_SourceAssetsV1(){return sourceRuntime?1u:0u;}
#ifndef W2C_PUBLIC_BUILD
#include "SourceDeployment.h"
#endif
extern "C" __declspec(dllexport) void InitializeASI(){static bool done=false;if(done)return;done=true;GetModuleFileNameA(nullptr,root,MAX_PATH);auto end=strrchr(root,'\\');if(!end)return;end[1]=0;
#ifndef W2C_PUBLIC_BUILD
bool editorEnabled=GetFileAttributesA(Path("MWEventEditor/runtime.json").c_str())!=INVALID_FILE_ATTRIBUTES;
#ifndef MW_EDITOR_NATIVE_ONLY
editorStock=editorEnabled&&GetFileAttributesA(Path("W2CMWv2/World/L5RA.w2i").c_str())==INVALID_FILE_ATTRIBUTES;
#endif
if(!SourceDeploymentAllowed())return;
#endif
 if(GetModuleHandleA("World2MWNativeLoader.asi")||GetModuleHandleA("AAA_World2MWProbe.asi"))return;
 QueryPerformanceFrequency(&frequency);InitWorldDebugLog(Path("W2CMWv2.ini").c_str(),Path("W2CMWv2.log").c_str());v2ReadBudget.measure(worldLogEnabled);v2StageBudget.measure(worldLogEnabled);v2MetadataBudget.measure(worldLogEnabled);v2GeometryBudget.measure(worldLogEnabled);v2TextureBudget.measure(worldLogEnabled);
 if(GetModuleHandleA(nullptr)!=reinterpret_cast<HMODULE>(0x400000)||!SupportedExe()){Log("REFUSED executable identity");return;}
#ifdef W2C_PUBLIC_BUILD
 if(!SourceDeploymentAllowed())return;
#endif
 event_geometry_service::TraceBegin=GeometryProfileBegin;event_geometry_service::TraceEnd=GeometryProfileEnd;
 worldRenderTelemetry=WorldDiagnosticFlag(Path("W2CMWv2.ini").c_str(),"Renderer","FrameTelemetry");
 InstallWorldFileTrace();
 Log("NEOAI_SEPARATED provider=EventASI legacy_neo_provider=0 legacy_neo_finish_hooks=0");
#ifndef W2C_PUBLIC_BUILD
 if(!editorStock&&InitPropNativeReference())return;
#endif
 if(!InstallCheckpointExtension())return;
 if(!editorStock&&!InstallRaceGridCompatibility())return;
 if(editorStock){
  auto mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED){Log("REFUSED MinHook");return;}
  // Stock worlds obtain authored event barriers from the independent Event ASI.
  raceBarriersLoaded=true;eventBarrierCollision=RaceBarrierContacts;
  if(!InstallWorldRaceNavigation())return;
  const BYTE guard[]={0x83,0xEC,0x14,0x53,0x8B,0x5C,0x24,0x20,0x56};auto draw=reinterpret_cast<void*>(0x723FA0);
  if(memcmp(draw,guard,sizeof(guard))||MH_CreateHook(draw,DrawHook,reinterpret_cast<void**>(&nativeOriginalDraw))!=MH_OK||MH_EnableHook(draw)!=MH_OK){Log("REFUSED editor draw hook");return;}
  if(!InstallStockEditorCollision()){MH_DisableHook(draw);MH_RemoveHook(draw);Log("REFUSED editor wall collision");return;}
  if(!StartStreaming()){Log("REFUSED editor worker");return;}loaderActive=true;
  Log("MW_EDITOR_RUNTIME_ACTIVE stock_world_preserved=1 native_ground_preserved=1 web_schema=1 world_streaming=0 world_hooks=0");return;
 }

#ifndef MW_EDITOR_NATIVE_ONLY
 if(!V2InitializePacks()||!LoadSourceVisibility())return;
#ifdef W2C_PUBLIC_BUILD
 if(!InstallSourcePublicPursuitVault())return;
#endif
#ifndef W2C_PUBLIC_BUILD
 InstallPalmontRegions();
#endif
 if(GetModuleHandleA("NFSMWMapLoader.asi")){Log("REFUSED both map loaders enabled");return;}
 // Keep the installed MOD chain's D3DX load order. Unlimiter's startup
 // patches stalled without this preload in the current sandbox (trial 163).
 // DDS decoding and uploads below are still implemented by this loader.
 if(!LoadLibraryA("d3dx9_43.dll")){Log("REFUSED D3DX compatibility preload");return;}

 auto mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED){Log("REFUSED MinHook");return;}
 if(!InstallSourceLegacyDiscovery())return;
 if(!InstallSourceNativeResources()||!InstallInitialWorldGate())return;
 if(!InstallWorldRaceNavigation())return;
 InstallW2CHeliAltitude();
 InstallW2CWeatherCover();
 InstallNativeMaterialGuard();InstallSunSetPassCoherence();InstallNativeScenePass();InstallPalmontFacilities();
 static const BYTE sceneryGuard[]={0x83,0xEC,0x14,0x53,0x8B,0x5C,0x24,0x20,0x56};BYTE* draw=reinterpret_cast<BYTE*>(0x723FA0);if(memcmp(draw,sceneryGuard,sizeof(sceneryGuard))||*reinterpret_cast<BYTE*>(0x7310B0)!=0x51){Log("REFUSED scenery hook guard");return;}
 if(MH_CreateHook(draw,DrawHook,reinterpret_cast<void**>(&nativeOriginalDraw))!=MH_OK){Log("REFUSED scenery hook");return;}
 if(MH_EnableHook(draw)!=MH_OK){MH_RemoveHook(draw);Log("REFUSED scenery hook activation");return;}
 if(!InstallNativeCollision()){Log("REFUSED native collision bridge");MH_DisableHook(draw);MH_RemoveHook(draw);return;}
 // Converted spawners provide their own native +0x34 model interface.
 // Keep the original scenery stream suppressed to avoid duplicate props.
 DWORD old=0;BYTE ret=0xC3;if(!VirtualProtect(reinterpret_cast<void*>(0x7310B0),1,PAGE_EXECUTE_READWRITE,&old)){Log("REFUSED original scenery suppression");UninstallNativeCollision();MH_DisableHook(draw);MH_RemoveHook(draw);return;}memcpy(reinterpret_cast<void*>(0x7310B0),&ret,1);VirtualProtect(reinterpret_cast<void*>(0x7310B0),1,old,&old);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x7310B0),1);
 envMirrorShadowReceiver=GetPrivateProfileIntA("Renderer","MirrorShadowReceiver",1,Path("W2CMWv2.ini").c_str())!=0;Log("MIRROR_SHADOW_RECEIVER enabled=%u",unsigned(envMirrorShadowReceiver));
 envBoundedLights=GetPrivateProfileIntA("Renderer","BoundedDynamicLights",1,Path("W2CMWv2.ini").c_str())!=0;LoadEnvironment();V2EffectiveMaterials();environmentInstalled=!envMaterials.empty();InstallWorldShadowHook();
#ifdef W2C_PUBLIC_BUILD
 if(!source_mw_tolls::placements.empty())InitNativeProps();
 InitAmbientParticles();
 InstallPublicPalmontDistricts();
 InitEventBridge();
#else
 InitNativeProps();InitAmbientParticles();InitEventBridge();
#endif
 if(!StartStreaming()){Log("REFUSED streaming worker");DWORD access;if(VirtualProtect(reinterpret_cast<void*>(0x7310B0),1,PAGE_EXECUTE_READWRITE,&access)){*reinterpret_cast<BYTE*>(0x7310B0)=0x51;VirtualProtect(reinterpret_cast<void*>(0x7310B0),1,access,&access);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x7310B0),1);}UninstallNativeCollision();MH_DisableHook(draw);MH_RemoveHook(draw);return;}loaderActive=true;W2V2Status.flags|=4;Log("ACTIVE custom renderer sections=%u materials=%u zones=%u legacy_collision=0; runtime FBX parsing disabled",unsigned(sections.size()),unsigned(materials.size()),unsigned(zones.size()));
#endif
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID reserved){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);InitializeASI();}if(reason==DLL_PROCESS_DETACH&&reserved==nullptr){std::lock_guard lock(ioMutex);quit=true;ioSignal.notify_all();}return TRUE;}



