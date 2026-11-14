#pragma once
#include "HeliClearancePolicy.h"
#include "WorldVehiclePosition.h"
static bool(__thiscall* w2cHeliAltitudeOriginal)(void*,float*);
static bool W2CHeliAltitude(void* iface,float* point){
 __try{
  if(!W2MW_IsWorldActiveV1()||!iface||!point||*reinterpret_cast<unsigned*>(iface)!=0x891F18)return false;
  auto primary=static_cast<BYTE*>(iface)-0x7A4;if(*reinterpret_cast<unsigned*>(primary)!=0x8920D8)return false;
  Vec3 target{point[0],point[1],point[2]},position=target;
  auto vehicle=*reinterpret_cast<BYTE**>(primary+0x48);
  if(!W2CReadVehiclePosition(vehicle,position))return false;
  float altitude=0;if(!W2CHeliWorldAltitude(collision,position,target,altitude))return false;
  point[1]=altitude;static DWORD next=0;DWORD now=GetTickCount();if(now>=next){next=now+5000;
   Log("W2C_HELI_CLEARANCE native_HeliSheet_replaced=1 requested=%.2f applied=%.2f roof_collision=1 rotor_margin=8",target.y,altitude);}
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool __fastcall W2CHeliAltitudeHook(void* iface,void*,float* point){
 HeliClearanceWorkTimer work;
 if(W2CHeliAltitude(iface,point))return true;
 return w2cHeliAltitudeOriginal(iface,point);
}
static void InstallW2CHeliAltitude(){
 const BYTE guard[]={0x83,0xEC,0x08,0x56,0x8B,0x74,0x24,0x10,0x8B,0x46,0x08};auto site=reinterpret_cast<void*>(0x409980);
 if(memcmp(site,guard,sizeof(guard))||*reinterpret_cast<unsigned*>(0x891F40)!=0x409980){Log("W2C_HELI_CLEARANCE_REFUSED ABI_guard");return;}
 if(MH_CreateHook(site,W2CHeliAltitudeHook,reinterpret_cast<void**>(&w2cHeliAltitudeOriginal))!=MH_OK)return;
 if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);return;}
 Log("W2C_HELI_CLEARANCE_ACTIVE FilterHeliAltitude=00409980 W2C_only=1 highest_collision_surface=1");
}
