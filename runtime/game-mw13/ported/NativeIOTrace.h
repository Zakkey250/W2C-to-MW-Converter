#pragma once
// Diagnostic only: trace the game's own file opens without redirecting paths
// or changing handles, errors, content, or installed language.
static decltype(&CreateFileA) worldCreateFileA=nullptr;
static decltype(&CreateFileW) worldCreateFileW=nullptr;
static bool WorldTracePath(const char* path){if(!path)return false;char upper[MAX_PATH]{};unsigned n=0;for(;path[n]&&n<MAX_PATH-1;++n)upper[n]=char(toupper(static_cast<unsigned char>(path[n])));return strstr(upper,"GLOBAL")||strstr(upper,"MOVIES");}
static HANDLE WINAPI WorldOpenFileA(LPCSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE model){auto h=worldCreateFileA(path,access,share,security,creation,flags,model);DWORD error=GetLastError();if(WorldTracePath(path))Log("WORLD_FILE_OPEN_A path=%s handle=%p error=%u",path,h,error);SetLastError(error);return h;}
static HANDLE WINAPI WorldOpenFileW(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE model){auto h=worldCreateFileW(path,access,share,security,creation,flags,model);DWORD error=GetLastError();char narrow[MAX_PATH]{};if(path)WideCharToMultiByte(CP_UTF8,0,path,-1,narrow,sizeof(narrow),nullptr,nullptr);if(WorldTracePath(narrow))Log("WORLD_FILE_OPEN_W path=%s handle=%p error=%u",narrow,h,error);SetLastError(error);return h;}
static void InstallWorldFileTrace(){
 if(!worldLogEnabled||!GetPrivateProfileIntA("Renderer","FileOpenDiagnostics",0,Path("W2CMWv2.ini").c_str()))return;
 auto base=reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;if(!rva)return;
 unsigned hooks=0;for(auto imp=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+rva);imp->Name;++imp){if(!imp->OriginalFirstThunk)continue;auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imp->OriginalFirstThunk);auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imp->FirstThunk);
  for(unsigned n=0;names[n].u1.AddressOfData;++n){if(IMAGE_SNAP_BY_ORDINAL(names[n].u1.Ordinal))continue;auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names[n].u1.AddressOfData)->Name;void* replacement=nullptr;
   if(!strcmp(reinterpret_cast<char*>(name),"CreateFileA")){worldCreateFileA=reinterpret_cast<decltype(worldCreateFileA)>(slots[n].u1.Function);replacement=WorldOpenFileA;}
   else if(!strcmp(reinterpret_cast<char*>(name),"CreateFileW")){worldCreateFileW=reinterpret_cast<decltype(worldCreateFileW)>(slots[n].u1.Function);replacement=WorldOpenFileW;}
   if(replacement){DWORD protection;if(VirtualProtect(&slots[n].u1.Function,sizeof(DWORD),PAGE_READWRITE,&protection)){slots[n].u1.Function=DWORD(replacement);VirtualProtect(&slots[n].u1.Function,sizeof(DWORD),protection,&protection);++hooks;}}
  }
 }Log("WORLD_FILE_TRACE_ACTIVE hooks=%u redirect=0",hooks);
}
