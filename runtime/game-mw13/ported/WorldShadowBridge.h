#pragma once
#include "ShadowCoordinates.h"
static IDirect3DVertexShader9* worldShadowVS=nullptr;
static IDirect3DPixelShader9* worldShadowPS=nullptr;
static void* worldPendingShadowView=nullptr;
static BYTE* worldPendingShadowTarget=nullptr;
static void(__cdecl* worldShadowModelsOriginal)(void*,int);
static bool EnsureWorldCommitHook();
static bool ShadowSectionEligible(unsigned id,const Section& section,const RenderFrustum& frustum){
 return section.vb&&section.ib&&Distance(section.header,cameraPosition)<=400.f*400.f&&(zoneIndex==~0u||id==26000||zones[zoneIndex].sections.count(id))&&frustum.Visible(section.header.minimum,section.header.maximum);
}
static bool DrawShadowOwner(unsigned id,unsigned owner,const RenderFrustum& frustum){
 if(!owner)return true;if(owner==~0u)return false;if(owner>=overlapGroups.size())return true;
 for(auto candidate:overlapGroups[owner]){auto found=sections.find(candidate);if(found!=sections.end()&&ShadowSectionEligible(candidate,found->second,frustum))return candidate==id;}
 return true;
}
static void DrawWorldShadowGeometry(const D3DMATRIX& clip){
 if(!InitEnvironmentShaders()||!InitEnvironmentDepthShaders())return;
 if(!worldShadowVS&&FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(envShaderVSShadow),&worldShadowVS)))return;
 if(!worldShadowPS&&FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPSShadow),&worldShadowPS)))return;
 device->SetVertexShader(worldShadowVS);device->SetPixelShader(worldShadowPS);device->SetVertexShaderConstantF(0,&clip._11,4);
 D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1;device->SetVertexShaderConstantF(5,&identity._11,4);float uv[4]{0,0,1,1};device->SetVertexShaderConstantF(4,uv,1);
 device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,0);device->SetTexture(6,nullptr);
 device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
 RenderFrustum frustum(clip);unsigned count=0,ownerSkipped=0,propsCulled=0;bool wasDepth=envDepthPass;envDepthPass=true;
 device->SetVertexDeclaration(envDecl);
 for(auto& [id,section]:sections){if(!section.page||!ShadowSectionEligible(id,section,frustum))continue;auto& page=*section.page;
  device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_DEPTHBIAS,0);device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);float opaque[4]{0,0,0,0};device->SetPixelShaderConstantF(0,opaque,1);
  for(const auto& span:page.shadowSpans){if(!DrawShadowOwner(id,span.source,frustum)){++ownerSkipped;continue;}auto& block=page.blocks[span.chunk];if(!block.shadow)continue;device->SetStreamSource(0,block.vb,0,40);device->SetIndices(block.shadow);device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,block.vertices,span.local,span.count/3);++count;}
  float alpha[4]{1,0,0,0};device->SetPixelShaderConstantF(0,alpha,1);
  for(unsigned ordinal=0;ordinal<page.draws.size();++ordinal){const auto& m=page.draws[ordinal];if(!DrawShadowOwner(id,m.owner,frustum)){++ownerSkipped;continue;}auto material=MaterialOf(m.texture);if(!material.blend&&material.usage!=1&&m.texture!=0x57FC91B7)continue;if((material.blend&&material.usage!=1)||envNightOverlays.count(m.texture)||m.texture==0xD2E09151||m.texture==0x29D9478C||!frustum.Visible(m.minimum,m.maximum))continue;Header bounds{};bounds.minimum=m.minimum;bounds.maximum=m.maximum;if(Distance(bounds,cameraPosition)>400.f*400.f)continue;auto texture=textures.find(m.texture);if(m.texture&&(texture==textures.end()||!texture->second))continue;SetMaterial(m.texture);device->SetTexture(0,texture==textures.end()?nullptr:texture->second);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);auto [begin,n]=page.drawSpans[ordinal];for(unsigned i=begin;i<begin+n;++i){const auto& span=page.spans[i];auto& block=page.blocks[span.chunk];device->SetStreamSource(0,block.vb,0,40);device->SetIndices(block.ib);device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,block.vertices,span.local,span.count/3);++count;}}
 }
 device->SetVertexDeclaration(envDepthPropDecl);float propAlpha[4]{1,0,0,0};device->SetPixelShaderConstantF(0,propAlpha,1);
 for(auto ptr:activeProps){auto& p=*ptr;if(!p.spawner||!p.initialized||!p.gpu.vb||p.nativeFractured||PursuitPayloadOwner(&p)||(p.nativeRender&&p.nativeTouched))continue;
  auto delta=PropDelta(p.initial,p.last);Vec3 low{},high{};TransformRenderBounds(p.gpu.header.minimum,p.gpu.header.maximum,delta,low,high);if(!frustum.Visible(low,high)){++propsCulled;continue;}device->SetVertexShaderConstantF(5,&delta._11,4);device->SetStreamSource(0,p.gpu.vb,0,sizeof(Vertex));device->SetIndices(p.gpu.ib);
  for(auto& mesh:p.gpu.meshes){if(MaterialOf(mesh.texture).blend&&MaterialOf(mesh.texture).usage!=1)continue;auto texture=textures.find(mesh.texture);if(mesh.texture&&(texture==textures.end()||!texture->second))continue;SetMaterial(mesh.texture);device->SetTexture(0,texture==textures.end()?nullptr:texture->second);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,mesh.firstVertex,0,mesh.vertexCount,mesh.firstIndex,mesh.indexCount/3);++count;}
 }
 envDepthPass=wasDepth;static DWORD last=0;DWORD now=GetTickCount();if(worldLogEnabled&&now-last>10000){last=now;Log("WORLD_SHADOW_CAST meshes=%u width=%u native_depth=1 resident_only=1 owner_skipped=%u props_culled=%u",count,envShadowWidth,ownerSkipped,propsCulled);}
}
static void RenderPendingWorldShadow(void* view,BYTE* target){if(v2Lost)return;
 double shadowStart=renderWorkProfiling?Clock():0;
 if(!view||!target||*reinterpret_cast<unsigned*>(target+4)!=15)return;
 device=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);auto camera=*reinterpret_cast<BYTE**>(view);auto surface=*reinterpret_cast<IDirect3DSurface9**>(target+8);auto depth=*reinterpret_cast<IDirect3DSurface9**>(target+12);if(!device||!camera||!surface||!depth)return;
 D3DSURFACE_DESC desc{};if(FAILED(depth->GetDesc(&desc))||desc.Width<=1)return;
 if(worldLogEnabled){static DWORD reported=0;DWORD now=GetTickCount();if(now-reported>10000){reported=now;DWORD nativeBias=0,nativeSlope=0;device->GetRenderState(D3DRS_DEPTHBIAS,&nativeBias);device->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,&nativeSlope);float bias=0,slope=0;memcpy(&bias,&nativeBias,4);memcpy(&slope,&nativeSlope,4);Log("WORLD_SHADOW_NATIVE_BIAS constant=%.9g slope=%.9g",bias,slope);}}
 // MW's shadow Pinfo projection already includes the texture-space bias.
 // Runtime native-effect comparison proved a second bias moved .493,.781
 // to .747,.110. Receivers use this matrix directly; rasterisation needs
 // the inverse UV-to-clip transform, including the D3D Y inversion.
 D3DMATRIX textureClip{};memcpy(&textureClip,camera+0x80,64);envShadowClip=textureClip;const D3DMATRIX clip=ShadowTextureToRaster(textureClip);

 IDirect3DStateBlock9* state=nullptr;IDirect3DSurface9* previous=nullptr;IDirect3DSurface9* previousDepth=nullptr;D3DVIEWPORT9 viewport{};
 if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return;if(FAILED(device->GetRenderTarget(0,&previous))){state->Release();return;}device->GetDepthStencilSurface(&previousDepth);device->GetViewport(&viewport);bool wasDepth=envDepthPass;
 __try{for(unsigned sampler=0;sampler<16;++sampler)device->SetTexture(sampler,nullptr);device->SetDepthStencilSurface(nullptr);if(SUCCEEDED(device->SetRenderTarget(0,surface))&&SUCCEEDED(device->SetDepthStencilSurface(depth))){D3DVIEWPORT9 local{0,0,desc.Width,desc.Height,0,1};device->SetViewport(&local);envShadowWidth=desc.Width;DrawWorldShadowGeometry(clip);envShadowReady=true;static DWORD last=0;DWORD now=GetTickCount();if(worldLogEnabled&&now-last>10000){last=now;Log("WORLD_SHADOW_NATIVE_VIEWPORT x=%u y=%u size=%ux%u min=%.3f max=%.3f target=%ux%u",viewport.X,viewport.Y,viewport.Width,viewport.Height,viewport.MinZ,viewport.MaxZ,desc.Width,desc.Height);}}}
 __finally{envDepthPass=wasDepth;device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,previous);device->SetDepthStencilSurface(previousDepth);device->SetViewport(&viewport);state->Apply();state->Release();previous->Release();if(previousDepth)previousDepth->Release();if(shadowStart)renderFrameShadowMs+=Clock()-shadowStart;}
}
static void __cdecl WorldShadowModels(void* view,int flags){
 worldShadowModelsOriginal(view,flags);
 if(W2MW_IsWorldActiveV1()&&view&&*reinterpret_cast<unsigned*>(static_cast<BYTE*>(view)+4)==15){worldPendingShadowView=view;worldPendingShadowTarget=*reinterpret_cast<BYTE**>(0x982A20);}
}
static void InstallWorldShadowHook(){
 const BYTE guard[]{0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x74,0x53,0x56,0x57};auto site=reinterpret_cast<void*>(0x7538D0);
 if(memcmp(site,guard,sizeof(guard))||!EnsureWorldCommitHook()||MH_CreateHook(site,WorldShadowModels,reinterpret_cast<void**>(&worldShadowModelsOriginal))!=MH_OK||MH_EnableHook(site)!=MH_OK){Log("WORLD_SHADOW_REFUSED ABI_or_hook");return;}
 Log("WORLD_SHADOW_ACTIVE native_view=15 native_depth_shared=1 resident_only=1");
}
