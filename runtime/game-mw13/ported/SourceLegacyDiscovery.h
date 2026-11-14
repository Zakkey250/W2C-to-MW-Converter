#pragma once
// Final integration with the unchanged, fingerprinted companion binaries.
// Only their legacy GetModuleHandle lookup resolves to this new provider.
// Other callers retain the operating system result. No old ASI is replaced.
struct SourceCompanion {const wchar_t* module;const char* file;const char* sha;unsigned bytes;BYTE* base=nullptr;unsigned imageBytes=0;bool rejected=false;};
static SourceCompanion sourceCompanions[]={
#ifdef W2C_PUBLIC_BUILD
 {L"MWEventLoader.asi","scripts\\MWEventLoader.asi","3d5d2aed87c8b3f5ee4ba2d4245224caba6c54df3f80376f69b32bdc42a4a9ed",8445440},
#else
 {L"MWEventLoader.asi","scripts\\MWEventLoader.asi","b95ef74cd3f5d9f46f5f8cbf791d739035981147fd4e46c1384125709a59f959",8381440},
 {L"NFSMWNativeFreeRoamRacers.asi","scripts\\NFSMWNativeFreeRoamRacers.asi","b6962120b82c930f7d2952a2cbba0a4139ae08c4bf64bad926e921db77c1f12a",557568}
#endif
};
static SRWLOCK sourceCompanionLock=SRWLOCK_INIT;
static HMODULE sourceDiscoverySelf=nullptr;
static HMODULE (WINAPI* sourceOriginalModuleW)(LPCWSTR)=nullptr;
static HMODULE (WINAPI* sourceOriginalModuleA)(LPCSTR)=nullptr;
static bool SourceCompanionCaller(const void* address){
 if(!sourceOriginalModuleW)return false;
 AcquireSRWLockExclusive(&sourceCompanionLock);bool allowed=false;
 for(auto& c:sourceCompanions){
  if(!c.base&&!c.rejected){auto module=sourceOriginalModuleW(c.module);if(module){try{
   w2v2disk::File file(std::filesystem::path(root)/c.file);w2v2disk::need(file.size==c.bytes&&file.fullHash()==c.sha,"unsupported unchanged companion identity");
   auto base=reinterpret_cast<BYTE*>(module);auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);w2v2disk::need(dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>0&&dos->e_lfanew<4096,"companion PE header");auto pe=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);w2v2disk::need(pe->Signature==IMAGE_NT_SIGNATURE&&pe->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&pe->OptionalHeader.SizeOfImage>c.bytes&&pe->OptionalHeader.SizeOfImage<16*1048576,"companion PE32 image");c.base=base;c.imageBytes=pe->OptionalHeader.SizeOfImage;Log("SOURCE_COMPANION_DISCOVERY_ACTIVE module=%ls unchanged_binary=1",c.module);
  }catch(const std::exception& e){c.rejected=true;Log("SOURCE_COMPANION_DISCOVERY_REFUSED module=%ls reason=%s",c.module,e.what());}}}
  auto caller=reinterpret_cast<uintptr_t>(address),base=reinterpret_cast<uintptr_t>(c.base);if(base&&caller>=base&&caller-base<c.imageBytes){allowed=true;break;}
 }
 ReleaseSRWLockExclusive(&sourceCompanionLock);return allowed;
}
static __declspec(noinline) HMODULE WINAPI SourceModuleW(LPCWSTR name){if(name&&(!_wcsicmp(name,L"World2MWNativeLoader.asi")
#ifdef W2C_PUBLIC_BUILD
 ||!_wcsicmp(name,L"W2CMWv2.asi")
#endif
 )&&SourceCompanionCaller(_ReturnAddress()))return sourceDiscoverySelf;return sourceOriginalModuleW(name);}
static __declspec(noinline) HMODULE WINAPI SourceModuleA(LPCSTR name){if(name&&(!_stricmp(name,"World2MWNativeLoader.asi")
#ifdef W2C_PUBLIC_BUILD
 ||!_stricmp(name,"W2CMWv2.asi")
#endif
 )&&SourceCompanionCaller(_ReturnAddress()))return sourceDiscoverySelf;return sourceOriginalModuleA(name);}
static bool InstallSourceLegacyDiscovery(){
 if(!sourceRuntime||GetPrivateProfileIntA("Compatibility","LegacyModuleDiscovery",1,Path("W2CMWv2.ini").c_str())==0)return true;
 if(GetModuleHandleW(L"World2MWNativeLoader.asi")||GetFileAttributesA(Path("World2MWNativeLoader.asi").c_str())!=INVALID_FILE_ATTRIBUTES){Log("SOURCE_COMPANION_DISCOVERY_REFUSED competing_legacy_provider=1");return false;}
#ifdef W2C_PUBLIC_BUILD
 // Resolve a receiving installation's exact companion only when it exists.
 // Do not install process-wide API detours in a plain MW installation.
 if(GetFileAttributesA(Path("scripts\\MWEventLoader.asi").c_str())==INVALID_FILE_ATTRIBUTES)return true;
 if(GetModuleHandleW(L"W2CMWv2.asi"))return false;
 sourceDiscoverySelf=GetModuleHandleW(L"W2CMWPublic.asi");
#else
 sourceDiscoverySelf=GetModuleHandleW(L"W2CMWv2.asi");
#endif
 auto kernel=GetModuleHandleW(L"kernel32.dll");auto wide=reinterpret_cast<void*>(GetProcAddress(kernel,"GetModuleHandleW")),ansi=reinterpret_cast<void*>(GetProcAddress(kernel,"GetModuleHandleA"));
 if(!sourceDiscoverySelf||!wide||!ansi)return false;
 if(MH_CreateHook(wide,SourceModuleW,reinterpret_cast<void**>(&sourceOriginalModuleW))!=MH_OK)return false;
 if(MH_CreateHook(ansi,SourceModuleA,reinterpret_cast<void**>(&sourceOriginalModuleA))!=MH_OK){MH_RemoveHook(wide);return false;}
 if(MH_EnableHook(wide)!=MH_OK||MH_EnableHook(ansi)!=MH_OK){MH_DisableHook(wide);MH_DisableHook(ansi);MH_RemoveHook(wide);MH_RemoveHook(ansi);return false;}
 return true;
}
