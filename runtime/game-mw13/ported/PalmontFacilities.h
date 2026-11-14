#pragma once
#include <NFSPluginSDK/Game.MW05/Types/IPlayer.h>
#include "PalmontFacilityData.h"
#ifdef W2C_PUBLIC_BUILD
namespace palmont_facility {
using NFSPluginSDK::MW05::IPlayer;
using MenuOpen=void(__thiscall*)(void*);
using GetCommand=unsigned**(__thiscall*)(void*,unsigned**);
using ExitPose=void(__thiscall*)(void*,Vec3*,Vec3*);
static GetCommand originalCommand=nullptr;
static ExitPose originalExitPose=nullptr;
static bool installed=false,homeHeld=false,shop=false,exitRead=false;
static int nearby=-1,cooldownEntry=-1,returnFacility=-1;
static DWORD cooldownStart=0;
static const auto safehouse=reinterpret_cast<const char*>(0x8A055C);
static const auto customshop=reinterpret_cast<const char*>(0x8A0548);

static BYTE* Manager(){return *reinterpret_cast<BYTE**>(0x91E00C);}
static bool Roaming(){
 auto race=*reinterpret_cast<const BYTE**>(0x91E000);
 return race&&*reinterpret_cast<const unsigned*>(race+0x1960)==0;
}
static IPlayer* LocalPlayer(){
 auto list=*reinterpret_cast<IPlayer***>(0x92D87C);
 const auto count=*reinterpret_cast<const unsigned*>(0x92D884);
 if(list&&count<=8)for(unsigned i=0;i<count;++i)
  if(list[i]&&list[i]->IsLocal()&&list[i]->GetSimable())return list[i];
 return nullptr;
}
static void* Menu(IPlayer* player){
 auto hud=reinterpret_cast<BYTE*>(player->GetHud());
 if(!hud)return nullptr;
 auto menu=reinterpret_cast<void*(__thiscall*)(void*,void*)>(0x5D59F0)(
     *reinterpret_cast<void**>(hud+4),reinterpret_cast<void*>(0x5659C0));
 return menu&&*reinterpret_cast<unsigned*>(menu)==0x8A24F8?menu:nullptr;
}
static void* Pursuit(BYTE* manager){
 void* pursuit=nullptr;void* meter=nullptr;
 reinterpret_cast<void(__thiscall*)(void*,void**,void**)>(0x5EB9E0)(manager,&pursuit,&meter);
 return pursuit;
}
static bool IsCooldown(void* pursuit){
 auto vtable=*reinterpret_cast<void***>(pursuit);
 // Same interface slots as GManager's update and the stock HUD action 46.
 return reinterpret_cast<unsigned(__thiscall*)(void*)>(vtable[0x114/4])(pursuit)==2;
}
static bool CanPrompt(BYTE* manager){
 // Use the stock free-roam input gate outside pursuit. Its last check
 // rejects any pursuit, including cooldown, which the HUD handles itself.
 if(manager[0x2CE])return reinterpret_cast<bool(__cdecl*)()>(0x5EC040)();
 auto frontend=*reinterpret_cast<BYTE**>(0x91CB20);
 return manager[0x2CF]&&frontend&&!frontend[0x1E]&&!frontend[0x1F]&&
     !*reinterpret_cast<BYTE*>(0x992244)&&!*reinterpret_cast<void**>(0x9885C8)&&
     !*reinterpret_cast<BYTE*>(0x91CF41);
}
static int Nearest(IPlayer* player){
 Vec3 pos;const auto& native=player->GetPosition();
 memcpy(&pos,&native,sizeof(pos));
 float closest=64.f;int index=-1;
 for(unsigned i=0;i<std::size(palmontFacilities);++i){
  const auto& f=palmontFacilities[i];if(!f.shared)continue;
  const auto d=Sub(pos,f.position);const float distance=d.x*d.x+d.z*d.z;
  if(std::abs(d.y)<4.f&&distance<closest){closest=distance;index=int(i);}
 }
 return index;
}
static const char* SelectedType(){return shop?customshop:safehouse;}
static bool OwnPrompt(void* menu){
 if(!menu)return false;
 auto data=static_cast<const BYTE*>(menu);
 const auto type=*reinterpret_cast<const char* const*>(data+0x14);
 return !*reinterpret_cast<const unsigned*>(data+0x18)&&(type==safehouse||type==customshop);
}
static void Show(void* menu){
 reinterpret_cast<void(__thiscall*)(void*,const char*)>(0x57BDE0)(menu,SelectedType());
}
static void Hide(void* menu){
 if(OwnPrompt(menu))reinterpret_cast<MenuOpen>(0x57BEA0)(menu);
}
static void Open(void* menu,int index){
 const int previous=returnFacility;
 returnFacility=index;exitRead=false;
 // The stock custom shop may reject a player with no owned car.
 const auto next=*reinterpret_cast<unsigned*>(0x920390);
 reinterpret_cast<MenuOpen>(0x58C640)(menu);
 const bool queued=next!=*reinterpret_cast<unsigned*>(0x920390);
 if(!queued)returnFacility=previous;
 Log("PALMONT_FACILITY_NATIVE_MENU entry=%08X shop=%u queued=%u",palmontFacilities[index].entry,unsigned(shop),unsigned(queued));
}
static bool Engage(){
 if(!installed||nearby<0||!W2MW_IsWorldActiveV1())return false;
 if(cooldownEntry>=0)return true;
 auto manager=Manager();auto player=LocalPlayer();
 if(!manager||!player||!CanPrompt(manager)||Nearest(player)!=nearby)return false;
 auto menu=Menu(player);if(!OwnPrompt(menu))return false;
 if(auto pursuit=Pursuit(manager)){
  if(shop||!IsCooldown(pursuit))return true;
  cooldownEntry=nearby;cooldownStart=GetTickCount();
  auto vtable=*reinterpret_cast<void***>(pursuit);
  returnFacility=nearby;exitRead=false;
  reinterpret_cast<MenuOpen>(vtable[0x130/4])(pursuit);
  Log("PALMONT_FACILITY_COOLDOWN entry=%08X",palmontFacilities[nearby].entry);
 }else Open(menu,nearby);
 return true;
}
static unsigned** __fastcall Command(void* self,void*,unsigned** out){
 auto result=originalCommand(self,out);
 if(_ReturnAddress()!=reinterpret_cast<void*>(0x57C742)||!*out||**out!=46)return result;
 __try{if(Engage())*out=nullptr;}
 __except(EXCEPTION_EXECUTE_HANDLER){installed=false;Log("PALMONT_FACILITIES_DISABLED native action");}
 return result;
}
static void __fastcall ApplyExitPose(void* self,void*,Vec3* position,Vec3* forward){
 if(!installed||returnFacility<0||!position||!forward){originalExitPose(self,position,forward);return;}
 const auto& facility=palmontFacilities[returnFacility];
 const float radians=facility.heading*(-0.01745329252f);
 *position=facility.exit;
 *forward={std::sin(radians),0,std::cos(radians)};
 exitRead=true;
 Log("PALMONT_FACILITY_EXIT respawn=%08X",facility.respawn);
}
static void Update(){
 if(!installed)return;
 if(!W2MW_IsWorldActiveV1()||!Roaming()){nearby=-1;return;}
 if(exitRead){returnFacility=-1;cooldownEntry=-1;exitRead=false;}
 auto player=LocalPlayer();auto manager=Manager();
 if(!player||!manager)return;
 auto menu=Menu(player);if(!menu)return;
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 const bool held=foreground==GetCurrentProcessId()&&(GetAsyncKeyState(VK_HOME)&0x8000)!=0;
 const bool pressed=held&&!homeHeld;homeHeld=held;
 if(cooldownEntry>=0){
  if(Pursuit(manager)){
   // Bound a request that leaves the pursuit running.
   if(GetTickCount()-cooldownStart>15000){
    cooldownEntry=-1;returnFacility=-1;
   }
  }else if(CanPrompt(manager)){
   // Results screens can stay open indefinitely while the driver reads
   // them. Resume the entry only after normal free-roam input returns.
   const int entry=cooldownEntry;cooldownEntry=-1;
   if(Nearest(player)==entry){shop=false;Show(menu);Open(menu,entry);}
   else returnFacility=-1;
  }
  return;
 }
 const int next=CanPrompt(manager)?Nearest(player):-1;
 if(next!=nearby){
  if(nearby>=0)Hide(menu);
  nearby=next;shop=false;
  if(nearby>=0){
   Show(menu);
   Log("PALMONT_FACILITY_ENTRY entry=%08X selector=Home",palmontFacilities[nearby].entry);
  }
 }
 if(nearby>=0&&pressed&&manager[0x2CE]){
  shop=!shop;Show(menu);
  Log("PALMONT_FACILITY_CHOICE shop=%u",unsigned(shop));
 }
 if(nearby>=0&&!manager[0x2CE]&&shop){shop=false;Show(menu);}
}
}
static void TickPalmontFacilities(){
 __try{palmont_facility::Update();}
 __except(EXCEPTION_EXECUTE_HANDLER){palmont_facility::installed=false;Log("PALMONT_FACILITIES_DISABLED native update");}
}
static void InstallPalmontFacilities(){
 using namespace palmont_facility;
 const BYTE commandGuard[]={0x83,0x39,0x00,0x7E,0x13,0x8B,0x41,0x08};
 const BYTE exitGuard[]={0x6A,0xFF,0x68,0x88,0x51,0x87,0x00,0x64,0xA1};
 auto command=reinterpret_cast<void*>(0x633DC0);
 auto exitPose=reinterpret_cast<void*>(0x5E4DF0);
 if(memcmp(command,commandGuard,sizeof(commandGuard))||memcmp(exitPose,exitGuard,sizeof(exitGuard))){
  Log("PALMONT_FACILITIES_REFUSED native action or exit ABI");return;
 }
 if(MH_CreateHook(command,Command,reinterpret_cast<void**>(&originalCommand))!=MH_OK)return;
 if(MH_CreateHook(exitPose,ApplyExitPose,reinterpret_cast<void**>(&originalExitPose))!=MH_OK){
  MH_RemoveHook(command);return;
 }
 if(MH_EnableHook(command)!=MH_OK||MH_EnableHook(exitPose)!=MH_OK){
  MH_RemoveHook(command);MH_RemoveHook(exitPose);return;
 }
 installed=true;
 Log("PALMONT_FACILITIES_ACTIVE shared_safehouse_shop=4 selector=Home native_entry_exit=1");
}
#else
template<class T> static bool FacilityRead(const void* address,T* value){__try{if(!address)return false;memcpy(value,address,sizeof(T));return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static unsigned facilityChoiceRespawn=0;
static int facilityNearby=-1;
static bool facilityShop=false,facilityKeyHeld=false,facilityInstalled=false;
static DWORD facilityPromptTime=0;
using FacilityMenuFn=void(__thiscall*)(void*);
static FacilityMenuFn facilityOriginalMenu=nullptr;
static bool FacilityFreeRoam(){auto race=*reinterpret_cast<BYTE**>(0x91E000);return *reinterpret_cast<unsigned*>(0x925E90)==6&&race&&*reinterpret_cast<unsigned*>(race+0x1960)==0;}
static bool FacilityPlayer(Vec3& position,void*& hud){
 unsigned count=0;void** list=nullptr;void* player=nullptr;
 if(!FacilityRead(reinterpret_cast<void*>(0x92D884),&count)||!count||count>8||!FacilityRead(reinterpret_cast<void*>(0x92D87C),&list)||!FacilityRead(list,&player)||!player)return false;
 auto vt=*reinterpret_cast<void***>(player);position=reinterpret_cast<const Vec3&(__thiscall*)(void*)>(vt[3])(player);
 return std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z)&&FacilityRead(static_cast<BYTE*>(player)+0x28,&hud)&&hud;
}
static bool FacilityUnicode(void* root,const wchar_t* value){
 // FE type 5 is a group, type 2 a string. Never write a group as bString.
 void* todo[64]{};void* seen[64]{};void* leaves[16]{};unsigned queued=1,count=0,leafCount=0;todo[0]=root;
 while(queued){auto node=static_cast<BYTE*>(todo[--queued]);if(!node||count>=64)return false;for(unsigned i=0;i<count;++i)if(seen[i]==node)return false;seen[count++]=node;
  unsigned type=0;if(!FacilityRead(node+0x18,&type)||type<1||type>5)return false;
  void* next=nullptr;if(node!=root&&FacilityRead(node+4,&next)&&next){if(queued>=64)return false;todo[queued++]=next;}
  if(type==5){void* child=nullptr;if(!FacilityRead(node+0x64,&child))return false;if(child){if(queued>=64)return false;todo[queued++]=child;}}
  else if(type==2){if(leafCount>=16)return false;leaves[leafCount++]=node;}
 }
 if(!leafCount)return false;for(unsigned i=0;i<leafCount;++i)reinterpret_cast<void(__thiscall*)(void*,const wchar_t*)>(0x5BCD20)(static_cast<BYTE*>(leaves[i])+0x64,value);return true;
}
static bool FacilityPrompt(void* hud){
 void* object=nullptr;if(!FacilityRead(static_cast<BYTE*>(hud)+4,&object)||!object)return false;
 auto message=reinterpret_cast<void*(__thiscall*)(void*,void*)>(0x5D59F0)(object,reinterpret_cast<void*>(0x5650B0));if(!message)return false;
 if(!reinterpret_cast<bool(__thiscall*)(void*,const char*,bool,unsigned,unsigned,unsigned,unsigned)>(0x568030)(message," ",false,0x8AB83EDB,0,0,1))return false;
 auto engine=*reinterpret_cast<void**>(0x91CADC);const char* package=nullptr;if(!engine||!FacilityRead(static_cast<BYTE*>(message)-0x20,&package)||!package)return false;
 auto loaded=reinterpret_cast<void*(__thiscall*)(void*,const char*)>(0x516AF0)(engine,package);if(!loaded)return false;
 auto text=reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(0x5B8550)(loaded,0x32A7A521);
 return FacilityUnicode(text,facilityShop?L"\u30D1\u30FC\u30C4\u30B7\u30E7\u30C3\u30D7  [Home] \u5207\u308A\u66FF\u3048 / \u901A\u5E38\u306E\u6C7A\u5B9A\u64CD\u4F5C\u3067\u5165\u308B":L"\u30BB\u30FC\u30D5\u30CF\u30A6\u30B9  [Home] \u5207\u308A\u66FF\u3048 / \u901A\u5E38\u306E\u6C7A\u5B9A\u64CD\u4F5C\u3067\u5165\u308B");
}
static void TickPalmontFacilitiesImpl(){
 if(!facilityInstalled||!FacilityFreeRoam()){facilityNearby=-1;facilityKeyHeld=false;return;}Vec3 player{};void* hud=nullptr;if(!FacilityPlayer(player,hud))return;
 int nearest=-1;float best=64;for(unsigned i=0;i<std::size(palmontFacilities);++i){auto& f=palmontFacilities[i];if(!f.shared)continue;auto d=Vec3{player.x-f.position.x,player.y-f.position.y,player.z-f.position.z};float planar=d.x*d.x+d.z*d.z;if(std::abs(d.y)<4&&planar<best){best=planar;nearest=int(i);}}
 if(nearest!=facilityNearby){facilityNearby=nearest;facilityPromptTime=0;if(nearest>=0){facilityChoiceRespawn=palmontFacilities[nearest].respawn;facilityShop=false;Log("PALMONT_FACILITY_ENTRY key=%08X respawn=%08X shared_menu=1",palmontFacilities[nearest].entry,facilityChoiceRespawn);}}
 DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);bool held=owner==GetCurrentProcessId()&&(GetAsyncKeyState(VK_HOME)&0x8000)!=0;
 if(nearest>=0&&held&&!facilityKeyHeld){facilityShop=!facilityShop;facilityPromptTime=0;Log("PALMONT_FACILITY_CHOICE respawn=%08X shop=%u",facilityChoiceRespawn,facilityShop);}facilityKeyHeld=held;
 DWORD now=GetTickCount();if(nearest>=0&&now-facilityPromptTime>2000){facilityPromptTime=now;FacilityPrompt(hud);}
}
static void TickPalmontFacilities(){
 __try{TickPalmontFacilitiesImpl();}
 __except(EXCEPTION_EXECUTE_HANDLER){facilityInstalled=false;Log("PALMONT_FACILITIES_DISABLED invalid native UI state; event bridge preserved");}
}
static void __fastcall FacilityMenu(void* self,void*){
 bool redirect=false;const char* saved=nullptr;
 __try{auto manager=*reinterpret_cast<BYTE**>(0x91E00C);unsigned respawn=manager?*reinterpret_cast<unsigned*>(manager+0x108):0;
  // Native 00612030 records this marker on engagement before opening the FE
  // facility dialog. Keep native +108/+10C return markers and menu lifecycle.
  if(facilityShop&&facilityChoiceRespawn&&respawn==facilityChoiceRespawn){saved=*reinterpret_cast<const char**>(static_cast<BYTE*>(self)+0x14);redirect=saved&&strcmp(saved,"safehouse")==0;if(redirect){*reinterpret_cast<const char**>(static_cast<BYTE*>(self)+0x14)="customshop";Log("PALMONT_FACILITY_NATIVE_MENU shop=1 respawn=%08X",respawn);}}
 }__except(EXCEPTION_EXECUTE_HANDLER){redirect=false;}
 __try{facilityOriginalMenu(self);}__finally{if(redirect)*reinterpret_cast<const char**>(static_cast<BYTE*>(self)+0x14)=saved;}
}
static void InstallPalmontFacilities(){
 const BYTE guard[]={0x6A,0xFF,0x68,0x5D,0x24,0x87,0x00,0x64,0xA1};auto target=reinterpret_cast<void*>(0x58C640);
 if(memcmp(target,guard,sizeof(guard))||MH_CreateHook(target,FacilityMenu,reinterpret_cast<void**>(&facilityOriginalMenu))!=MH_OK||MH_EnableHook(target)!=MH_OK){Log("PALMONT_FACILITIES_REFUSED menu byte guard");return;}
 facilityInstalled=true;Log("PALMONT_FACILITIES_ACTIVE entrances=8 dealers=4 shared_safehouse_shop=4 selector=Home native_entry_exit=1");
}
#endif
