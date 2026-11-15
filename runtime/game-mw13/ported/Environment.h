#pragma once
#include <d3dx9effect.h>
#include "RenderLightBounds.h"
#include "EnvironmentWindowLayers.h"
// Private Source metadata bridge. No runtime FBX or YAML parsing.
struct EnvironmentMaterial {int scroll=0,step=0,speedS=0,speedT=0,offsetS=0,offsetT=0,scaleS=0,scaleT=0;unsigned mask=0;float rgb[3]{1,1,1},brightness=0;int prelit=0,always=0,vertexColor=0;};
struct EnvironmentAnimation {unsigned fps,base;std::vector<unsigned> frames;};
struct EnvironmentLight {Vec3 position,color;float intensity,range;Vec3 direction;unsigned flags;float inner=1,outer=-1;unsigned blink=0;};
struct EnvironmentFlare {Vec3 position;unsigned tint,type,section;Vec3 direction;};
struct NativeEnvironmentFlare {void* next=nullptr;void* prev=nullptr;unsigned hash=0,tint=0;Vec3 position{};float reflectZ=0;Vec3 direction{0,0,1};BYTE type=0,flags=2;unsigned short section=0;};
static_assert(sizeof(EnvironmentFlare)==36&&sizeof(NativeEnvironmentFlare)==48);
static std::vector<EnvironmentFlare> envFlares;
static std::map<std::pair<int,int>,std::vector<unsigned>> envFlareGrid;
static unsigned envFlareDraws=0;
// Source model records stay 36 bytes; the extra GPU-only exposure value is
// derived from source shelter zones, without repacking any world cache.
static_assert(sizeof(NormalColor)==16&&sizeof(EnvironmentVertex)==40&&sizeof(EnvironmentLight)==60);
static std::map<unsigned,EnvironmentMaterial> envMaterials;
static std::vector<EnvironmentAnimation> envAnimations;
static std::map<unsigned,std::pair<unsigned,unsigned>> envFrames;
static std::vector<EnvironmentLight> envLights;
static std::map<std::pair<int,int>,std::vector<unsigned>> envLightGrid;
static IDirect3DVertexShader9* envVS;static IDirect3DPixelShader9* envPS;static IDirect3DVertexDeclaration9* envDecl;
static bool envCompiled=false,envAvailable=false;static float envWorldTime=0,envRealTime=0,envTOD=.5f,envNight=0;
static bool envDepthPass=false,envDepthFrameReady=false;
static IDirect3DVertexShader9* envDepthVS=nullptr;
static IDirect3DPixelShader9* envDepthPS=nullptr;
static IDirect3DVertexDeclaration9* envDepthPropDecl=nullptr;
static std::set<unsigned> envNightOverlays;
static float envDiffuse[4]{.6f,.6f,.6f,1},envAmbient[4]{.5f,.5f,.5f,0},envSun[4]{0,0,1,0},envFog[4]{},envFogSun[4]{},envFogValue[4]{1000,.001f,0,1};
static float envLightsOn=.65f,envLightsOff=.24f;static unsigned envAnimationDraws=0,envLitDraws=0,envEmissiveMasks=0;
#ifdef W2C_PUBLIC_BUILD
#include "PublicLightingPolicy.h"
static float PublicShadeWeight(){
 return w2c_public_light::DaylightWeight(envSun[2],envTOD>=envLightsOff&&envTOD<=envLightsOn,w2cPlayerInTunnel);
}
#endif
#include "EnvironmentRoadBridge.h"
struct EnvironmentNativeSpot {Vec3 position,direction,color;float range,intensity,inner,outer,specular;unsigned source;void* flare;float flareIntensity,distance;bool added;BYTE padding[3];};
static_assert(sizeof(EnvironmentNativeSpot)==0x4C);
static unsigned envVehicleLights=0;
static float envShadeHeadlightBoost=0.f;
static float envDP[16][4]{},envDD[16][4]{},envDC[16][4]{};
static unsigned envDynamicMask=~0u;
static unsigned envSurfaceHash=0;static bool envSurfaceSet=false;
static bool envBoundedLights=true;
static unsigned EnvironmentDynamicMask(const Mesh* bounds){
 unsigned mask=0;
 for(unsigned n=0;n<envVehicleLights;++n){
  if(envDC[n][0]==0&&envDC[n][1]==0&&envDC[n][2]==0)continue;
  if(!bounds||RenderLightIntersects({envDP[n][0],envDP[n][1],envDP[n][2]},std::abs(envDP[n][3]),bounds->minimum,bounds->maximum))mask|=1u<<n;
 }
 return mask;
}
static void SetEnvironmentDynamicLights(const Mesh* bounds){
 if(envDepthPass||!envBoundedLights)return;
 const unsigned mask=EnvironmentDynamicMask(bounds);
 if(mask==envDynamicMask)return;envDynamicMask=mask;
 float pos[16][4]{},dir[16][4]{},color[16][4]{};unsigned count=0;
 for(unsigned n=0;n<envVehicleLights;++n)if(mask&(1u<<n)){memcpy(pos[count],envDP[n],16);memcpy(dir[count],envDD[n],16);memcpy(color[count],envDC[n],16);++count;}
 if(count){device->SetPixelShaderConstantF(33,pos[0],count);device->SetPixelShaderConstantF(49,dir[0],count);device->SetPixelShaderConstantF(65,color[0],count);}
 float settings[4]{float(count),envShadeHeadlightBoost,0,0};device->SetPixelShaderConstantF(81,settings,1);
}
#ifdef W2C_PUBLIC_BUILD
#include "PublicTunnelHeadlights.h"
#include "PublicTunnelCeilingLights.h"
static unsigned publicDayShadeNativeLights=0;
#endif
static bool ReadEnvironmentVehicleLightsUnsafe(Vec3 eye){
 envVehicleLights=0;
 auto mod=GetModuleHandleA("SunSet.asi");
 if(!mod)return false;
 auto nt=(IMAGE_NT_HEADERS*)((BYTE*)mod+((IMAGE_DOS_HEADER*)mod)->e_lfanew);
 if(nt->FileHeader.TimeDateStamp!=0x6A4A2A2E||nt->OptionalHeader.SizeOfImage!=0x41000)return false;
 unsigned count=*(unsigned*)((BYTE*)mod+0x30C9C);
 if(count>512)return false;
 auto lights=(EnvironmentNativeSpot*)((BYTE*)mod+0x32830);
 std::array<std::pair<float,unsigned>,512> sorted{};
 unsigned used=0;
 for(unsigned n=0;n<count;++n){
  const auto& light=lights[n];
  if(!light.source||light.source>6||!std::isfinite(light.range)||light.range<=0||
     light.range>500||!std::isfinite(light.intensity)||light.intensity<=0||light.intensity>100)continue;
  if(!std::isfinite(light.position.x)||!std::isfinite(light.position.y)||!std::isfinite(light.position.z)||
     !std::isfinite(light.direction.x)||!std::isfinite(light.direction.y)||!std::isfinite(light.direction.z)||
     !std::isfinite(light.color.x)||!std::isfinite(light.color.y)||!std::isfinite(light.color.z)||
     !std::isfinite(light.inner)||!std::isfinite(light.outer))continue;
  auto d=Sub(light.position,eye);
  sorted[used++]={Dot(d,d)-(light.source==1||light.source==4?1000000.f:0.f),n};
 }
 std::sort(sorted.begin(),sorted.begin()+used);
 envVehicleLights=std::min(16u,used);
#ifdef W2C_PUBLIC_BUILD
 publicTunnelNativeLights=0;
#endif
 for(unsigned n=0;n<envVehicleLights;++n){
  const auto& light=lights[sorted[n].second];
  float range=light.range;
  float intensity=light.intensity;
  float rangeSign=1.f;
#ifdef W2C_PUBLIC_BUILD
  if(light.source==1){
   const float strongest=std::max({light.color.x,light.color.y,light.color.z})*intensity;
   if(strongest>.02f&&publicTunnelHeadlightBlend>0.f){
    ++publicTunnelNativeLights;
    intensity*=w2c_public_light::TunnelHeadlightGain(strongest,publicTunnelHeadlightBlend);
    range=std::lerp(range,std::max(range,62.f),publicTunnelHeadlightBlend);
   }else if(strongest>0.f&&envShadeHeadlightBoost>0.f){
    rangeSign=-1.f;
    ++publicDayShadeNativeLights;
   }
  }
#endif
  envDP[n][0]=light.position.x;envDP[n][1]=light.position.y;envDP[n][2]=light.position.z;envDP[n][3]=rangeSign/range;
  envDD[n][0]=light.direction.x;envDD[n][1]=light.direction.y;envDD[n][2]=light.direction.z;
  envDD[n][3]=std::cos(light.outer*.01745329252f);
  envDC[n][0]=light.color.x*intensity;envDC[n][1]=light.color.y*intensity;envDC[n][2]=light.color.z*intensity;
  envDC[n][3]=std::cos(light.inner*.01745329252f);
 }
 return true;
}
static void ReadEnvironmentVehicleLights(Vec3 eye){
#ifdef W2C_PUBLIC_BUILD
 publicTunnelNativeLights=publicTunnelFillLights=publicDayShadeNativeLights=0;
 publicDayCeilingDraws=0;
 UpdatePublicTunnelHeadlightBlend();
 envShadeHeadlightBoost=envAvailable&&publicTunnelHeadlightBlend==0.f?.28f*PublicShadeWeight():0.f;
#endif
 __try{
  const bool readable=ReadEnvironmentVehicleLightsUnsafe(eye);
#ifdef W2C_PUBLIC_BUILD
  if(readable&&envAvailable&&publicTunnelHeadlightBlend>0.f&&!publicTunnelNativeLights&&AddPublicTunnelHeadlights()){
   publicTunnelFillLights=2;
  }
#endif
 }__except(EXCEPTION_EXECUTE_HANDLER){envVehicleLights=0;}
#ifdef W2C_PUBLIC_BUILD
 if(worldLogEnabled){
  static DWORD lastReport=0;
  const DWORD now=GetTickCount();
  if(now-lastReport>=2000){
   lastReport=now;
   Log("PUBLIC_WORLD_LIGHTING daylight_fill=%.2f tunnel=%u blend=%.2f native_headlights=%u fill_headlights=%u shade_headlights=%u shade_boost=%.2f",
       envAvailable?PublicShadeWeight():0.f,unsigned(w2cPlayerInTunnel),publicTunnelHeadlightBlend,
       publicTunnelNativeLights,publicTunnelFillLights,publicDayShadeNativeLights,envShadeHeadlightBoost);
  }
 }
#endif
}
// The shipped LTCG binary specializes its local call to ECX (PDB + disassembly),
// despite the source-level stdcall declaration. Match the actual machine ABI.
static void (__fastcall* envSunSetParamsOriginal)(BYTE*,void*);
static void __fastcall EnvironmentSunSetParams(BYTE* model,void*){if(model&&!*(void**)(model+0x24)){auto white=reinterpret_cast<void*(__cdecl*)(unsigned,bool,bool)>(0x503400)(0xE2C7C5E2,false,true);if(!white)white=*(void**)0x916040;if(!white)return;*(void**)(model+0x24)=white;}envSunSetParamsOriginal(model,nullptr);}
static void InstallEnvironmentFlareHook();
#ifdef W2C_PUBLIC_BUILD
static void* envSunSetSkyTechniqueOriginal=nullptr;
// The lighting selector may return Unlit for a sky render model. SunSet's
// sky effect has no Unlit technique; SetTechnique(null) leaves the preceding
// ZPrePass selected. Use the effect's named main pass for the color sky.
static void __declspec(naked) SelectPublicSunSetSkyTechnique(){__asm{
 cmp dword ptr [edi+4],13h
 jne originalSelection
 cmp ebx,0Ah
 je originalSelection
 mov ebx,-1
 originalSelection:
 jmp [envSunSetSkyTechniqueOriginal]
}}
#endif
static void InstallSunSetCompatibility(){
 InstallEnvironmentFlareHook();if(envSunSetParamsOriginal)return;
 auto mod=reinterpret_cast<BYTE*>(GetModuleHandleA("SunSet.asi"));if(!mod)return;
 auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(mod+reinterpret_cast<IMAGE_DOS_HEADER*>(mod)->e_lfanew);
 if(nt->FileHeader.TimeDateStamp!=0x6a4a2a2e||nt->OptionalHeader.SizeOfImage!=0x41000){Log("SUNSET_GUARD_REFUSED timestamp=%08X size=%08X",nt->FileHeader.TimeDateStamp,nt->OptionalHeader.SizeOfImage);return;}
 auto target=mod+0xf910;const BYTE guard[]={0x55,0x8B,0xEC,0x83,0xE4,0xF8,0x83,0xEC,0x28};
 if(memcmp(target,guard,sizeof(guard))){Log("SUNSET_GUARD_REFUSED prologue");return;}
 if(MH_CreateHook(target,EnvironmentSunSetParams,reinterpret_cast<void**>(&envSunSetParamsOriginal))==MH_OK&&MH_EnableHook(target)==MH_OK){
#ifdef W2C_PUBLIC_BUILD
  auto selection=mod+0xfd4b;
  const BYTE selectionGuard[]={0x3B,0xCF,0x75,0x0C,0x39,0x1D};
  if(!memcmp(selection,selectionGuard,sizeof(selectionGuard))&&
     *reinterpret_cast<BYTE**>(selection+6)==mod+0x3009c&&
     MH_CreateHook(selection,SelectPublicSunSetSkyTechnique,&envSunSetSkyTechniqueOriginal)==MH_OK){
   if(MH_EnableHook(selection)==MH_OK)Log("PUBLIC_SUNSET_SKY_TECHNIQUE_ACTIVE rva=FD4B");
   else {MH_RemoveHook(selection);envSunSetSkyTechniqueOriginal=nullptr;}
  }
  if(!envSunSetSkyTechniqueOriginal)Log("SUNSET_GUARD_REFUSED sky technique site");
#endif
  Log("SUNSET_NULL_TEXTURE_GUARD_ACTIVE rva=F910");return;
 }
 MH_RemoveHook(target);envSunSetParamsOriginal=nullptr;
 Log("SUNSET_GUARD_REFUSED shader parameter hook");
}
// Bytecode is generated by MSBuild; no shader compilation during gameplay.
#include "EnvironmentShaderVS.generated.h"
#include "EnvironmentShaderPS.generated.h"
#include "EnvironmentShaderVSDepth.generated.h"
#include "EnvironmentShaderPSDepth.generated.h"
#include "EnvironmentShaderVSShadow.generated.h"
#include "EnvironmentShaderPSShadow.generated.h"
#include "EnvironmentShaderPSPropColour.generated.h"
#include "EnvironmentShadow.h"
static IDirect3DPixelShader9* envPropColourPS=nullptr;
static void BindEnvironmentPropColour(){if(!envPropColourPS)device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPSPropColour),&envPropColourPS);if(envPropColourPS)device->SetPixelShader(envPropColourPS);}

#include "SourceEnvironment.h"
static void LoadEnvironment(){if(sourceRuntime){LoadSourceEnvironment();return;}FILE* f=nullptr;fopen_s(&f,Path("Environment/materials.txt").c_str(),"r");if(f){unsigned hash;EnvironmentMaterial m;while(fscanf_s(f,"%x %d %d %d %d %d %d %d %d %x %f %f %f %f %d %d %d",&hash,&m.scroll,&m.step,&m.speedS,&m.speedT,&m.offsetS,&m.offsetT,&m.scaleS,&m.scaleT,&m.mask,&m.rgb[0],&m.rgb[1],&m.rgb[2],&m.brightness,&m.prelit,&m.always,&m.vertexColor)==17)envMaterials[hash]=m;fclose(f);}
 fopen_s(&f,Path("Environment/animations.txt").c_str(),"r");if(f){unsigned count,fps,base;while(fscanf_s(f,"%u %u %u",&count,&fps,&base)==3){if(!count||count>255||!fps||fps>255||base>1)break;EnvironmentAnimation a{fps,base,{}};for(unsigned n=0;n<count;++n){unsigned hash;if(fscanf_s(f,"%x",&hash)!=1){fclose(f);return;}a.frames.push_back(hash);envFrames[hash]={unsigned(envAnimations.size()),n};}envAnimations.push_back(std::move(a));}fclose(f);}
 fopen_s(&f,Path("Environment/lights.bin").c_str(),"rb");if(f){unsigned magic,count;if(fread(&magic,4,1,f)==1&&fread(&count,4,1,f)==1&&magic==0x324C4D57&&count<100000){envLights.resize(count);if(fread(envLights.data(),sizeof(EnvironmentLight),count,f)!=count||fgetc(f)!=EOF)envLights.clear();}fclose(f);}for(unsigned n=0;n<envLights.size();++n){auto& l=envLights[n];if(!std::isfinite(l.position.x)||!std::isfinite(l.range)||l.range<=0||l.range>500){envLights.clear();envLightGrid.clear();break;}envLightGrid[{int(std::floor(l.position.x/64)),int(std::floor(l.position.y/64))}].push_back(n);}
 fopen_s(&f,Path("Environment/flares.bin").c_str(),"rb");if(f){unsigned magic,count;if(fread(&magic,4,1,f)==1&&fread(&count,4,1,f)==1&&magic==0x32464D57&&count<100000){envFlares.resize(count);if(fread(envFlares.data(),sizeof(EnvironmentFlare),count,f)!=count||fgetc(f)!=EOF)envFlares.clear();}fclose(f);}for(unsigned n=0;n<envFlares.size();++n){auto& l=envFlares[n];if(l.type>20||!std::isfinite(l.position.x)){envFlares.clear();envFlareGrid.clear();break;}envFlareGrid[{int(std::floor(l.position.x/64)),int(std::floor(l.position.y/64))}].push_back(n);}
 fopen_s(&f,Path("Environment/night-overlays.txt").c_str(),"r");if(f){unsigned hash;while(fscanf_s(f,"%x",&hash)==1)if(MaterialOf(hash).blend==1||MaterialOf(hash).blend==2)envNightOverlays.insert(hash);fclose(f);}for(auto hash:environmentWindowLayers)if(MaterialOf(hash).blend==1||MaterialOf(hash).blend==2)envNightOverlays.insert(hash);for(auto hash:envNightOverlays){auto& material=envMaterials[hash];if(material.brightness==0){material.brightness=1.5f;material.prelit=1;material.always=0;material.vertexColor=0;}}Log("ENVIRONMENT_NIGHT_OVERLAYS count=%u emissive_fallback=1",unsigned(envNightOverlays.size()));
 Log("ENVIRONMENT_SOURCE materials=%u animations=%u frames=%u lights=%u flares=%u",unsigned(envMaterials.size()),unsigned(envAnimations.size()),unsigned(envFrames.size()),unsigned(envLights.size()),unsigned(envFlares.size()));}
static bool ReadEnvironmentNormals(unsigned id,CpuSection& data,std::vector<NormalColor>& normals){char name[100];sprintf_s(name,"Environment/Normals/%u.nrm",id);FILE* f=nullptr;fopen_s(&f,Path(name).c_str(),"rb");if(!f)return false;unsigned h[4]{};uint64_t sourceDigest=0,digest=14695981039346656037ull;auto add=[&](const void* p,size_t n){auto b=(const BYTE*)p;for(size_t i=0;i<n;++i){digest^=b[i];digest*=1099511628211ull;}};for(auto& v:data.vertices){add(&v.position,12);add(&v.u,8);}add(data.indices.data(),data.indices.size()*4);bool ok=fread(h,16,1,f)==1&&fread(&sourceDigest,8,1,f)==1&&h[0]==0x314E4D57&&h[1]==id&&h[2]==data.header.vertices&&h[3]==data.header.indices&&sourceDigest==digest;if(ok){normals.resize(h[2]);ok=fread(normals.data(),16,h[2],f)==h[2]&&fgetc(f)==EOF;}fclose(f);if(!ok){normals.clear();Log("ENVIRONMENT_NORMAL_REFUSED section=%u source=%016llX current=%016llX",id,sourceDigest,digest);}return ok;}
static bool InitEnvironmentShaders(){
 if(envCompiled)return envVS&&envPS;envCompiled=true;
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,36,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
 bool ok=SUCCEEDED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(envShaderVS),&envVS))&&SUCCEEDED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPS),&envPS))&&SUCCEEDED(device->CreateVertexDeclaration(elements,&envDecl));
 Log("ENVIRONMENT_SHADER %s precompiled=1",ok?"ACTIVE":"FAILED");return ok;
}
static bool InitEnvironmentDepthShaders(){
 static bool tried=false;if(tried)return envDepthVS&&envDepthPS&&envDepthPropDecl;tried=true;
 const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
 bool ok=SUCCEEDED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(envShaderVSDepth),&envDepthVS))&&SUCCEEDED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPSDepth),&envDepthPS))&&SUCCEEDED(device->CreateVertexDeclaration(decl,&envDepthPropDecl));
 Log("ENVIRONMENT_DEPTH_SHADER %s linear_view_depth=1 lighting=0 precompiled=1",ok?"ACTIVE":"FAILED");return ok;
}
static void SetEnvironmentPropDepth(const D3DMATRIX& world){device->SetVertexDeclaration(envDepthPropDecl);device->SetVertexShader(envDepthVS);device->SetPixelShader(envDepthPS);device->SetVertexShaderConstantF(5,&world._11,4);float uv[4]{0,0,1,1};device->SetVertexShaderConstantF(4,uv,1);}
static bool ReadSunSetUniforms(){if(!GetModuleHandleA("SunSet.asi"))return false;__try{auto e=*(BYTE**)0x93DE78;if(!e||*(unsigned*)(e+4)!=0)return false;auto fx=*(ID3DXEffect**)(e+0x48);if(!fx)return false;auto get=[&](const char* name,float* value){auto handle=fx->GetParameterByName(nullptr,name);return handle&&SUCCEEDED(fx->GetVector(handle,(D3DXVECTOR4*)value));};if(!get("cvDiffuseColor",envDiffuse)||!get("cvAmbientColor",envAmbient))return false;
 get("cvFogColor",envFog);get("cvFogSunColor",envFogSun);get("cvFogValue",envFogValue);auto tod=*(BYTE**)0x9B392C;if(!tod)return false;{envTOD=*(float*)(tod+8);memcpy(envSun,tod+0x180,16);float length=std::sqrt(envSun[0]*envSun[0]+envSun[1]*envSun[1]+envSun[2]*envSun[2]);if(length>.0001f)for(unsigned n=0;n<3;++n)envSun[n]/=length;}return std::isfinite(envTOD)&&envTOD>=0&&envTOD<=1;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool envLocalSet=false;static std::array<unsigned,8> envLocalIndices{};
static bool BeginEnvironment(const D3DMATRIX& clip,Vec3 eye,bool mainView){if(worldLogEnabled)envEmissiveMasks=0;if(mainView){InstallEnvironmentFlareHook();envRealTime=GetTickCount()*.001f;envWorldTime=reinterpret_cast<float(__cdecl*)()>(0x6E8DE0)();envAvailable=ReadSunSetUniforms();
#ifdef W2C_PUBLIC_BUILD
 if(envAvailable)w2c_public_light::BalanceDaylight(envDiffuse,envAmbient,1.80f,PublicShadeWeight());
#endif
 ReadEnvironmentVehicleLights(eye);envNight=envAvailable&&(envTOD>envLightsOn||envTOD<envLightsOff)?1.f:0.f;envAnimationDraws=envLitDraws=0;}if(!envAvailable||!InitEnvironmentShaders())return false;device->SetVertexDeclaration(envDecl);if(envDepthPass&&!InitEnvironmentDepthShaders())return false;device->SetVertexShader(envDepthPass?envDepthVS:envVS);device->SetPixelShader(envDepthPass?envDepthPS:envPS);device->SetVertexShaderConstantF(0,&clip._11,4);D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1;device->SetVertexShaderConstantF(5,&identity._11,4);device->SetPixelShaderConstantF(0,envDiffuse,1);device->SetPixelShaderConstantF(1,envAmbient,1);device->SetPixelShaderConstantF(2,envSun,1);device->SetPixelShaderConstantF(3,envFog,1);device->SetPixelShaderConstantF(4,envFogSun,1);device->SetPixelShaderConstantF(5,envFogValue,1);float p[4]{eye.x,eye.y,eye.z,1};device->SetPixelShaderConstantF(6,p,1);if(!envDepthPass){BindEnvironmentRoad();BindEnvironmentShadow();}envDynamicMask=~0u;envSurfaceSet=false;envLocalSet=false;device->SetPixelShaderConstantF(33,envDP[0],16);device->SetPixelShaderConstantF(49,envDD[0],16);device->SetPixelShaderConstantF(65,envDC[0],16);float dynamic[4]{float(envVehicleLights),envShadeHeadlightBoost,0,0};device->SetPixelShaderConstantF(81,dynamic,1);device->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);device->SetSamplerState(1,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);device->SetSamplerState(1,D3DSAMP_MIPMAPLODBIAS,0);device->SetSamplerState(1,D3DSAMP_MAXMIPLEVEL,0);return true;}
static unsigned EnvironmentTexture(unsigned hash){auto it=envFrames.find(hash);if(it==envFrames.end())return hash;auto& a=envAnimations[it->second.first];unsigned index=(unsigned((a.base?envRealTime:envWorldTime)*a.fps)+it->second.second)%a.frames.size();auto next=a.frames[index];if(Texture(next)){++envAnimationDraws;return next;}return hash;}
static void SetEnvironmentSurface(unsigned hash){if(envSurfaceSet&&envSurfaceHash==hash)return;envSurfaceHash=hash;envSurfaceSet=true;auto it=envMaterials.find(hash);EnvironmentMaterial m;if(it!=envMaterials.end())m=it->second;float uv[4]{0,0,1,1};if(m.scroll==3){uv[0]=m.offsetS/-1024.f;uv[1]=m.offsetT/-1024.f;uv[2]=1+m.scaleS/256.f;uv[3]=1+m.scaleT/256.f;}else if(m.scroll){float time=envWorldTime;if(m.scroll==2&&m.step>0)time=float(int(time/(m.step/256.f)));uv[0]=std::fmod(time*m.speedS/4096.f,1.f);uv[1]=std::fmod(time*m.speedT/4096.f,1.f);}device->SetVertexShaderConstantF(4,uv,1);if(envDepthPass)return;float emit[4]{m.rgb[0],m.rgb[1],m.rgb[2],m.brightness*(m.always?1:envNight)};float settings[4]{emit[3]>0?1.f:0.f,float(m.prelit),float(m.vertexColor),MaterialOf(hash).blend==2?1.f:0.f};if(m.mask&&settings[0]>0){auto mask=Texture(m.mask);
 // The mask uses the base material's UVs, including repeat and mirror modes.
 auto tile=MaterialOf(hash).tile;
 device->SetSamplerState(1,D3DSAMP_ADDRESSU,(tile&4)?D3DTADDRESS_MIRROR:((tile&1)?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));
 device->SetSamplerState(1,D3DSAMP_ADDRESSV,(tile&8)?D3DTADDRESS_MIRROR:((tile&2)?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));
 device->SetTexture(1,mask);if(!mask)settings[0]=0;else if(worldLogEnabled)++envEmissiveMasks;}else device->SetTexture(1,nullptr);device->SetPixelShaderConstantF(7,emit,1);device->SetPixelShaderConstantF(8,settings,1);envRoadSettings[0]=!envRoadReflectionPass&&EnvironmentRoadMaterial(hash)?1.f:0.f;device->SetPixelShaderConstantF(86,envRoadSettings,1);float signal[4]{0,0,0,-1},other[4]{};if(hash==0x5FA9D9B5){signal[0]=88.f/512;signal[1]=88.f/512;signal[2]=58.f/512;other[0]=220.f/512;other[1]=88.f/512;other[2]=350.f/512;other[3]=88.f/512;}else if(hash==0x123A6814){signal[0]=86.f/512;signal[1]=82.f/512;signal[2]=72.f/512;other[0]=256.f/512;other[1]=82.f/512;other[2]=427.f/512;other[3]=82.f/512;}else if(hash==0x5FA9D574){signal[0]=177.f/256;signal[1]=132.f/256;signal[2]=14.f/256;other[0]=177.f/256;other[1]=169.f/256;other[2]=177.f/256;other[3]=207.f/256;}if(signal[2]>0)signal[3]=*(int*)0x91601C==-1?1.f:0.f;device->SetPixelShaderConstantF(82,signal,1);device->SetPixelShaderConstantF(83,other,1);
}
static void SetEnvironmentMaterial(unsigned hash,const EnvironmentMeshLights& meshLights,const Mesh* bounds=nullptr){
 SetEnvironmentSurface(hash);if(envDepthPass)return;SetEnvironmentDynamicLights(bounds);
 const auto& nearby=envNight?meshLights.night:meshLights.day;
 if(envLocalSet&&envLocalIndices==nearby)return;
 envLocalSet=true;envLocalIndices=nearby;
 float pos[8][4]{},color[8][4]{},dir[8][4]{};unsigned count=0;
 for(unsigned index:nearby){
  if(index==~0u)continue;
  const auto& light=envLights[index];
  const unsigned slot=count++;
  float intensity=light.intensity*((light.flags&1)?envNight:1);
  float inverseRange=1.f/light.range;
  bool hasCone=(light.flags&2)!=0;
  float inner=light.inner,outer=light.outer;
#ifdef W2C_PUBLIC_BUILD
  if(!envNight&&(light.flags&publicCeilingLight)){
   if(worldLogEnabled)++publicDayCeilingDraws;
   intensity=light.intensity;
   if(!(light.flags&publicCeilingUnboundedRoof))inverseRange=-inverseRange;
   if(!hasCone){inner=.88f;outer=.36f;hasCone=true;}
  }
#endif
  if(light.blink&&light.blink<=3&&reinterpret_cast<int*>(0x91601C)[light.blink-1]!=-1)intensity=0;
  pos[slot][0]=light.position.x;pos[slot][1]=light.position.y;pos[slot][2]=light.position.z;pos[slot][3]=inverseRange;
  color[slot][0]=light.color.x*intensity;color[slot][1]=light.color.y*intensity;color[slot][2]=light.color.z*intensity;
  color[slot][3]=hasCone?inner:-.9999f;
  dir[slot][0]=light.direction.x;dir[slot][1]=light.direction.y;dir[slot][2]=light.direction.z;
  dir[slot][3]=hasCone?outer:-1.f;
 }
 if(count)++envLitDraws;
 float local[4]{float(count),0,0,0};device->SetPixelShaderConstantF(84,local,1);
 device->SetPixelShaderConstantF(9,pos[0],8);device->SetPixelShaderConstantF(17,color[0],8);device->SetPixelShaderConstantF(25,dir[0],8);
}
// Split index ranges only, preserving source vertices and normals. Worker-side
// light selection keeps per-frame work independent of the full source light count.
#include "EnvironmentBatch.h"

static void SubmitEnvironmentFlares(void* view,Vec3 eye,int destinationType){envFlareDraws=0;if(!envAvailable)return;int gx=int(std::floor(eye.x/64)),gy=int(std::floor(eye.y/64));auto submit=reinterpret_cast<void(__cdecl*)(void*,NativeEnvironmentFlare*,void*,float,int,int,float,DWORD,float)>(0x505380);
 for(int x=gx-8;x<=gx+8;++x)for(int y=gy-8;y<=gy+8;++y){auto it=envFlareGrid.find({x,y});if(it==envFlareGrid.end())continue;for(auto n:it->second){auto& source=envFlares[n];float intensity=source.type==12?envNight:1.f;
#ifdef W2C_PUBLIC_BUILD
 const auto ceiling=n<publicCeilingFlares.size()?publicCeilingFlares[n]:PublicCeilingFlare::Stock;
 if(ceiling==PublicCeilingFlare::Always)intensity=1.f;
 if(ceiling==PublicCeilingFlare::DayOnly)intensity=1.f-envNight;
#endif
 if(intensity<=0.f)continue;
 // Direct submission uses MW's shared blink clock; the native pool normally
 // applies this gate before calling eRenderLightFlare.
 if(source.type>=17&&source.type<=19){unsigned slot=source.type==18?0:source.type==17?unsigned(std::abs(int(std::floor(source.position.x)))%3):2;if(reinterpret_cast<int*>(0x91601C)[slot]!=-1)continue;}
 if(zoneIndex!=~0u&&!zones[zoneIndex].sections.count(source.section))continue;auto d=Sub(source.position,eye);if(Dot(d,d)>500.f*500.f)continue;NativeEnvironmentFlare f;f.position=source.position;f.direction=source.direction;f.type=BYTE(source.type);f.tint=source.tint;f.section=(unsigned short)source.section;submit(view,&f,reinterpret_cast<void*>(0x987AB0),intensity,0,destinationType,0,0,1);++envFlareDraws;}}
}

static void* envFlareOriginal=nullptr;
static void __stdcall EnvironmentFlarePass(int destinationType){auto target=*(BYTE**)0x982A20;if(*(int*)0x925E90==6&&*(void**)0x982C80&&target&&*(int*)(target+4)==1)SubmitEnvironmentFlares(reinterpret_cast<void*>(0x919650),cameraPosition,destinationType);}
static void __declspec(naked) EnvironmentFlareHook(){__asm {
 pushad
 push [esp+0x28]
 call EnvironmentFlarePass
 popad
 jmp [envFlareOriginal]
 }}
static void InstallEnvironmentFlareHook(){if(envFlareOriginal||envFlares.empty())return;auto mod=GetModuleHandleA("SunSet.asi");if(!mod)return;auto at=(BYTE*)0x505F71;if(*at!=0xE9)return;auto target=at+5+*(int*)(at+1);if(target<(BYTE*)mod||target>=(BYTE*)mod+0x41000)return;if(MH_CreateHook(at,EnvironmentFlareHook,&envFlareOriginal)==MH_OK&&MH_EnableHook(at)==MH_OK)Log("ENVIRONMENT_NATIVE_FLARE_PASS_ACTIVE");}
