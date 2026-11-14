#pragma once
#ifdef W2C_PUBLIC_BUILD
#include "PublicDistrictLocalization.h"
// Only the two calls in the native neighborhood HUD are redirected. Native
// region users (including police speech, facilities and saves) stay untouched.
static constexpr unsigned PublicPalmontLabel=0xE2D15000;
static std::array<PalmontArea,22> publicDistricts;
static std::array<PalmontRegionRecord,std::size(publicDistrictNames)> publicDistrictRecords{};
static std::vector<BYTE> publicCityCells;
static int publicCityX=0,publicCityY=0;
static unsigned publicCityWidth=0,publicCityHeight=0,publicCityStep=0;

static bool LoadPublicDistricts(const std::vector<BYTE>& data){
 unsigned header[3]{};size_t at=0;
 const auto take=[&](void* dest,size_t bytes){if(bytes>data.size()-at)return false;memcpy(dest,data.data()+at,bytes);at+=bytes;return true;};
 if(!take(header,sizeof(header))||header[0]!=0x44503257||header[1]!=1||header[2]!=publicDistricts.size())return false;
 std::array<PalmontArea,22> parsed;unsigned total=0;
 for(unsigned i=0;i<parsed.size();++i){
  unsigned row[2]{};if(!take(row,sizeof(row))||row[0]!=i+1||!row[1]||row[1]>128)return false;
  auto& area=parsed[i];area.hash=PublicPalmontLabel+row[0];
  area.minX=area.minY=10000.f;area.maxX=area.maxY=-10000.f;
  for(unsigned j=0;j<row[1];++j){
   float vertices[6]{};if(!take(vertices,sizeof(vertices)))return false;
   PalmontTriangle tri{};
   for(unsigned k=0;k<3;++k){
    float x=vertices[2*k],y=vertices[2*k+1];if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000.f||std::abs(y)>10000.f)return false;
    tri.x[k]=x;tri.y[k]=y;area.minX=std::min(area.minX,x);area.maxX=std::max(area.maxX,x);
    area.minY=std::min(area.minY,y);area.maxY=std::max(area.maxY,y);
   }
   area.triangles.push_back(tri);
  }
  total+=row[1];
 }
 if(at!=data.size()||total!=447)return false;
 publicDistricts=std::move(parsed);return true;
}

static bool LoadPublicCityCoverage(const std::vector<BYTE>& data){
 struct Header {unsigned magic,version;int x,y;unsigned step,width,height;};
 Header h{};
 if(data.size()<sizeof(h))return false;
 memcpy(&h,data.data(),sizeof(h));
 if(h.magic!=0x47483257||h.version!=1||h.step!=16||!h.width||h.width>1024||
    !h.height||h.height>1024||h.x<-10000||h.x>10000||h.y<-10000||h.y>10000||
    data.size()!=sizeof(h)+h.width*h.height||
    std::any_of(data.begin()+sizeof(h),data.end(),[](BYTE value){return value>2;}))return false;
 publicCityCells.assign(data.begin()+sizeof(h),data.end());
 publicCityX=h.x;publicCityY=h.y;publicCityStep=h.step;
 publicCityWidth=h.width;publicCityHeight=h.height;
 return true;
}

static BYTE PublicCityCoverage(float x,float y){
 if(publicCityCells.empty())return 0;
 const float i=(x-publicCityX)/publicCityStep,j=(y-publicCityY)/publicCityStep;
 if(i<0||j<0||i>=publicCityWidth||j>=publicCityHeight)return 0;
 return publicCityCells[unsigned(j)*publicCityWidth+unsigned(i)];
}

static void* __fastcall PublicDistrictQuery(void* self,void*,float* p,int type,void* previous){
 const auto native=reinterpret_cast<void*(__thiscall*)(void*,float*,int,void*)>(0x727480);
 if(type!=11||!p||!std::isfinite(p[0])||!std::isfinite(p[1]))return native(self,p,type,previous);
 for(auto& record:publicDistrictRecords)if(previous==&record){previous=nullptr;break;}
 const float x=p[0],y=p[1];
 const BYTE city=PublicCityCoverage(x,y);
 // A road retained from Rockport takes priority over a nearby Carbon area.
 if(city==1)return native(self,p,type,previous);
 for(const auto& area:publicDistricts){
  if(!area.hash||x<area.minX||x>area.maxX||y<area.minY||y>area.maxY)continue;
  for(const auto& tri:area.triangles)if(PalmontContains(tri,x,y))return &publicDistrictRecords[area.hash-PublicPalmontLabel];
 }
 if(city==2)return &publicDistrictRecords[0];
 return native(self,p,type,previous);
}

static const char* __cdecl PublicDistrictName(unsigned hash){
 unsigned index=hash-PublicPalmontLabel;
 if(index<std::size(publicDistrictNames)){
  const char* locale=nullptr;
  const char* name=PublicLocalizedDistrict(index,locale);
  Log("PUBLIC_PALMONT_HUD district=%u locale=%s canonical=%s",index,locale,publicDistrictNames[index]);
  return name;
 }
 return reinterpret_cast<const char*(__cdecl*)(unsigned)>(0x56BC60)(hash);
}

static void InstallPublicPalmontDistricts(){
 bool detailed=false,coverage=false;
 try{const auto& bundle=v2Store.bundle();
  if(auto entry=bundle.find(51,"environment/world-city-coverage")){
   coverage=entry->bytes<=1024*1024+28&&LoadPublicCityCoverage(bundle.read(*entry));
   if(!coverage)Log("PUBLIC_PALMONT_DISTRICTS_REFUSED invalid city coverage");
  }
  if(auto entry=bundle.find(51,"environment/palmont-districts")){
   detailed=entry->bytes==10916&&LoadPublicDistricts(bundle.read(*entry));
   if(!detailed)Log("PUBLIC_PALMONT_DISTRICTS_REFUSED invalid optional boundaries; using city name");
  }
 }catch(const std::exception& e){Log("PUBLIC_PALMONT_DISTRICTS_REFUSED %s",e.what());}
 for(unsigned i=0;i<publicDistrictRecords.size();++i){
  publicDistrictRecords[i].type=11;publicDistrictRecords[i].hash=PublicPalmontLabel+i;
 }
 // The two guarded x86 calls share one page. Only the native HUD can see the
 // synthetic records, and it reads only their localization hash at +0x30.
 auto query=reinterpret_cast<BYTE*>(0x6EE39D);auto label=reinterpret_cast<BYTE*>(0x6EE3CF);
 const BYTE queryGuard[]={0xE8,0xDE,0x90,0x03,0x00},labelGuard[]={0xE8,0x8C,0xD8,0xE7,0xFF};
 if(memcmp(query,queryGuard,5)||memcmp(label,labelGuard,5)){Log("PUBLIC_PALMONT_DISTRICTS_REFUSED HUD guard");return;}
 DWORD old=0;size_t span=size_t(label+5-query);
 if(!VirtualProtect(query,span,PAGE_EXECUTE_READWRITE,&old))return;
 const DWORD queryRel=DWORD(reinterpret_cast<uintptr_t>(PublicDistrictQuery)-reinterpret_cast<uintptr_t>(query+5));
 const DWORD labelRel=DWORD(reinterpret_cast<uintptr_t>(PublicDistrictName)-reinterpret_cast<uintptr_t>(label+5));
 memcpy(query+1,&queryRel,4);memcpy(label+1,&labelRel,4);
 VirtualProtect(query,span,old,&old);FlushInstructionCache(GetCurrentProcess(),query,span);
 Log("PUBLIC_PALMONT_DISTRICTS_ACTIVE districts=%u world_coverage=%u native_rockport=1",detailed?22:0,unsigned(coverage));
}
#endif
