#pragma once
// StuffScenery queues native models before CommitRenderedModels selects the
// native reflection surface. Drawing there used SunSet's previous R32F depth
// target. Queue our resident scenery and render after the native commit.
static void(__cdecl* w2cReflectionCommitOriginal)();
static void* w2cPendingRoadView=nullptr;
static BYTE* w2cPendingRoadTarget=nullptr;
static unsigned w2cPendingRoadFlags=0;
static bool w2cMirrorAuditPending=false;
static DWORD w2cMirrorAuditLast=0;
static void TraceMirrorSurface(const char* phase){
 if(!worldLogEnabled)return;
 auto dev=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);if(!dev)return;
 IDirect3DSurface9* color=nullptr;IDirect3DSurface9* depth=nullptr;D3DSURFACE_DESC desc{},zdesc{};D3DVIEWPORT9 viewport{};
 dev->GetRenderTarget(0,&color);dev->GetDepthStencilSurface(&depth);dev->GetViewport(&viewport);if(color)color->GetDesc(&desc);if(depth)depth->GetDesc(&zdesc);
 DWORD zwrite=0,blend=0,colorwrite=0,stencil=0,scissor=0;dev->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite);dev->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);dev->GetRenderState(D3DRS_COLORWRITEENABLE,&colorwrite);dev->GetRenderState(D3DRS_STENCILENABLE,&stencil);dev->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
 Log("MIRROR_SURFACE phase=%s color=%p size=%ux%u depth=%p zsize=%ux%u viewport=%u,%u,%u,%u zwrite=%u blend=%u mask=%u stencil=%u scissor=%u",phase,color,desc.Width,desc.Height,depth,zdesc.Width,zdesc.Height,viewport.X,viewport.Y,viewport.Width,viewport.Height,zwrite,blend,colorwrite,stencil,scissor);
 auto camera=*reinterpret_cast<D3DMATRIX**>(0x919730);if(camera){D3DMATRIX product{};float difference=0;for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column){for(unsigned k=0;k<4;++k)product.m[row][column]+=camera[0].m[row][k]*camera[1].m[k][column];difference=(std::max)(difference,std::abs(product.m[row][column]-camera[2].m[row][column]));}Log("MIRROR_MATRIX phase=%s difference=%.9g native_z=%.9g,%.9g,%.9g,%.9g computed_z=%.9g,%.9g,%.9g,%.9g",phase,difference,camera[2]._13,camera[2]._23,camera[2]._33,camera[2]._43,product._13,product._23,product._33,product._43);}
 if(depth)depth->Release();if(color)color->Release();
}
static void RenderPendingRoadReflection(void* view,unsigned flags,BYTE* target){if(v2Lost)return;
 auto dev=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);
 static bool entered=false;if(!entered){entered=true;Log("ROAD_REFLECTION_TARGET_ENTER view=%p target=%p device=%p",view,target,dev);}
 if(!dev||!target){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING device=%p target=%p",dev,target);}return;}
 if(*reinterpret_cast<unsigned*>(target+4)!=4){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING after_commit_view=%u",*reinterpret_cast<unsigned*>(target+4));}return;}
 auto surface=*reinterpret_cast<IDirect3DSurface9**>(target+8);
 auto depth=*reinterpret_cast<IDirect3DSurface9**>(target+12);
 if(!surface){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING native_surface_null=1");}return;}D3DSURFACE_DESC desc{};auto status=surface->GetDesc(&desc);if(FAILED(status)){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING surface=%p GetDesc=%08X",surface,unsigned(status));}return;}
 if(desc.Format==D3DFMT_R32F){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING native_surface_is_depth=1 size=%ux%u",desc.Width,desc.Height);}return;}
 if(desc.Width<=1||desc.Height<=1||!(desc.Usage&D3DUSAGE_RENDERTARGET)){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING startup_placeholder=1 size=%ux%u",desc.Width,desc.Height);}return;}
 IDirect3DSurface9* previous=nullptr;IDirect3DSurface9* previousDepth=nullptr;D3DVIEWPORT9 viewport{};
 auto getTarget=dev->GetRenderTarget(0,&previous);if(FAILED(getTarget)){static bool reported=false;if(!reported){reported=true;Log("ROAD_REFLECTION_TARGET_PENDING GetRenderTarget=%08X",unsigned(getTarget));}return;}
 dev->GetDepthStencilSurface(&previousDepth);dev->GetViewport(&viewport);
 dev->SetDepthStencilSurface(nullptr);
 auto setTarget=dev->SetRenderTarget(0,surface);auto setDepth=dev->SetDepthStencilSurface(depth);
 bool ready=SUCCEEDED(setTarget)&&SUCCEEDED(setDepth);
 if(!ready){static bool reported=false;if(!reported){reported=true;D3DSURFACE_DESC depthDesc{};if(depth)depth->GetDesc(&depthDesc);Log("ROAD_REFLECTION_TARGET_PENDING SetRenderTarget=%08X SetDepthStencil=%08X color=%ux%u format=%u msaa=%u depth=%ux%u msaa=%u",unsigned(setTarget),unsigned(setDepth),desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),depthDesc.Width,depthDesc.Height,unsigned(depthDesc.MultiSampleType));}}
 __try{if(ready){D3DVIEWPORT9 local{0,0,desc.Width,desc.Height,0,1};dev->SetViewport(&local);sceneTargetFormat=desc.Format;
  Render(view,flags);
  static DWORD reported=0;DWORD now=GetTickCount();if(now-reported>10000){reported=now;Log("ROAD_REFLECTION_NATIVE_TARGET format=%u size=%ux%u after_native_commit=1",unsigned(desc.Format),desc.Width,desc.Height);}}
 }__finally{dev->SetDepthStencilSurface(nullptr);dev->SetRenderTarget(0,previous);dev->SetDepthStencilSurface(previousDepth);dev->SetViewport(&viewport);
 if(previousDepth)previousDepth->Release();previous->Release();}
}
static void __cdecl W2CReflectionCommit(){
 static unsigned calls=0;if(calls++<4)Log("ROAD_REFLECTION_COMMIT_TRACE pending=%p world_active=%u flow=%u caller=%p",w2cPendingRoadView,W2MW_IsWorldActiveV1(),*reinterpret_cast<unsigned*>(0x925E90),_ReturnAddress());
 auto shadowView=worldPendingShadowView;auto shadowTarget=worldPendingShadowTarget;worldPendingShadowView=nullptr;worldPendingShadowTarget=nullptr;
 auto view=w2cPendingRoadView;unsigned flags=w2cPendingRoadFlags;w2cPendingRoadView=nullptr;
 auto target=w2cPendingRoadTarget;w2cPendingRoadTarget=nullptr;
 w2cReflectionCommitOriginal();
 if(w2cMirrorAuditPending){TraceMirrorSurface("after_native_commit");w2cMirrorAuditPending=false;}
 if(view){static bool returned=false;if(!returned){returned=true;Log("ROAD_REFLECTION_COMMIT_RETURN view=%p target=%p world_active=%u flow=%u",view,target,W2MW_IsWorldActiveV1(),*reinterpret_cast<unsigned*>(0x925E90));}}
 if(shadowView){__try{if(W2MW_IsWorldActiveV1())RenderPendingWorldShadow(shadowView,shadowTarget);}__except(RenderFault(GetExceptionInformation())){}}
 if(!view)return;
 __try{if(W2MW_IsWorldActiveV1())RenderPendingRoadReflection(view,flags,target);}
 __except(RenderFault(GetExceptionInformation())){}
}
static bool EnsureWorldCommitHook(){
 if(!w2cReflectionCommitOriginal){
  static bool tried=false;if(tried)return false;tried=true;
  const BYTE guard[]={0x8B,0x0D,0xDC,0x2C,0x98,0x00,0x85,0xC9};auto site=reinterpret_cast<void*>(0x6E2F50);
  if(memcmp(site,guard,sizeof(guard))||MH_CreateHook(site,W2CReflectionCommit,reinterpret_cast<void**>(&w2cReflectionCommitOriginal))!=MH_OK||MH_EnableHook(site)!=MH_OK){Log("ROAD_REFLECTION_COMMIT_REFUSED ABI_or_hook");return false;}
  Log("ROAD_REFLECTION_COMMIT_ACTIVE resident_world_only=1 native_render_target=1");
 }
 return w2cReflectionCommitOriginal!=nullptr;
}
static bool QueueRoadReflection(void* view,unsigned flags){
 if(!EnsureWorldCommitHook())return false;
 w2cPendingRoadView=view;w2cPendingRoadFlags=flags;w2cPendingRoadTarget=*reinterpret_cast<BYTE**>(0x982A20);return true;
}
