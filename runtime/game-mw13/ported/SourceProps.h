#pragma once
static bool LoadSourcePropHierarchies(){try{
 using namespace w2v2disk;std::map<unsigned,std::vector<BYTE>> trees;std::map<unsigned,std::string> names;
#ifdef W2C_PUBLIC_BUILD
 if(sourceCarbonPursuit){
#endif
 auto input=SourceInput(61,"native/prop-hierarchies");need(input.get<unsigned>()==0x31485057,"source prop tree magic");auto n=input.get<unsigned>();need(n>0&&n<=4096,"source prop tree count");
 for(unsigned i=0;i<n;++i){auto asset=input.get<unsigned>(),bytes=input.get<unsigned>();need(bytes>=24&&bytes<=520&&(bytes-8)%16==0,"source prop tree size");auto tree=input.take(bytes);need(r32(tree,4)==(bytes-8)/16,"source prop node count");for(unsigned j=0;j<(bytes-8)/16;++j)need(r32(tree,16+j*16)==0&&unsigned(tree[22+j*16])+tree[23+j*16]<=(bytes-8)/16&&(tree[20+j*16]<(bytes-8)/16||tree[20+j*16]==255),"source prop tree links/runtime pointers");need(trees.emplace(asset,std::move(tree)).second,"duplicate source prop tree");}input.end();
 auto params=SourceInput(61,"native/prop-parameters");need(params.get<unsigned>()==0x32505057&&params.get<unsigned>()==n,"source prop parameter header");
 for(unsigned i=0;i<n;++i){auto asset=params.get<unsigned>();char text[64];params.read(text,64);size_t len=0;while(len<64&&text[len])++len;need(len>5&&len<64&&!memcmp(text,"w2v2_",5)&&trees.count(asset),"source private prop parameter identity");for(size_t j=0;j<len;++j)need((text[j]>='a'&&text[j]<='z')||(text[j]>='0'&&text[j]<='9')||text[j]=='_',"source parameter characters");for(size_t j=len;j<64;++j)need(!text[j],"source parameter padding");need(names.emplace(asset,std::string(text,len)).second,"duplicate source parameter");}params.end();
#ifdef W2C_PUBLIC_BUILD
 }
 for(const auto& [asset,tree]:source_mw_prop_fallback::trees)
  need(trees.emplace(asset,tree).second,"MW fallback hierarchy identity collision");
#endif
 for(const auto& [asset,tree]:source_mw_tolls::trees)need(trees.emplace(asset,tree).second,"MW toll/source hierarchy identity collision");
 propHierarchies=std::move(trees);propPursuitParameters=std::move(names);for(const auto& [asset,tree]:propHierarchies)propPursuitAssets.insert(asset);propHierarchyEnabled=true;propPursuitPayload=true;return true;
 }catch(const std::exception& error){Log("SOURCE_PROP_TREES_REFUSED reason=%s",error.what());++v2Failures;return false;}}
static bool LoadSourcePropCatalog(){try{
 using namespace w2v2disk;std::vector<std::unique_ptr<NativeProp>> staged;unsigned nextId=0;
#ifdef W2C_PUBLIC_BUILD
 if(sourceCarbonPursuit){
#endif
 auto input=SourceInput(61,"native/prop-placements");need(input.get<unsigned>()==0x31504d57,"source prop catalog magic");auto n=input.get<unsigned>();need(n>0&&n<=100000&&input.remaining()==U(n)*64,"source prop catalog count/size");
 nextId=n;
#ifdef W2C_PUBLIC_BUILD
 std::array<BYTE,64> cushionTemplate{};
 bool foundCushionTemplate=false;
#endif
 for(unsigned i=0;i<n;++i){
  auto p=std::make_unique<NativeProp>();p->id=i;input.read(p->record,64);
#ifdef W2C_PUBLIC_BUILD
  if(!rockportCushions.placements.empty()&&i==rockportCushions.templateId){
   memcpy(cushionTemplate.data(),p->record,cushionTemplate.size());
   foundCushionTemplate=true;
  }
  if(worldClosureAddon.props.count(i))continue;
#endif
  for(unsigned j=0;j<3;++j){float x;memcpy(&x,p->record+16+j*4,4);need(std::isfinite(x),"source prop position");}
  unsigned asset;memcpy(&asset,p->record+32,4);
  need(propHierarchies.count(asset)&&propPursuitParameters.count(asset),"source prop catalog dependency");
  staged.push_back(std::move(p));
 }input.end();
#ifdef W2C_PUBLIC_BUILD
 if(!rockportCushions.placements.empty()){
  const auto asset=r32(B(cushionTemplate.begin(),cushionTemplate.end()),32);
  const auto tree=propHierarchies.find(asset);
  if(foundCushionTemplate&&sha(B(cushionTemplate.begin(),cushionTemplate.end()))==rockportCushions.templateSha&&
     tree!=propHierarchies.end()&&r32(tree->second,4)>1&&propPursuitParameters.count(asset)){
   auto& replaced=worldClosureAddon.draws[rockportCushions.section];
   for(const auto& [draw,transform]:rockportCushions.placements){
    auto prop=std::make_unique<NativeProp>();
    prop->id=nextId++;
    prop->rockportCushion=true;
    memcpy(prop->record,cushionTemplate.data(),cushionTemplate.size());
    memcpy(prop->record,transform.data(),transform.size()*sizeof(float));
    staged.push_back(std::move(prop));
    replaced.insert(draw);
   }
   if(worldLogEnabled)Log("ROCKPORT_CUSHIONS_ACTIVE first=%u count=%u asset=%08X",
       n,unsigned(rockportCushions.placements.size()),asset);
  }else if(worldLogEnabled)Log("ROCKPORT_CUSHIONS_REFUSED native model or template identity");
 }
 }
#endif
 auto appendPlacement=[&staged,&nextId](const std::array<BYTE,64>& record){
  auto prop=std::make_unique<NativeProp>();prop->id=nextId++;
  memcpy(prop->record,record.data(),record.size());staged.push_back(std::move(prop));
 };
#ifdef W2C_PUBLIC_BUILD
 for(const auto& record:source_mw_prop_fallback::placements)appendPlacement(record);
#endif
 for(const auto& record:source_mw_tolls::placements)appendPlacement(record);
#ifdef W2C_PUBLIC_BUILD
 if(!authoredPropPlacements.empty()){
  const auto bank=SourceBytes(61,"native/prop-placements");
  for(const auto& row:authoredPropPlacements){
   auto prop=std::make_unique<NativeProp>();prop->id=nextId++;prop->authored=true;
   memcpy(prop->record,bank.data()+8+std::size_t(row.templateId)*64,64);memcpy(prop->record,row.transform.data(),32);
   const auto asset=*reinterpret_cast<const unsigned*>(prop->record+32);
   need(propHierarchies.count(asset)&&propPursuitParameters.count(asset),"Authored prop native dependency");staged.push_back(std::move(prop));
  }
  if(worldLogEnabled)Log("AUTHORED_PROPS_ACTIVE count=%u native_collision=1 native_motion=1",unsigned(authoredPropPlacements.size()));
 }
#endif
 nativeProps=std::move(staged);propCatalog=true;w2mw::propPolygonBridge=true;return true;
 }catch(const std::exception& error){Log("SOURCE_PROP_CATALOG_REFUSED reason=%s",error.what());++v2Failures;return false;}}
static bool RegisterSourcePropConvex(){try{
#ifdef W2C_PUBLIC_BUILD
 // The public master already supplies the stock bank. Register private aliases only.
 if(!sourceCarbonPursuit)return !source_mw_tolls::placements.empty();
#endif
 using namespace w2v2disk;auto bytes=SourceBytes(61,"native/prop-convex");need(bytes.size()>=8&&bytes.size()<=4*MiB&&r32(bytes,0)==0x8003b900&&r32(bytes,4)==bytes.size()-8,"source convex envelope");const BYTE guard[]={0x64,0xa1,0,0,0,0,0x6a,0xff};need(!memcmp(reinterpret_cast<void*>(0x687240),guard,sizeof(guard)),"source native convex guard");auto memory=VirtualAlloc(nullptr,bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);need(memory!=nullptr,"source convex allocation");memcpy(memory,bytes.data(),bytes.size());if(!reinterpret_cast<int(__cdecl*)(void*)>(0x687240)(memory)){VirtualFree(memory,0,MEM_RELEASE);throw std::runtime_error("source convex registration");}propCollisionBundle=memory;return true;
 }catch(const std::exception& error){Log("SOURCE_CONVEX_REFUSED reason=%s",error.what());++v2Failures;return false;}}
static bool RegisterSourcePropModels(){try{
 using namespace w2v2disk;const BYTE guard[]={0x8b,0x44,0x24,0x04,0x81,0x38,0,0x40,0x13,0x80};need(!memcmp(reinterpret_cast<void*>(0x5075a0),guard,sizeof(guard)),"source native geometry guard");
 auto registerBank=[](const B& bytes){
  need(bytes.size()>=16&&bytes.size()<=64*MiB&&r32(bytes,0)==0x80134000&&r32(bytes,4)==bytes.size()-8,"source native geometry envelope");
  auto memory=VirtualAlloc(nullptr,bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);need(memory!=nullptr,"source native geometry allocation");memcpy(memory,bytes.data(),bytes.size());
  if(!reinterpret_cast<int(__cdecl*)(void*)>(0x5075a0)(memory)){VirtualFree(memory,0,MEM_RELEASE);throw std::runtime_error("source native geometry registration");}propNativeSolidBundles.push_back(memory);
 };
#ifndef W2C_PUBLIC_BUILD
 for(const char* key:{"native/prop-models","native/utility-models"})registerBank(SourceBytes(61,key));
 Log("SOURCE_NATIVE_UTILITIES_REGISTERED original_spike_model=2A908C13 unchanged_model_bytes=1");
#else
 if(sourceCarbonPursuit)registerBank(SourceBytes(61,"native/prop-models"));
 if(const auto* utility=v2Store.bundle().find(61,"native/utility-models")){
  registerBank(v2Store.bundle().read(*utility));
  Log("SOURCE_NATIVE_UTILITIES_REGISTERED original_spike_model=2A908C13 unchanged_model_bytes=1");
 }
 if(!source_mw_prop_fallback::models.empty())registerBank(source_mw_prop_fallback::models);
#endif
 if(!source_mw_tolls::models.empty())registerBank(source_mw_tolls::models);
 propNativeSolidsReady=true;propNativeSolidsValid=true;return true;
 }catch(const std::exception& error){Log("SOURCE_PROP_GEOMETRY_REFUSED reason=%s",error.what());++v2Failures;return false;}}
