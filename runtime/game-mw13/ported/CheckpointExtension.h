#pragma once
#include "../integration/MWCheckpointRuntime.h"
static bool InstallCheckpointExtension(){return mw_checkpoint::InstallCheckpointExtension(GetModuleHandleW(L"MWEventLoader.asi"),"MWEL_CheckpointRuntimeV1",WorldDebugLog);}
#pragma comment(linker,"/EXPORT:W2MW_CheckpointRuntimeV1=_W2MW_CheckpointRuntimeV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_CheckpointRuntimeV1(mw_checkpoint::Runtime* out,unsigned size){return mw_checkpoint::QueryRuntime(out,size);}
