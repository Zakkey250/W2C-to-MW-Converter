#pragma once
// 006C68B0 already supports fixed-function texture submission when Current
// is null, but its SunSet-era shader tail still dereferences that null pointer.
// Preserve the native blend/texture path and omit only shader-parameter writes.
static void* materialBlendOriginal=nullptr;
static void* materialTailOriginal=nullptr;
static unsigned materialBlendResume=0x6C6A35,materialTailEnd=0x6C6C97;
static unsigned materialTailResume=0x6C6A61;
static void __stdcall TraceFixedFunctionMaterial(){if(!worldLogEnabled)return;static DWORD last=0;DWORD now=GetTickCount();if(now-last>5000){last=now;Log("NATIVE_MATERIAL_FIXED_FUNCTION current_effect=0 shader_tail_skipped=1 geometry_preserved=1");}}
static void __declspec(naked) MaterialBlendGuard(){__asm{
 // Use explicit indirection: the former absolute-expression form emitted
 // MOV reg, immediate-address in the release build, so it never saw null.
 mov eax,0982C80h
 mov eax,[eax]
 test eax,eax
 jnz normalBlend
 // A missing shader is not the native car shader (id=EDI, normally 4).
 // Set NZ as the original CMP would for a non-car effect.
 cmp eax,1
 jmp [materialBlendResume]
 normalBlend:
 cmp dword ptr [eax+4],edi
 jmp [materialBlendResume]
}}
static void __declspec(naked) MaterialTailGuard(){__asm{
 mov ecx,0982C80h
 mov ecx,[ecx]
 test ecx,ecx
 jnz normalTail
 pushfd
 pushad
 call TraceFixedFunctionMaterial
 popad
 popfd
 // Native epilogue balances EDI/ESI/EBX and EBP; the draw still follows.
 jmp [materialTailEnd]
 normalTail:
 // Continue using the checked snapshot, without reloading a mutable global.
 jmp [materialTailResume]
}}
static void InstallNativeMaterialGuard(){
 const BYTE blend[]={0xA1,0x80,0x2C,0x98,0x00,0x39,0x78,0x04};
 const BYTE tail[]={0x8B,0x0D,0x80,0x2C,0x98,0x00,0x8B,0x41,0x44};
 if(memcmp(reinterpret_cast<void*>(0x6C6A2D),blend,sizeof(blend))||memcmp(reinterpret_cast<void*>(0x6C6A5B),tail,sizeof(tail))){Log("NATIVE_MATERIAL_GUARD_REFUSED byte guard");return;}
 if(MH_CreateHook(reinterpret_cast<void*>(0x6C6A2D),MaterialBlendGuard,&materialBlendOriginal)!=MH_OK)return;
 if(MH_CreateHook(reinterpret_cast<void*>(0x6C6A5B),MaterialTailGuard,&materialTailOriginal)!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(0x6C6A2D));return;}
 if(MH_EnableHook(reinterpret_cast<void*>(0x6C6A2D))!=MH_OK||MH_EnableHook(reinterpret_cast<void*>(0x6C6A5B))!=MH_OK){MH_DisableHook(reinterpret_cast<void*>(0x6C6A2D));MH_DisableHook(reinterpret_cast<void*>(0x6C6A5B));Log("NATIVE_MATERIAL_GUARD_REFUSED activation");return;}
 Log("NATIVE_MATERIAL_GUARD_ACTIVE fixed_function_preserved=1 arrest_scenes_native=1");
}
