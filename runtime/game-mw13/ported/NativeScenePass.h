#pragma once
#include <d3dx9effect.h>
#include "RenderTransformState.h"
// Native NIS immediate geometry (0044B190 -> 006D1A40) is not a queued
// RenderModel. SunSet's queued-model BeginPass shim therefore never binds a
// shader for it after CommitRenderedModels has cleared eEffect::Current.
// Own a real native prelit pass only for that otherwise unowned draw scope.
// No EventLoader ABI, scene ID, race state, or fabricated effect pointer.
static void (__cdecl* nativeSceneDrawOriginal)(void*,unsigned)=nullptr;
static void InvalidateNativeSceneCaches(){
 *reinterpret_cast<void**>(0x988550)=nullptr; // last transform effect
 *reinterpret_cast<void**>(0x988554)=nullptr; // last world matrix
 *reinterpret_cast<void**>(0x982CC8)=nullptr; // last texture effect
}
static void __cdecl NativeSceneDraw(void* view,unsigned flags){
 auto& current=*reinterpret_cast<BYTE**>(0x982C80);
 if(current){nativeSceneDrawOriginal(view,flags);return;}
 // Do not allocate a state block for the overwhelmingly common empty NIS list.
 auto manager=*reinterpret_cast<void**>(0x9885C8);
 if(!manager)return;
 auto scene=reinterpret_cast<void*(__thiscall*)(void*)>((*reinterpret_cast<void***>(manager))[19])(manager);
 if(!scene||*reinterpret_cast<void**>(static_cast<BYTE*>(scene)+0x18)==static_cast<BYTE*>(scene)+0x14)return;
 auto effect=reinterpret_cast<BYTE**>(0x93DE78)[17]; // WorldPrelitShader: position/UV/colour
 auto dev=*reinterpret_cast<IDirect3DDevice9**>(0x982BDC);
 if(!effect||!dev||*reinterpret_cast<unsigned*>(effect+4)!=17){Log("NATIVE_SCENE_PASS_REFUSED prelit_unavailable");return;}
 auto fx=*reinterpret_cast<ID3DXEffect**>(effect+0x48);
 auto decl=*reinterpret_cast<IDirect3DVertexDeclaration9**>(effect+0x4C);
 auto technique=*reinterpret_cast<D3DXHANDLE*>(effect+0x10);
 if(!fx||!decl||!technique){Log("NATIVE_SCENE_PASS_REFUSED incomplete_prelit");return;}
 IDirect3DStateBlock9* state=nullptr;
 if(FAILED(dev->CreateStateBlock(D3DSBT_ALL,&state)))return;
 RenderTransformState transforms;transforms.Capture(dev);
 bool started=false,begun=false,pass=false;
 __try{
  if(SUCCEEDED(fx->SetTechnique(technique))){
   current=effect;
   reinterpret_cast<void(__thiscall*)(void*)>((*reinterpret_cast<void***>(effect))[1])(effect);started=true;
   dev->SetVertexDeclaration(decl);
   UINT count=0;begun=SUCCEEDED(fx->Begin(&count,0));
   if(begun&&count)pass=SUCCEEDED(fx->BeginPass(0));
   if(pass){
    InvalidateNativeSceneCaches();
    if(worldLogEnabled){static DWORD last=0;auto now=GetTickCount();if(now-last>=1000){last=now;Log("NATIVE_SCENE_PASS_ACTIVE native_nis=1 prelit=17 view=%p eventloader_required=0",view);}}
    nativeSceneDrawOriginal(view,flags);
   }
  }
 }__finally{
  if(pass)fx->EndPass();
  if(started)reinterpret_cast<void(__thiscall*)(void*)>((*reinterpret_cast<void***>(effect))[2])(effect);
  if(begun)fx->End();
  current=nullptr;
  InvalidateNativeSceneCaches();
  state->Apply();transforms.Restore(dev);state->Release();
 }
}
static void InstallNativeScenePass(){
 const BYTE guard[]{0x8B,0x0D,0xC8,0x85,0x98,0x00,0x85,0xC9};
 auto site=reinterpret_cast<void*>(0x44B190);
 if(memcmp(site,guard,sizeof(guard))){Log("NATIVE_SCENE_PASS_REFUSED byte_guard");return;}
 if(MH_CreateHook(site,NativeSceneDraw,reinterpret_cast<void**>(&nativeSceneDrawOriginal))!=MH_OK)return;
 if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);nativeSceneDrawOriginal=nullptr;return;}
 Log("NATIVE_SCENE_PASS_INSTALLED eventloader_required=0 native_effect_lifetime=1");
}
