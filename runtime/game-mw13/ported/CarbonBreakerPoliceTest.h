#pragma once
// Isolated lab only: explicit TestMode + diagnostic logging + exact opt-in.
// Use CopMgr's factory and pursuit ownership; never fabricate a police racer.
static bool carbonBreakerPoliceTest=false,carbonBreakerPoliceRequested=false,carbonBreakerPolicePlaced=false;
static DWORD carbonBreakerPoliceAt=0,carbonBreakerPoliceSampleAt=0;
static unsigned carbonBreakerPoliceHandle=0;
static unsigned carbonBreakerPoliceAnchor=23814;
static Vec3 CarbonPolicePoint(){
 Vec3 point=carbonBreakerPoliceAnchor==23740?Vec3{-3527.f,124.44f,-1655.f}:Vec3{564.6522f,17.8f,-1725.1699f};
 // Only this explicit diagnostic test reads these temporary coordinates.
 const char* keys[]={"CarbonBreakerPoliceX","CarbonBreakerPoliceY","CarbonBreakerPoliceZ"};
 for(unsigned n=0;n<3;n++){char text[64]{};GetPrivateProfileStringA("Probe",keys[n],"",text,sizeof(text),Path("W2CMWv2Bootstrap.ini").c_str());if(*text){char* end=nullptr;float v=strtof(text,&end);if(end&&!*end&&std::isfinite(v))(&point.x)[n]=v;}}
 return point;
}
static void TickCarbonBreakerPoliceTest(){
 if(!carbonBreakerPoliceTest||!carbonBreakerPrototype)return;
 NativeProp* anchor=nullptr;for(auto p:activeProps)if(p->id==carbonBreakerPoliceAnchor&&p->initialized){anchor=p;break;}
 if(!anchor)return;
 DWORD now=GetTickCount();if(!carbonBreakerPoliceAt)carbonBreakerPoliceAt=now;
 if(now-carbonBreakerPoliceAt<1000)return;
 auto manager=*reinterpret_cast<void**>(0x92C67C);
 if(!manager||*reinterpret_cast<unsigned*>(manager)!=0x891900)return;
 if(!carbonBreakerPoliceRequested){
  const BYTE guard[]={0x83,0xEC,0x0C,0xA1,0x7C,0xD8,0x92,0x00},lockGuard[]={0x64,0xA1,0,0,0,0,0x6A,0xFF};
  // PursueAtHeatLevel calls the ordinary police factory and binds pursuit.
  if(memcmp(reinterpret_cast<void*>(0x42EBF0),guard,sizeof(guard))||memcmp(reinterpret_cast<void*>(0x411410),lockGuard,sizeof(lockGuard))){Log("CARBON_PB_REFUSED police heat guard");carbonBreakerPoliceTest=false;return;}
  // World2MWProbe deliberately disables cops for ordinary geometry trials.
  // This isolated police-context test opts back into the native manager.
  *reinterpret_cast<bool*>(0x8F86C0)=false;
  // AICopManager copies SkipFEDisableCops into ICopMgr::mDisableCops
  // during construction; restoring only the launch flag is too late.
  *reinterpret_cast<int*>(0x90D5EC)=0;
  reinterpret_cast<void(__thiscall*)(void*,bool)>(0x411410)(manager,false);
  reinterpret_cast<void(__thiscall*)(void*,int)>(0x42EBF0)(manager,1);
  const BYTE spawnGuard[]={0x83,0xEC,0x3C,0x8B,0x44,0x24,0x40,0x53};
  if(memcmp(reinterpret_cast<void*>(0x439DD0),spawnGuard,sizeof(spawnGuard))){Log("CARBON_PB_REFUSED police spawn guard");carbonBreakerPoliceTest=false;return;}
  Vec3 position=CarbonPolicePoint(),forward{0,0,-1};
  reinterpret_cast<void(__thiscall*)(void*,const Vec3*,const Vec3*,const char*,bool,bool)>(0x439DD0)(manager,&position,&forward,"copmidsize",true,false);
  carbonBreakerPoliceRequested=true;Log("CARBON_PB_POLICE_REQUEST_ACTIVE native_factory=1 heat=1 explicit_native_request=1");
 }
 if(now-carbonBreakerPoliceSampleAt<200)return;carbonBreakerPoliceSampleAt=now;
 unsigned count=*reinterpret_cast<unsigned*>(0x92CD24),capacity=*reinterpret_cast<unsigned*>(0x92CD20);
 auto cars=*reinterpret_cast<BYTE***>(0x92CD1C);if(!cars||count>capacity||count>512)return;
 for(unsigned i=0;i<count;i++){
  auto car=cars[i];if(!car||*reinterpret_cast<unsigned*>(car)!=0x8AA828||*reinterpret_cast<unsigned*>(car+0x94)!=2)continue;
  if(!reinterpret_cast<bool(__thiscall*)(void*)>(0x688200)(car))continue;
  auto rb=*reinterpret_cast<BYTE**>(car-0x34);if(!rb)continue;auto vt=*reinterpret_cast<void***>(rb);
  if(vt[8]!=reinterpret_cast<void*>(0x671180)&&vt[8]!=reinterpret_cast<void*>(0x6B6360))continue;
  unsigned handle=*reinterpret_cast<unsigned*>(car-0x78);
  if(!carbonBreakerPolicePlaced){
   if(vt[23]!=reinterpret_cast<void*>(0x696FB0)&&vt[23]!=reinterpret_cast<void*>(0x670EE0)){Log("CARBON_PB_REFUSED police body setter");carbonBreakerPoliceTest=false;return;}
   Vec3 point=CarbonPolicePoint(),zero{};
   reinterpret_cast<void(__thiscall*)(void*,const Vec3*)>(vt[23])(rb,&point);
   reinterpret_cast<void(__thiscall*)(void*,const Vec3*)>(vt[24])(rb,&zero);
   reinterpret_cast<void(__thiscall*)(void*,const Vec3*)>(vt[25])(rb,&zero);
   carbonBreakerPolicePlaced=true;carbonBreakerPoliceHandle=handle;
   Log("CARBON_PB_POLICE_PLACED_ACTIVE handle=%u position=%.3f,%.3f,%.3f native_controller=1",handle,point.x,point.y,point.z);
  }
  if(handle!=carbonBreakerPoliceHandle)continue;
  const auto pos=reinterpret_cast<const Vec3*(__thiscall*)(void*)>(vt[8])(rb);
  const auto velocity=reinterpret_cast<const Vec3*(__thiscall*)(void*)>(vt[9])(rb);
  auto com=*reinterpret_cast<void**>(car+4);
  auto damage=reinterpret_cast<BYTE*(__thiscall*)(void*,void*)>(0x5D59F0)(com,reinterpret_cast<void*>(0x405000));
  float health=-1.f;if(damage){auto dv=*reinterpret_cast<void***>(damage);if(dv[5]==reinterpret_cast<void*>(0x6B8950))health=reinterpret_cast<float(__thiscall*)(void*)>(dv[5])(damage);}
  unsigned pursuit=0;auto ai=*reinterpret_cast<BYTE**>(car+0x54);if(ai&&*reinterpret_cast<unsigned*>(ai)==0x892480)pursuit=*reinterpret_cast<unsigned*>(ai+0x70);
  Log("CARBON_PB_POLICE_SAMPLE_ACTIVE handle=%u at=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f health=%.6f destroyed=%u pursuit=%08X",handle,pos->x,pos->y,pos->z,velocity->x,velocity->y,velocity->z,health,unsigned(reinterpret_cast<bool(__thiscall*)(void*)>(0x688170)(car)),pursuit);
 }
}
