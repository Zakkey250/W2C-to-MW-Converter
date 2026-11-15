#pragma once
#include "EnvironmentRoadMaterials.generated.h"
static float envRain[4]{},envSpecular[4]{},envRoadSettings[4]{};
static float envRoadDetailSettings[4]{},envRoadDiffuse[4]{},envRoadAmbient[4]{};
static bool envRoadReflectionPass=false;
static bool envRoadReflectionReady=false;
static ID3DXEffect* EnvironmentEffect(unsigned id){
 auto effect=reinterpret_cast<BYTE**>(0x93DE78)[id];
 return effect&&*reinterpret_cast<unsigned*>(effect+4)==id?*reinterpret_cast<ID3DXEffect**>(effect+0x48):nullptr;
}
static bool EnvironmentVector(ID3DXEffect* effect,const char* name,float* value){
 if(!effect)return false;auto handle=effect->GetParameterByName(nullptr,name);
 if(!handle||FAILED(effect->GetVector(handle,reinterpret_cast<D3DXVECTOR4*>(value))))return false;
 for(unsigned i=0;i<4;++i)if(!std::isfinite(value[i]))return false;return true;
}
static void AuditEnvironmentShadow(){
 static DWORD reported=0;auto now=GetTickCount();if(!worldLogEnabled||now-reported<10000)return;reported=now;
 for(unsigned id: {0u,1u}){
  auto effect=EnvironmentEffect(id);if(!effect)continue;
  float enabled=0;auto flag=effect->GetParameterByName(nullptr,"cfShadowsEnabled");if(flag)effect->GetFloat(flag,&enabled);
  D3DXMATRIX matrix{};auto transform=effect->GetParameterByName(nullptr,"matShadowMapWVP");bool matrixOK=transform&&SUCCEEDED(effect->GetMatrix(transform,&matrix));
  bool finite=matrixOK;for(unsigned i=0;i<16;++i)finite&=std::isfinite(reinterpret_cast<float*>(&matrix)[i]);
  D3DXVECTOR4 scale{};auto scaleHandle=effect->GetParameterByName(nullptr,"ShadowMapScale");if(scaleHandle)effect->GetVector(scaleHandle,&scale);
  IDirect3DBaseTexture9* texture=nullptr;auto sampler=effect->GetParameterByName(nullptr,"ShadowMapTex");if(sampler)effect->GetTexture(sampler,&texture);
  D3DSURFACE_DESC desc{};if(texture&&texture->GetType()==D3DRTYPE_TEXTURE)reinterpret_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc);
  Log("ENVIRONMENT_SHADOW_AUDIT effect=%u enabled=%u draw=%u detail=%u texture=%p format=%u size=%ux%u usage=%08X matrix_finite=%u scale=%.8f,%.8f matrix=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f",id,unsigned(enabled),unsigned(*reinterpret_cast<bool*>(0x8FAED0)),*reinterpret_cast<unsigned*>(0x901830),texture,unsigned(desc.Format),desc.Width,desc.Height,unsigned(desc.Usage),unsigned(finite),scale.x,scale.y,matrix._11,matrix._12,matrix._13,matrix._14,matrix._21,matrix._22,matrix._23,matrix._24,matrix._31,matrix._32,matrix._33,matrix._34,matrix._41,matrix._42,matrix._43,matrix._44);
  if(texture)texture->Release();
 }
}
static bool EnvironmentReflectionTextureValid(IDirect3DBaseTexture9* texture){
 if(!texture||texture->GetType()!=D3DRTYPE_TEXTURE)return false;
 auto color=reinterpret_cast<IDirect3DTexture9*>(texture);D3DSURFACE_DESC desc{};
 if(FAILED(color->GetLevelDesc(0,&desc))||desc.Width<=1||desc.Height<=1||!(desc.Usage&D3DUSAGE_RENDERTARGET)||desc.Format==D3DFMT_R32F)return false;
 IDirect3DSurface9* surface=nullptr;IDirect3DSurface9* target=nullptr;
 bool readable=SUCCEEDED(color->GetSurfaceLevel(0,&surface))&&SUCCEEDED(device->GetRenderTarget(0,&target));
 bool feedback=surface&&surface==target;if(surface)surface->Release();if(target)target->Release();
 return readable&&!feedback;
}
static bool EnvironmentRoadTexture(ID3DXEffect* effect,const char* name,unsigned sampler,bool reflection){
 IDirect3DBaseTexture9* texture=nullptr;auto handle=effect?effect->GetParameterByName(nullptr,name):nullptr;
 if(handle)effect->GetTexture(handle,&texture);
 if(texture&&reflection&&!EnvironmentReflectionTextureValid(texture)){texture->Release();texture=nullptr;}
 if(!texture&&reflection){
  // SunSet Game::ReflectionTexture. W2C scenery bypasses the native road
  // material binder, so the effect parameter can remain unset while the
  // game's reflection target is valid.
  texture=*reinterpret_cast<IDirect3DTexture9**>(0x982A6C);if(texture)texture->AddRef();
  if(texture&&!EnvironmentReflectionTextureValid(texture)){texture->Release();texture=nullptr;}
 }
 if(texture&&reflection){
  D3DSURFACE_DESC desc{};
  if(texture)reinterpret_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc);
  static bool reported=false;if(texture&&!reported){reported=true;Log("ROAD_REFLECTION_TEXTURE format=%u size=%ux%u native_global=%p effect_or_native=%p",unsigned(desc.Format),desc.Width,desc.Height,*reinterpret_cast<void**>(0x982A6C),texture);}
 }
 device->SetTexture(sampler,texture);bool available=texture!=nullptr;if(texture)texture->Release();
 device->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
 device->SetSamplerState(sampler,D3DSAMP_ADDRESSU,reflection?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP);
 device->SetSamplerState(sampler,D3DSAMP_ADDRESSV,reflection?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP);
 return available;
}
static void BindEnvironmentRoad(){
 AuditEnvironmentShadow();
 std::fill(std::begin(envRain),std::end(envRain),0.f);std::fill(std::begin(envSpecular),std::end(envSpecular),0.f);
 std::fill(std::begin(envRoadSettings),std::end(envRoadSettings),0.f);
 std::fill(std::begin(envRoadDetailSettings),std::end(envRoadDetailSettings),0.f);
 memcpy(envRoadDiffuse,envDiffuse,sizeof(envRoadDiffuse));memcpy(envRoadAmbient,envAmbient,sizeof(envRoadAmbient));
 device->SetPixelShaderConstantF(88,envRoadDetailSettings,1);device->SetPixelShaderConstantF(89,envRoadDiffuse,1);device->SetPixelShaderConstantF(90,envRoadAmbient,1);
 device->SetPixelShaderConstantF(85,envRain,1);device->SetPixelShaderConstantF(87,envSpecular,1);
 for(unsigned sampler=2;sampler<=5;++sampler)device->SetTexture(sampler,nullptr);
 auto effect=EnvironmentEffect(1);
 if(!effect||!EnvironmentVector(effect,"cvRainParams",envRain))return;
 if(!EnvironmentVector(effect,"cvSpecularColor",envSpecular))std::fill(std::begin(envSpecular),std::end(envSpecular),0.f);
 // WorldReflectShader uses its own live weather uniforms and the global
 // SunSet road-detail texture. Flat geometric normals lose that daylight
 // surface detail even when the diffuse road texture is otherwise correct.
 const bool roadDiffuseRead=EnvironmentVector(effect,"cvDiffuseColor",envRoadDiffuse);
 const bool roadAmbientRead=EnvironmentVector(effect,"cvAmbientColor",envRoadAmbient);
 if(!roadDiffuseRead||!roadAmbientRead){
  memcpy(envRoadDiffuse,envDiffuse,sizeof envRoadDiffuse);
  memcpy(envRoadAmbient,envAmbient,sizeof envRoadAmbient);
 }
#ifdef W2C_PUBLIC_BUILD
 else w2c_public_light::BalanceDaylight(envRoadDiffuse,envRoadAmbient,1.40f,PublicShadeWeight());
#endif
 envRoadDetailSettings[0]=EnvironmentRoadTexture(effect,"MISCMAP3_TEXTURE",5,false)?1.f:0.f;
 device->SetSamplerState(5,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);device->SetSamplerState(5,D3DSAMP_MIPMAPLODBIAS,0);device->SetSamplerState(5,D3DSAMP_MAXMIPLEVEL,0);
 device->SetPixelShaderConstantF(88,envRoadDetailSettings,1);device->SetPixelShaderConstantF(89,envRoadDiffuse,1);device->SetPixelShaderConstantF(90,envRoadAmbient,1);
 static int reflectionDiagnosis=-1;
 if(reflectionDiagnosis<0)reflectionDiagnosis=GetPrivateProfileIntA("RendererDiagnosis","DisableRoadReflection",0,Path("W2CMWv2.ini").c_str())&&GetFileAttributesA(Path("MWEventLoader.local-test").c_str())!=INVALID_FILE_ATTRIBUTES;
 // MW allocates a reflection texture even when road reflections are off.
 // Sampling that unwritten texture produced white pavement after bloom.
 envRoadSettings[1]=!envRoadReflectionPass&&envRoadReflectionReady&&*reinterpret_cast<unsigned*>(0x9017D4)>1&&!reflectionDiagnosis&&EnvironmentRoadTexture(effect,"ReflectedTex",2,true)?1.f:0.f;
 envRoadSettings[2]=EnvironmentRoadTexture(effect,"MISCMAP1_TEXTURE",3,false)?1.f:0.f;
 envRoadSettings[3]=EnvironmentRoadTexture(effect,"MISCMAP2_TEXTURE",4,false)?1.f:0.f;
 device->SetPixelShaderConstantF(85,envRain,1);device->SetPixelShaderConstantF(87,envSpecular,1);
 static DWORD reported=0;DWORD now=worldLogEnabled?GetTickCount():0;if(now-reported>10000&&worldLogEnabled){reported=now;
  auto nativeRain=*reinterpret_cast<BYTE**>(0x9196B8);
  Log("ENVIRONMENT_ROAD_DETAIL native_texture=%u world_diffuse=%.4f,%.4f,%.4f road_diffuse=%.4f,%.4f,%.4f world_ambient=%.4f,%.4f,%.4f road_ambient=%.4f,%.4f,%.4f source=SunSet_WorldReflectShader",
   unsigned(envRoadDetailSettings[0]),envDiffuse[0],envDiffuse[1],envDiffuse[2],envRoadDiffuse[0],envRoadDiffuse[1],envRoadDiffuse[2],envAmbient[0],envAmbient[1],envAmbient[2],envRoadAmbient[0],envRoadAmbient[1],envRoadAmbient[2]);
  Log("ENVIRONMENT_ROAD_BRIDGE wet=%.3f rain=%.3f reflection=%u mask=%u drops=%u diffuse=%.3f,%.3f,%.3f ambient=%.3f,%.3f,%.3f specular=%.3f,%.3f,%.3f power=%.3f ambient_specular=%.3f native_spotlight_diffuse_max=4.5 puddle_intensity=%.3f native_reflection_wetness=%.3f reflection_quality=%u native_rain_particles=%u",
   envRain[1],envRain[0],unsigned(envRoadSettings[1]),unsigned(envRoadSettings[2]),unsigned(envRoadSettings[3]),
   envDiffuse[0],envDiffuse[1],envDiffuse[2],envAmbient[0],envAmbient[1],envAmbient[2],envSpecular[0],envSpecular[1],envSpecular[2],envSpecular[3],envAmbient[3],envRain[3],nativeRain?*reinterpret_cast<float*>(nativeRain+0x36A0):0,*reinterpret_cast<unsigned*>(0x9017D4),nativeRain?*reinterpret_cast<unsigned*>(nativeRain):0);}
}
