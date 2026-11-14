#pragma once
// MW's own resource-file path owns TPK upload and post-load texture fixups.
// Helpers are exact byte extractions of approved bundle assets, verified here.
static std::vector<std::string> sourceNativeResourceNames;
static std::vector<void*> sourceNativeResources;
static int (__cdecl* sourceOriginalGlobalChunks)();
static void* (__cdecl* sourceOriginalTextureInfo)(unsigned,int,int);
static std::map<unsigned,unsigned> sourceMapAliases;
static void* __cdecl SourceTextureInfo(unsigned key,int defaultTexture,int unloaded){auto found=sourceMapAliases.find(key);if(loaderActive&&found!=sourceMapAliases.end())key=found->second;return sourceOriginalTextureInfo(key,defaultTexture,unloaded);}
static bool sourceTextureInstalled=false;
static bool ActivateSourceTextureAliases(){
 if(sourceTextureInstalled)return true;
 auto texture=reinterpret_cast<BYTE*>(0x503400);const BYTE guard[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14};
 // Event Loader validates this entry at DB construction. Chain its existing
 // detour only after publication, preserving its original activation checks.
 bool supported=!memcmp(texture,guard,sizeof(guard));
 if(!supported&&texture[0]==0xe9){auto destination=texture+5+*reinterpret_cast<int*>(texture+1);supported=SourceCompanionCaller(destination);}
 if(!supported||MH_CreateHook(texture,SourceTextureInfo,reinterpret_cast<void**>(&sourceOriginalTextureInfo))!=MH_OK)return false;
 if(MH_EnableHook(texture)!=MH_OK){MH_RemoveHook(texture);return false;}
 sourceTextureInstalled=true;Log("SOURCE_MAP_ALIASES_ACTIVE after_database_publication=1 companion_guard_preserved=1");return true;
}
static void* __cdecl SourceDatabasePublished(){
 auto result=reinterpret_cast<void*(__cdecl*)()>(0x603a40)();
 if(!ActivateSourceTextureAliases()){++v2Failures;W2V2Status.error=1;Log("SOURCE_MAP_ALIASES_REFUSED post_publication_hook");}
 return result;
}
static int __cdecl SourceGlobalChunks(){
 int result=sourceOriginalGlobalChunks();
 if(!sourceNativeResources.empty())return result;
 for(const auto& name:sourceNativeResourceNames){auto resource=reinterpret_cast<void*(__cdecl*)(const char*,int,int,int,int)>(0x65fd30)(name.c_str(),1,0,0,0);if(!resource){++v2Failures;W2V2Status.error=1;Log("SOURCE_NATIVE_TPK_REFUSED resource");break;}sourceNativeResources.push_back(resource);reinterpret_cast<int(__thiscall*)(void*,int,int)>(0x6616f0)(resource,0,0);Log("SOURCE_NATIVE_TPK_QUEUED name=%s",name.c_str());}
 return result;
}
static bool InstallSourceNativeResources(){try{
 using namespace w2v2disk;const BYTE create[]={0x6a,0xff,0x68,0x5b,0xa1,0x87,0};const BYTE begin[]={0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1};const BYTE global[]={0x83,0xec,0x08,0x56,0x57,0x68,0xb0,0x90,0x8a,0};need(!memcmp(reinterpret_cast<void*>(0x65fd30),create,sizeof(create))&&!memcmp(reinterpret_cast<void*>(0x6616f0),begin,sizeof(begin))&&!memcmp(reinterpret_cast<void*>(0x664780),global,sizeof(global)),"MW resource loader byte guards");
#ifdef W2C_PUBLIC_BUILD
 const std::string nativeDir=sourcePublicGeneration+"Native\\";
 const std::array<std::pair<const char*,const char*>,1> specs{{{"native/original-map-textures","original-map-textures.bin"}}};
#else
 const std::string nativeDir="W2CMWv2\\Native\\";
 const std::array<std::pair<const char*,const char*>,4> specs{{{"native/prop-textures","prop-textures.bin"},{"native/ambient-textures","ambient-textures.bin"},{"native/original-map-textures","original-map-textures.bin"},{"native/utility-textures","utility-textures.bin"}}};
#endif
 for(const auto& spec:specs){auto e=v2Store.find(61,spec.first);auto relative=nativeDir+spec.second;File file(std::filesystem::path(root)/relative);need(file.size==e.bytes&&file.fullHash()==e.sha,"native TPK helper differs from approved bundle");sourceNativeResourceNames.push_back(std::move(relative));}
#ifdef W2C_PUBLIC_BUILD
 // Old local previews had neither bank. Never activate a half-installed pair.
 const auto& bundle=v2Store.bundle();auto tolls=bundle.find(61,"native/mw-tolls"),tollTextures=bundle.find(61,"native/mw-toll-textures");
 auto utility=bundle.find(61,"native/utility-models"),utilityTextures=bundle.find(61,"native/utility-textures");
  need(bool(utility)==bool(utilityTextures),"incomplete public MW roadblock resources");
 if(utility){
  auto relative=nativeDir+"utility-textures.bin";
  File tpk(std::filesystem::path(root)/relative);
  need(tpk.size==utilityTextures->bytes&&tpk.size<=2*MiB&&tpk.fullHash()==utilityTextures->sha,
        "MW roadblock TPK differs from approved bundle");
  sourceNativeResourceNames.push_back(std::move(relative));
 }
 if(sourceCarbonPursuit){
  auto entry=v2Store.find(61,"native/prop-textures");auto relative=nativeDir+"prop-textures.bin";
  File tpk(std::filesystem::path(root)/relative);
  need(tpk.size==entry.bytes&&tpk.size<=64*MiB&&tpk.fullHash()==entry.sha,"pursuit TPK differs from approved bundle");
  sourceNativeResourceNames.push_back(std::move(relative));
 }
 if(auto ambient=bundle.find(61,"native/ambient-textures")){
  need(sourceCarbonPursuit&&bundle.find(51,"environment/emitters"),"ambient textures require the verified Carbon overlay");
  auto relative=nativeDir+"ambient-textures.bin";File tpk(std::filesystem::path(root)/relative);
  need(tpk.size==ambient->bytes&&tpk.size<=16*MiB&&tpk.fullHash()==ambient->sha,"ambient TPK differs from approved bundle");
  sourceNativeResourceNames.push_back(std::move(relative));
 }
 need(bool(tolls)==bool(tollTextures),"incomplete public MW toll pair");
 if(tolls){auto relative=nativeDir+"mw-toll-textures.bin";File tpk(std::filesystem::path(root)/relative);
  need(tpk.size==tollTextures->bytes&&tpk.fullHash()==tollTextures->sha,"MW toll TPK differs from approved bundle");
  source_mw_tolls::CheckTextures(tpk);source_mw_tolls::Parse(bundle.read(*tolls));sourceNativeResourceNames.push_back(std::move(relative));}
 auto fallback=bundle.find(61,"native/mw-prop-fallback");
 auto fallbackTextures=bundle.find(61,"native/mw-prop-fallback-textures");
 need(bool(fallback)==bool(fallbackTextures),"incomplete public MW prop fallback pair");
 if(fallback){
  auto relative=nativeDir+"mw-prop-fallback-textures.bin";
  File tpk(std::filesystem::path(root)/relative);
  need(tpk.size==fallbackTextures->bytes&&tpk.size<=16*MiB&&tpk.fullHash()==fallbackTextures->sha,
       "MW prop fallback TPK differs from approved bundle");
  source_mw_prop_fallback::Parse(bundle.read(*fallback));
  sourceNativeResourceNames.push_back(std::move(relative));
 }
#else
 if(previous_barrier_appearance::Load())sourceNativeResourceNames.emplace_back(previous_barrier_appearance::relative);
 if(source_mw_tolls::Load())sourceNativeResourceNames.emplace_back(source_mw_tolls::texturePath);
#endif
 auto aliases=parse(SourceBytes(61,"native/original-map-aliases"),65536);need(aliases.is_object()&&aliases.size()<=1024,"source map alias table cap");for(auto it=aliases.begin();it!=aliases.end();++it)sourceMapAliases.emplace(unsigned(std::stoul(it.key(),nullptr,16)),unsigned(std::stoul(str(it.value(),8),nullptr,16)));
 auto site=reinterpret_cast<BYTE*>(0x666127);const BYTE publicationGuard[]={0xe8,0x14,0xd9,0xf9,0xff,0x8b,0x0d,0x0c,0xe0,0x91,0};need(!memcmp(site,publicationGuard,sizeof(publicationGuard)),"MW database publication call site");
 DWORD protection=0;need(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)!=0,"source publication call protection");site[0]=0xe8;*reinterpret_cast<unsigned*>(site+1)=unsigned(SourceDatabasePublished)-unsigned(site+5);DWORD ignored=0;VirtualProtect(site,5,protection,&ignored);FlushInstructionCache(GetCurrentProcess(),site,5);
 auto target=reinterpret_cast<void*>(0x664780);need(MH_CreateHook(target,SourceGlobalChunks,reinterpret_cast<void**>(&sourceOriginalGlobalChunks))==MH_OK,"source resource hook create");if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);throw std::runtime_error("source resource hook enable");}return true;
 }catch(const std::exception& error){sourceNativeResourceNames.clear();Log("SOURCE_NATIVE_RESOURCES_REFUSED reason=%s",error.what());return false;}}
