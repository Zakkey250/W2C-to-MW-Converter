#pragma once
// Carbon's flag-1 state-entry actions are non-blocking, one-shot backgrounds.
// Only our converted states carry this private stimulus. Stock MW is unchanged.
static bool carbonBreakerFamilies=false;
static constexpr unsigned carbonBackgroundStimulus=0xFA6C9D01;
static thread_local unsigned carbonBackgroundTransition=0;
static void (__thiscall* carbonOriginalSetState)(void*,float,unsigned);
static void (__thiscall* carbonOriginalReleaseState)(void*);
static void (__thiscall* carbonBackgroundStop)(void*,float,bool,void*)=reinterpret_cast<void(__thiscall*)(void*,float,bool,void*)>(0x634090);
static const unsigned* CarbonStateResponse(const void* state,unsigned stimulus){
 if(!state)return nullptr;
 auto words=static_cast<const unsigned*>(state);unsigned count=words[0];
 if(count>1024)return nullptr;
 auto first=words+2,last=first+count;
 auto found=std::lower_bound(first,last,stimulus);
 return found!=last&&*found==stimulus?last+2*(found-first):nullptr;
}
static bool CarbonBackgroundActive(void* system){
 if(!carbonBreakerFamilies||!system)return false;
 auto words=static_cast<unsigned*>(system);
 auto response=CarbonStateResponse(reinterpret_cast<void*>(words[2]),carbonBackgroundStimulus);
 return response&&response[1]&&response[1]==words[3];
}
static void __fastcall CarbonSetState(void* self,void*,float time,unsigned state){
 ++carbonBackgroundTransition;carbonOriginalSetState(self,time,state);--carbonBackgroundTransition;
 if(!carbonBreakerFamilies||carbonBackgroundTransition)return;
 auto words=static_cast<unsigned*>(self);
 if(!words[3]&&CarbonStateResponse(reinterpret_cast<void*>(words[2]),carbonBackgroundStimulus)){
  // Bypass our ProcessStimulus wrapper: native registration/start callbacks
  // and the active-system array must continue to be owned by the game.
  ++carbonBackgroundTransition;
  bool started=propOriginalEvent(self,carbonBackgroundStimulus,time,nullptr,2);
  --carbonBackgroundTransition;
  if(worldLogEnabled)Log("CARBON_PB_BACKGROUND_ENTRY_ACTIVE state=%08X started=%u",words[8],unsigned(started));
 }
}
static bool CarbonProcessEvent(void* self,unsigned event,float time,void* data,unsigned mode){
 if(!carbonBackgroundTransition&&CarbonBackgroundActive(self)&&event!=carbonBackgroundStimulus){
  // A background is not an occupied action in Carbon. Unknown stimuli are
  // ignored rather than queued behind an indefinitely held MW action.
  auto words=static_cast<unsigned*>(self);
  if(!CarbonStateResponse(reinterpret_cast<void*>(words[2]),event))return false;
  ++carbonBackgroundTransition;
  carbonBackgroundStop(self,time,false,nullptr);
  --carbonBackgroundTransition;
 }
 return propOriginalEvent(self,event,time,data,mode);
}
static void __fastcall CarbonReleaseState(void* self,void*){
 // Native InternalRelease stops the action and calls SetState before clearing
 // mEngine. Never restart a held background while its system is being freed.
 ++carbonBackgroundTransition;carbonOriginalReleaseState(self);--carbonBackgroundTransition;
}
static bool InitCarbonBackground(){
 const BYTE guard[]={0x56,0x8B,0xF1,0x8B,0x46,0x04,0x85,0xC0};
 const BYTE releaseGuard[]={0x56,0x8B,0xF1,0x83,0x3E,0x00};
 auto target=reinterpret_cast<void*>(0x644280),release=reinterpret_cast<void*>(0x645CE0);
 if(!propOriginalEvent||memcmp(target,guard,sizeof(guard))||memcmp(release,releaseGuard,sizeof(releaseGuard)))return false;
 if(MH_CreateHook(target,CarbonSetState,reinterpret_cast<void**>(&carbonOriginalSetState))!=MH_OK)return false;
 if(MH_CreateHook(release,CarbonReleaseState,reinterpret_cast<void**>(&carbonOriginalReleaseState))!=MH_OK){MH_RemoveHook(target);return false;}
 if(MH_EnableHook(target)!=MH_OK||MH_EnableHook(release)!=MH_OK){MH_DisableHook(target);MH_RemoveHook(target);MH_DisableHook(release);MH_RemoveHook(release);return false;}return true;
}
