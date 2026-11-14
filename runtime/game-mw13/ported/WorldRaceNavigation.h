#pragma once
// CGcn stores a DWORD cell number. MW's loader narrowed it to WORD, which
// wraps the expanded 288*256 grid. Keep the native allocator, records and
// consumers; widen only the load that indexes the native pointer table.
static bool InstallRaceGridCompatibility(){
 auto site=reinterpret_cast<BYTE*>(0x76D010);
 const BYTE guard[]={0x0F,0xB7,0x50,0x04,0x8B,0x3D,0xA8,0x38,0x9B,0x00,0x8B,0x7F,0x20,0x89,0x04,0x97};
 if(memcmp(site,guard,sizeof(guard))){Log("RACE_GRID_REFUSED load guard");return false;}
 const BYTE widened[]={0x8B,0x50,0x04,0x90}; // mov edx,[eax+4]; nop
 DWORD old=0;if(!VirtualProtect(site,sizeof(widened),PAGE_EXECUTE_READWRITE,&old)){Log("RACE_GRID_REFUSED protection");return false;}
 memcpy(site,widened,sizeof(widened));DWORD ignored=0;VirtualProtect(site,sizeof(widened),old,&ignored);
 FlushInstructionCache(GetCurrentProcess(),site,sizeof(widened));
 Log("RACE_GRID_CELL_INDEX_ACTIVE address=0076D010 source_bits=32 native_allocation=1");return true;
}
static void TraceRaceGridImpl(){
 static void* lastTable=nullptr;static DWORD last=0;DWORD now=GetTickCount();if(now-last<3000)return;last=now;
 auto grid=*reinterpret_cast<BYTE**>(0x9B38A8);if(!grid)return;
 unsigned rows=*reinterpret_cast<unsigned*>(grid+24),columns=*reinterpret_cast<unsigned*>(grid+28);
 if(!rows||!columns||rows>1024||columns>1024)return;
 auto table=*reinterpret_cast<BYTE***>(grid+32);if(!table||table==lastTable)return;
 unsigned present=0,high=0,mismatch=0,maxId=0;
 for(unsigned i=0;i<rows*columns;++i){auto row=table[i];if(!row)continue;unsigned id=*reinterpret_cast<unsigned*>(row+4);++present;if(id>=65536)++high;if(id!=i)++mismatch;maxId=std::max(maxId,id);}
 lastTable=table;Log("RACE_GRID_LOADED rows=%u columns=%u records=%u high_cells=%u slot_mismatch=%u max_cell=%u",rows,columns,present,high,mismatch,maxId);
}
static void TraceRaceProgressImpl(){
 static DWORD last=0;DWORD now=GetTickCount();if(now-last<3000)return;last=now;
 auto race=*reinterpret_cast<BYTE**>(0x91E000);if(!race||*reinterpret_cast<unsigned*>(race+0x1960)!=1)return;
 unsigned count=*reinterpret_cast<unsigned*>(race+0x1958);if(count>16)return;
 for(unsigned i=0;i<count;++i){auto r=race+0x18+i*0x194;
  Log("RACE_PROGRESS racer=%u checkpoints=%d laps=%d finished=%u distance=%.1f next_distance=%.1f",i,*reinterpret_cast<int*>(r+0x2C),*reinterpret_cast<int*>(r+0x28),r[0x21],*reinterpret_cast<float*>(r+0xFC),*reinterpret_cast<float*>(r+0xF8));
 }
}
static void TraceRaceGrid(){__try{TraceRaceGridImpl();TraceRaceProgressImpl();}__except(EXCEPTION_EXECUTE_HANDLER){Log("RACE_GRID_AUDIT_FAULT code=%08X",GetExceptionCode());}}
// Observe native route generation only; do not recalculate a started race or
// clear its error flag. A new career race must succeed in its ordinary setup.
#include "RouteSectionCode.h"
// MW gives each shortcut a byte group ID (255 is the sentinel), but its
// section solver reserves only 28 filter bytes on the stack. Imported editor
// courses can exceed that capacity. Retain the native algorithm and SEH frame;
// relocate both filter accesses to a complete byte-ID table.
static BYTE raceShortcutFilter[256];
static void* MakeExpandedRouteSection(){
 if(memcmp(reinterpret_cast<void*>(0x5FE840),RouteSectionOriginal,sizeof(RouteSectionOriginal))){Log("ROUTE_FILTER_REFUSED section_guard");return nullptr;}
 auto code=static_cast<BYTE*>(VirtualAlloc(nullptr,sizeof(RouteSectionCode),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!code)return nullptr;
 memcpy(code,RouteSectionCode,sizeof(RouteSectionCode));
 for(auto& r:RouteSectionCalls)*reinterpret_cast<unsigned*>(code+r.offset)=r.target-unsigned(code+r.offset+4);
 for(auto off:RouteSectionBuffers)*reinterpret_cast<unsigned*>(code+off)=unsigned(raceShortcutFilter);
 DWORD old=0;if(!VirtualProtect(code,sizeof(RouteSectionCode),PAGE_EXECUTE_READ,&old)){VirtualFree(code,0,MEM_RELEASE);return nullptr;}
 FlushInstructionCache(GetCurrentProcess(),code,sizeof(RouteSectionCode));return code;
}
// Expanded-world route capacity is a world responsibility. Event authoring,
// driver state, custom goals and opponent selection live in the event ASI.
static double(__thiscall* worldSectionOriginal)(void*,float*,float*,int,int)=nullptr;
static double __fastcall WorldRouteSection(void* self,void*,float* p,float* d,int a,int b){
 memset(raceShortcutFilter,1,sizeof(raceShortcutFilter));
 return worldSectionOriginal(self,p,d,a,b);
}
static bool InstallWorldRaceNavigation(){
 const BYTE section[]={0x6A,0xFF,0x68,0x1C,0x61,0x87,0x00};
 auto target=reinterpret_cast<void*>(0x5FE840);
 if(memcmp(target,section,sizeof(section)))return false;
 auto expanded=MakeExpandedRouteSection();if(!expanded)return false;
 if(MH_CreateHook(target,WorldRouteSection,reinterpret_cast<void**>(&worldSectionOriginal))!=MH_OK)return false;
 worldSectionOriginal=reinterpret_cast<decltype(worldSectionOriginal)>(expanded);
 if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return false;}
 Log("WORLD_ROUTE_CAPACITY_ACTIVE shortcut_groups=256 native_section_algorithm=1 event_state=0 driving_hooks=0");return true;
}
