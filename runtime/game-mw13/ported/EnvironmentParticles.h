#pragma once
// Authored effects run in MW's particle engine; no substitute particle simulation.
struct AmbientParticle {unsigned section=0,triggers=0;bool repeat=true,observed=false,bird=false;float range=0;Vec3 position{};std::vector<BYTE> source;void* chunk=nullptr;DWORD retry=0;};
static std::vector<AmbientParticle> ambientParticles;
static int(__cdecl* ambientLoad)(void*);static int(__cdecl* ambientUnload)(void*);
static bool ambientActive=false;static DWORD ambientSelect=0,ambientReport=0;
static bool ambientDiagnostics=false;static double ambientTickTotal=0,ambientTickMax=0;static unsigned ambientTicks=0;
static bool AmbientBird(unsigned hash){
 switch(hash){case 0x45F3B7ED:case 0x39EDE226:case 0x4D9E7F58:
 case 0xA2736753:case 0xA97F118D:case 0x9F538372:case 0x5DF1D3C8:return true;default:return false;}
}
static int __cdecl AmbientLoadHook(void* chunk){if(ambientActive&&*static_cast<unsigned*>(chunk)==0x3BC00)return 1;return ambientLoad(chunk);}
static int __cdecl AmbientUnloadHook(void* chunk){if(ambientActive&&*static_cast<unsigned*>(chunk)==0x3BC00)return 1;return ambientUnload(chunk);}
static void ReleaseAmbient(AmbientParticle& p){if(!p.chunk)return;ambientUnload(p.chunk);VirtualFree(p.chunk,0,MEM_RELEASE);p.chunk=nullptr;p.observed=false;}
static bool CreateAmbient(AmbientParticle& p){
 unsigned hash=0;memcpy(&hash,p.source.data(),sizeof(hash));
 auto collection=reinterpret_cast<void*(__cdecl*)(unsigned,unsigned)>(0x455FD0)(0xABA86E60,hash);
 if(!collection)return false;
 unsigned bytes=32+unsigned(p.source.size());auto chunk=static_cast<BYTE*>(VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!chunk)return false;auto h=reinterpret_cast<unsigned*>(chunk);h[0]=0x3BC00;h[1]=bytes-8;h[4]=0;h[5]=5;h[6]=1;h[7]=2600;memcpy(chunk+32,p.source.data(),p.source.size());if(!ambientLoad(chunk)){VirtualFree(chunk,0,MEM_RELEASE);return false;}p.chunk=chunk;
 if(worldLogEnabled){static unsigned diagnosed=0;if(diagnosed++<32)Log("AMBIENT_CREATE hash=%08X collection=%p enabled=%u effect=%p triggers=%u",hash,collection,unsigned(*reinterpret_cast<BYTE*>(0x8F86EC)),*reinterpret_cast<void**>(chunk+44),p.triggers);}return true;}
static bool LoadSourceAmbientParticles(){try{
 using namespace w2v2disk;auto input=SourceInput(51,"environment/emitters");need(input.get<unsigned>()==0x32454d57,"source ambient magic");auto n=input.get<unsigned>();need(n<10000,"source ambient count");std::vector<AmbientParticle> staged;
 for(unsigned i=0;i<n;++i){AmbientParticle p;p.section=input.get<unsigned>();auto bytes=input.get<unsigned>();p.range=input.get<float>();p.triggers=input.get<unsigned>();auto repeat=input.get<unsigned>();need(p.triggers<=80&&bytes==80+48*p.triggers&&bytes<4096&&repeat<=1&&std::isfinite(p.range)&&p.range>0&&p.range<=2000,"source ambient dimensions");p.repeat=repeat!=0;p.source=input.take(bytes);need(r32(p.source,4)==0&&r16(p.source,8)==2600&&r16(p.source,10)==p.triggers&&r32(p.source,12)==0,"source ambient native pointers/header");for(unsigned k=16;k<80;k+=4)rf(p.source,k);memcpy(&p.position,p.source.data()+64,12);staged.push_back(std::move(p));}input.end();ambientParticles=std::move(staged);return true;
 }catch(const std::exception& error){Log("SOURCE_AMBIENT_REFUSED reason=%s",error.what());++v2Failures;return false;}}
static void InitAmbientParticles(){
#ifdef W2C_PUBLIC_BUILD
 if(!v2Store.bundle().find(51,"environment/emitters"))return;
#endif
 if(sourceRuntime){if(!LoadSourceAmbientParticles())return;}else{
 FILE* f=nullptr;fopen_s(&f,Path("Environment/emitters.bin").c_str(),"rb");if(!f)return;unsigned magic=0,count=0;bool ok=fread(&magic,4,1,f)==1&&fread(&count,4,1,f)==1&&(magic==0x31454D57||magic==0x32454D57)&&count<10000;
 for(unsigned n=0;ok&&n<count;++n){AmbientParticle p;unsigned size=0,flags=1;ok=fread(&p.section,4,1,f)==1&&fread(&size,4,1,f)==1&&fread(&p.range,4,1,f)==1&&fread(&p.triggers,4,1,f)==1;if(magic==0x32454D57)ok=ok&&fread(&flags,4,1,f)==1&&flags<=1;p.repeat=flags!=0;ok=ok&&size==80+48*p.triggers&&size<4096&&std::isfinite(p.range)&&p.range>0&&p.range<=2000;if(!ok)break;p.source.resize(size);ok=fread(p.source.data(),1,size,f)==size;if(!ok)break;memcpy(&p.position,p.source.data()+64,12);ambientParticles.push_back(std::move(p));}
 ok=ok&&fgetc(f)==EOF;fclose(f);for(const auto& p:ambientParticles)if(!std::isfinite(p.position.x)||!std::isfinite(p.position.y)||!std::isfinite(p.position.z))ok=false;if(!ok){ambientParticles.clear();Log("AMBIENT_REFUSED catalog");return;}
 }
 const BYTE loadGuard[]={0x8B,0x44,0x24,0x04,0x8B,0x08,0x83,0xEC,0x0C};const BYTE unloadGuard[]={0x8B,0x44,0x24,0x04,0x8B,0x08};
 if(memcmp(reinterpret_cast<void*>(0x50CC60),loadGuard,sizeof(loadGuard))||memcmp(reinterpret_cast<void*>(0x5099C0),unloadGuard,sizeof(unloadGuard))){Log("AMBIENT_REFUSED native guards");return;}
#ifdef W2C_PUBLIC_BUILD
 // Public packs preserve the destination's stock native banks. Our owned
 // chunks may use the same loader, but must not intercept stock effects.
 ambientLoad=reinterpret_cast<decltype(ambientLoad)>(0x50CC60);
 ambientUnload=reinterpret_cast<decltype(ambientUnload)>(0x5099C0);
#else
 if(MH_CreateHook(reinterpret_cast<void*>(0x50CC60),AmbientLoadHook,reinterpret_cast<void**>(&ambientLoad))!=MH_OK)return;
 if(MH_CreateHook(reinterpret_cast<void*>(0x5099C0),AmbientUnloadHook,reinterpret_cast<void**>(&ambientUnload))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(0x50CC60));return;}
 if(MH_EnableHook(reinterpret_cast<void*>(0x50CC60))!=MH_OK||MH_EnableHook(reinterpret_cast<void*>(0x5099C0))!=MH_OK){MH_DisableHook(reinterpret_cast<void*>(0x50CC60));MH_DisableHook(reinterpret_cast<void*>(0x5099C0));return;}
 ambientDiagnostics=WorldDiagnosticFlag(Path("W2CMWv2Bootstrap.ini").c_str(),"Probe","AmbientLifecycleDiagnostics");
#endif
 for(auto& p:ambientParticles){unsigned hash;memcpy(&hash,p.source.data(),sizeof(hash));p.bird=AmbientBird(hash);}
 ambientActive=true;Log("AMBIENT_ACTIVE authored=%u native_load=0050CC60 native_unload=005099C0 diagnostics=%u",unsigned(ambientParticles.size()),unsigned(ambientDiagnostics));
}
static void AmbientFlow(){static int previous=0;int flow=*reinterpret_cast<int*>(0x925E90);if(ambientActive&&flow!=6&&previous==6){for(auto& p:ambientParticles)ReleaseAmbient(p);ambientSelect=0;Log("AMBIENT_FLOW_RELEASE state=%d",flow);}previous=flow;}
static void TickAmbientParticles(){if(!ambientActive)return;DWORD now=GetTickCount();if(now-ambientSelect<100)return;ambientSelect=now;double tickStart=worldLogEnabled?Clock():0;
#ifdef W2C_PUBLIC_BUILD
 if(sourceCarbonPursuit&&!EnsureSourcePublicPursuitVault())return;
#endif
 // Opt-in sandbox test: destroy one owned effect through the same native
 // destructor/pool path used by the chunk unloader, then recycle all owned
 // chunks. It changes neither gameflow nor native world/save state.
 static DWORD testStart=0;static unsigned testStage=0;if(ambientDiagnostics){if(!testStart)testStart=now;
 if(testStage==0&&now-testStart>10000){const BYTE guard[]={0x6A,0xFF,0x68,0x2B,0xDF,0x86,0x00};for(auto& p:ambientParticles)if(p.chunk&&!p.triggers){auto group=*reinterpret_cast<void**>(static_cast<BYTE*>(p.chunk)+44);if(!group)continue;if(memcmp(reinterpret_cast<void*>(0x5094C0),guard,sizeof(guard)))break;reinterpret_cast<void(__thiscall*)(void*)>(0x5094C0)(group);reinterpret_cast<void(__cdecl*)(void*,void*)>(0x463CA0)(*reinterpret_cast<void**>(0x916068),group);Log("AMBIENT_TEST_NATIVE_DESTROY callback_cleared=%u",!*reinterpret_cast<void**>(static_cast<BYTE*>(p.chunk)+44));break;}testStage=1;}
 if(testStage==1&&now-testStart>16000){unsigned count=0;for(auto& p:ambientParticles)if(p.chunk){ReleaseAmbient(p);++count;}Log("AMBIENT_TEST_RECYCLE released=%u",count);testStage=2;}}
 std::vector<std::pair<float,unsigned>> candidates;for(unsigned n=0;n<ambientParticles.size();++n){auto& p=ambientParticles[n];if(zoneIndex!=~0u&&p.section!=26000&&!zones[zoneIndex].sections.count(p.section))continue;Vec3 d=Sub(p.position,cameraPosition);float distance=Dot(d,d);if(distance<p.range*p.range)candidates.emplace_back(distance/(p.range*p.range),n);}
 std::sort(candidates.begin(),candidates.end());if(candidates.size()>96)candidates.resize(96);std::set<unsigned> selected;for(auto& n:candidates)selected.insert(n.second);unsigned active=0,added=0,removed=0,recovered=0;
 for(unsigned n=0;n<ambientParticles.size();++n){auto& p=ambientParticles[n];if(p.chunk&&!selected.count(n)){ReleaseAmbient(p);++removed;}
 // MW can destroy effects during a world reload without unloading our private
 // chunks. Its destructor clears the source record's runtime pointer. Trigger
 // groups intentionally have a null pointer while waiting and must stay registered.
 if(p.chunk){bool live=*reinterpret_cast<void**>(static_cast<BYTE*>(p.chunk)+44)!=nullptr;if(live)p.observed=true;
 if(!p.triggers&&p.repeat&&p.observed&&!live){ReleaseAmbient(p);p.retry=now;++recovered;}}
 if(p.chunk)++active;}
 for(auto& n:candidates){auto& p=ambientParticles[n.second];if(p.chunk||now-p.retry<1000||added>=8)continue;unsigned groups=*reinterpret_cast<unsigned*>(0x916A50),emitters=*reinterpret_cast<unsigned*>(0x916A54);if(groups>136||emitters>400)break;p.retry=now;if(CreateAmbient(p)){++added;++active;}}
 if(recovered)Log("AMBIENT_NATIVE_RECOVERY count=%u",recovered);
 if(!worldLogEnabled)return;
 double elapsed=Clock()-tickStart;ambientTickTotal+=elapsed;ambientTickMax=std::max(ambientTickMax,elapsed);++ambientTicks;
 if(now-ambientReport>2000){
  ambientReport=now;unsigned effects=0,particles=0,birdGroups=0,birdParticles=0;
  for(auto& p:ambientParticles)if(p.chunk){auto g=*reinterpret_cast<BYTE**>(static_cast<BYTE*>(p.chunk)+44);if(g){
   ++effects;auto end=g+8;unsigned visited=0,groupParticles=0;
   for(auto e=*reinterpret_cast<BYTE**>(end);e!=end&&visited++<500;e=*reinterpret_cast<BYTE**>(e))groupParticles+=*reinterpret_cast<unsigned short*>(e+28);
   particles+=groupParticles;
   if(p.bird){++birdGroups;birdParticles+=groupParticles;}
  }}
  Log("AMBIENT_FRAME resident=%u effects=%u particles=%u added=%u removed=%u eligible=%u native_particles=%u tick_mean_ms=%.3f tick_max_ms=%.3f camera=%.1f,%.1f,%.1f bird_groups=%u bird_particles=%u",active,effects,particles,added,removed,unsigned(candidates.size()),*reinterpret_cast<unsigned*>(0x9166C0),ambientTickTotal/ambientTicks,ambientTickMax,cameraPosition.x,cameraPosition.y,cameraPosition.z,birdGroups,birdParticles);
 }
}
