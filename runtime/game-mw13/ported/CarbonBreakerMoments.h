#pragma once
// Opt-in converted Carbon cameras and source placement markers.
// The stable render/shader hooks and MW's existing camera groups are retained.
struct CarbonMomentMarker {unsigned scene;BYTE record[48];};
static bool carbonBreakerMoments=false;
static BYTE* carbonMomentCameraPack=nullptr;
static std::vector<CarbonMomentMarker> carbonMomentMarkers;
static std::set<unsigned> sourceCameraScenes;
static bool CarbonMomentScene(unsigned scene){
 if(sourceRuntime)return sourceCameraScenes.count(scene)!=0;
 if(carbonBreakerFamilies)return std::find(std::begin(carbonFamilyScenes),std::end(carbonFamilyScenes),scene)!=std::end(carbonFamilyScenes);
 return scene==0xF562A14C||scene==0x2A384A3D;
}
static BYTE* CarbonMomentPinned(const char* name,unsigned size,const char* expected){
 FILE* file=nullptr;fopen_s(&file,Path(name).c_str(),"rb");if(!file)return nullptr;
 auto data=static_cast<BYTE*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 bool valid=data&&fread(data,1,size,file)==size&&fgetc(file)==EOF;fclose(file);
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[32]{};DWORD length=32;char hex[65]{};
 valid=valid&&CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,data,size,0)&&CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0);
 for(unsigned i=0;i<32;i++)sprintf_s(hex+i*2,3,"%02x",digest[i]);
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
 if(!valid||strcmp(hex,expected)){if(data)VirtualFree(data,0,MEM_RELEASE);return nullptr;}return data;
}
static void LoadCarbonBreakerMoments(){
 if(!(carbonBreakerLab&&CarbonBreakerFlag("W2CMWv2Bootstrap.ini","Probe","CarbonBreakerMomentPrototype"))&&
    !(carbonBreakerInstalled&&CarbonBreakerFlag("World2MWCarbonBreakers.ini","World","Moments")))return;
 const BYTE guard[]={0x51,0x55,0x56,0x8B,0x74,0x24,0x10,0x8B};
 if(memcmp(reinterpret_cast<void*>(0x4787C0),guard,sizeof(guard))){Log("CARBON_PB_REFUSED camera loader guard");return;}
 std::vector<unsigned> scenes=carbonBreakerFamilies?std::vector<unsigned>(std::begin(carbonFamilyScenes),std::end(carbonFamilyScenes)):std::vector<unsigned>{0xF562A14Cu,0x2A384A3Du};
 for(unsigned scene:scenes)if(reinterpret_cast<int(__thiscall*)(void*,unsigned)>(0x46E710)(reinterpret_cast<void*>(0x8EE320),scene)){Log("CARBON_PB_REFUSED existing camera group scene=%08X",scene);return;}
 auto cameras=carbonBreakerFamilies?CarbonMomentPinned("PursuitPayload/carbon-families-cameras.bun",carbonFamilyCameraBytes,carbonFamilyCameraSha):CarbonMomentPinned("PursuitPayload/carbon-moment-cameras.bun",12792,"b92b0cec75cfbb0ee946e49aa10f0c7c25a106cc55fa9abde954cfed23156f0f");
 auto markers=carbonBreakerFamilies?CarbonMomentPinned("PursuitPayload/carbon-families-markers.bin",carbonFamilyMarkerBytes,carbonFamilyMarkerSha):CarbonMomentPinned("PursuitPayload/carbon-moment-markers.bin",172,"453e193e8ac6611cfd33c2f746244165c2d69e9865ec31ad6b4fde8d61801824");
 if(!cameras||!markers){if(cameras)VirtualFree(cameras,0,MEM_RELEASE);if(markers)VirtualFree(markers,0,MEM_RELEASE);Log("CARBON_PB_REFUSED converted moment identity");return;}
 carbonMomentMarkers.resize(carbonBreakerFamilies?carbonFamilyMarkerCount:3);
 memcpy(carbonMomentMarkers.data(),markers+16,carbonMomentMarkers.size()*sizeof(CarbonMomentMarker));VirtualFree(markers,0,MEM_RELEASE);
 // Native lists retain track addresses in this writable allocation.
 carbonMomentCameraPack=cameras;
 reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4787C0)(reinterpret_cast<void*>(0x8EE320),cameras);
 for(unsigned scene:scenes)if(reinterpret_cast<int(__thiscall*)(void*,unsigned)>(0x46E710)(reinterpret_cast<void*>(0x8EE320),scene)!=1){Log("CARBON_PB_REFUSED converted moment registration scene=%08X",scene);return;}
 carbonBreakerMoments=true;Log("CARBON_PB_MOMENTS_ACTIVE scenes=%u markers=%u source_coordinates_translated=1",unsigned(scenes.size()),unsigned(carbonMomentMarkers.size()));
}
static void* CarbonMomentClosest(unsigned scene,const void* position,int* id,float* distance,unsigned short angle){
 if(!carbonBreakerMoments||!position)return nullptr;
 auto p=static_cast<const Vec3*>(position);BYTE* best=nullptr;float closest=0;unsigned index=0;
 for(auto& marker:carbonMomentMarkers){
  if(marker.scene!=scene)continue;
  const auto point=reinterpret_cast<const Vec3*>(marker.record+16);auto heading=*reinterpret_cast<unsigned short*>(marker.record+32);
  unsigned short delta=heading-angle;if(delta>32768)delta=angle-heading;
  float dx=point->x-p->x,dy=point->y-p->y,dz=point->z-p->z;
  float d=std::sqrt(dx*dx+dy*dy+dz*dz)+delta*(25.f/65536.f);
  if(!best||d<closest){best=marker.record;closest=d;if(id)*id=int(index);if(distance)*distance=d;}++index;
 }
 return best;
}
static void TraceCarbonMoment(){
 if(!carbonBreakerMoments||!worldLogEnabled)return;
 static DWORD previous=0;DWORD now=GetTickCount();if(now-previous<200)return;previous=now;
 static unsigned tracked=0;auto nis=*reinterpret_cast<void**>(0x9885C8);if(!nis){if(tracked){Log("CARBON_PB_CINEMATIC_RETURN_ACTIVE scene=%08X native_nis_released=1",tracked);tracked=0;}return;}
 auto vt=*reinterpret_cast<void***>(nis);
 auto scene=reinterpret_cast<void*(__thiscall*)(void*)>(vt[18])(nis);
 if(!scene)return;auto st=*reinterpret_cast<void***>(scene);
 if(st[0]!=reinterpret_cast<void*>(0x6EF330))return;
 unsigned hash=reinterpret_cast<unsigned(__thiscall*)(void*)>(st[0])(scene);
 if(!CarbonMomentScene(hash))return;
 tracked=hash;
 const float* transform=reinterpret_cast<const float*(__thiscall*)(void*)>(st[12])(scene);
 Log("CARBON_PB_CAMERA_TRANSFORM_ACTIVE scene=%08X camera=%.3f,%.3f,%.3f row0=%.5f,%.5f,%.5f row1=%.5f,%.5f,%.5f row2=%.5f,%.5f,%.5f origin=%.3f,%.3f,%.3f",hash,cameraPosition.x,cameraPosition.y,cameraPosition.z,transform[0],transform[1],transform[2],transform[4],transform[5],transform[6],transform[8],transform[9],transform[10],transform[12],transform[13],transform[14]);
 Log("CARBON_PB_CINEMATIC_STATE_ACTIVE scene=%08X playing=%u camera=%u elapsed=%.3f length=%.3f",hash,unsigned(reinterpret_cast<bool(__thiscall*)(void*)>(vt[15])(nis)),unsigned(reinterpret_cast<bool(__thiscall*)(void*)>(st[2])(scene)),reinterpret_cast<float(__thiscall*)(void*)>(st[10])(scene),reinterpret_cast<float(__thiscall*)(void*)>(st[9])(scene));
}
