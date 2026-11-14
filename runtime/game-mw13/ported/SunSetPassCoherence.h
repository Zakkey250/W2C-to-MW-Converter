#pragma once
// Chain the game-side SunSet pass-selection entry. Its local LastEffect cache
// must not suppress a fresh BeginPass after native code clears Current.
// Let SunSet choose and bind its own effect/technique; never invent a shader.
static void* nativePassOriginal=nullptr;
static volatile LONG nativePassRepairs=0;
static void __declspec(naked) NativePassCoherence(){__asm{
 pushfd
 push eax
 mov eax,0982C80h
 cmp dword ptr [eax],0
 jne coherentPass
 test ebx,ebx
 jz coherentPass
 xor ebx,ebx
 cmp byte ptr [worldLogEnabled],0
 je coherentPass
 lock inc dword ptr [nativePassRepairs]
 coherentPass:
 pop eax
 popfd
 jmp [nativePassOriginal]
}}
static void InstallSunSetPassCoherence(){
 if(nativePassOriginal)return;
 auto module=reinterpret_cast<BYTE*>(GetModuleHandleA("SunSet.asi"));if(!module)return;
 auto site=reinterpret_cast<BYTE*>(0x6E0254);if(site[0]!=0xE9){Log("NATIVE_SUNSET_PASS_COHERENCE_WAIT opcode=%02X module=%p",site[0],module);return;}
 auto target=site+5+*reinterpret_cast<const int*>(site+1);
 auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>4096)return;
 auto pe=reinterpret_cast<const IMAGE_NT_HEADERS*>(module+dos->e_lfanew);
 if(pe->Signature!=IMAGE_NT_SIGNATURE||target<module||target>=module+pe->OptionalHeader.SizeOfImage){Log("NATIVE_SUNSET_PASS_COHERENCE_REFUSED target=%p module=%p",target,module);return;}
 if(MH_CreateHook(site,NativePassCoherence,&nativePassOriginal)!=MH_OK)return;
 if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);nativePassOriginal=nullptr;return;}
 Log("NATIVE_SUNSET_PASS_COHERENCE_ACTIVE host_site=006E0254 shader_selection=SunSet third_party_binary_changes=0");
}
static void ReportNativePassCoherence(){
 if(!worldLogEnabled)return;static LONG last=0;auto count=nativePassRepairs;
 if(count!=last){last=count;Log("NATIVE_SUNSET_PASS_REBIND count=%ld stale_last_effect_cleared=1",count);}
}
