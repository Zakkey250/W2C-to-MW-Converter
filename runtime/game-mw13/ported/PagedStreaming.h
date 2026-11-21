#pragma once
#include "../../format/GeometryAddon.h"
#include "StreamingUploads.h"
#ifdef W2C_PUBLIC_BUILD
#include "TunnelAddonLights.h"
#endif
static std::unique_ptr<PageCpu> v2Uploading;
static std::shared_ptr<PageGpu> v2Partial;
// Render-thread index, in the same section order as the source map. The
// std::map nodes in sections stay in place after the world is constructed.
static std::map<unsigned,Section*> v2ResidentSections;
static unsigned v2NextBlock=0;
struct ResidentTerrainPage {
 w2c_resident::View view;
 w2v2disk::B table;
};

static ResidentTerrainPage V2OpenResidentTerrain(unsigned id,unsigned clusters,std::string_view sha){
 try{
  auto view=w2c_resident::reader().map(v2Store.buildId,w2c_resident::kTerrain,id,sha);
  const auto bytes=view.span();
  if(bytes.size()<256||memcmp(bytes.data(),"W2GPAGE2",8)!=0)return {};
  w2v2disk::B table(bytes.begin(),bytes.begin()+256);
  const auto tableBytes=w2v2disk::r64(table,208);
  if(w2v2disk::r32(table,8)!=1||w2v2disk::r32(table,12)!=256||
     w2v2disk::r32(table,16)!=id||w2v2disk::r32(table,20)!=clusters||
     w2v2disk::r64(table,56)!=256||tableBytes<256+std::uint64_t(clusters)*128||
     tableBytes>bytes.size()||tableBytes>16ull*1048576)return {};
  table.assign(bytes.begin(),bytes.begin()+static_cast<std::size_t>(tableBytes));
  std::uint64_t decoded=0;
  for(unsigned i=0;i<clusters;++i){
   const auto at=256+std::size_t(i)*128;
   const auto block=w2v2disk::r64(table,at+16);
   const auto vertices=std::uint64_t(w2v2disk::r32(table,at+24));
   const auto indices=std::uint64_t(w2v2disk::r32(table,at+28));
   const auto shadows=std::uint64_t(w2v2disk::r32(table,at+32));
   if(!vertices||block!=vertices*40+(indices+shadows)*2||block>1048576)return {};
   decoded+=block;
  }
  if(decoded!=bytes.size()-tableBytes)return {};
  // The provider verifies SHA, geometry tables, and decoded checksums before publishing.
  return {std::move(view),std::move(table)};
 }catch(const std::exception&){return {};}
}

static bool V2ReadPage(unsigned id,w2v2::Epoch epoch){
 const auto& entry=v2Store.page(id);
 const w2geometry::Page* replacement=nullptr;
#ifdef W2C_PUBLIC_BUILD
 if(const auto it=worldGeometryAddon.find(id);it!=worldGeometryAddon.end())replacement=&it->second;
#endif
 const unsigned clusters=replacement?replacement->summary.clusters:entry.count;
 const auto storedBytes=replacement?replacement->storedBytes:entry.bytes;
 w2v2::Lease input(v2ReadBudget,storedBytes+clusters*128ull+256);
 if(!input){++v2BudgetWaits;return false;}
 try{
  static const bool residentEnabled=[](){
   char value[8]{};
   GetPrivateProfileStringA("Streaming","ResidentGeometryViews","1",value,sizeof(value),Path("W2CMWv2.ini").c_str());
   return !strcmp(value,"1");
  }();
  bool mutablePage=replacement!=nullptr;
#ifdef W2C_PUBLIC_BUILD
  mutablePage=mutablePage||worldClosureAddon.draws.count(id)!=0;
#endif
  auto resident=residentEnabled&&!mutablePage?V2OpenResidentTerrain(id,clusters,entry.sha):ResidentTerrainPage{};
  const bool useResident=bool(resident.view);
  const auto residentBytes=resident.view.span();

  // First inspect bounded chunk declarations. Reserve all decode and table
  // scratch before allocating raw block output. Never map a whole pack.
  // Header and cluster declarations share the same verified source block.
  // Probe them together instead of reading/hashing that block twice.
  auto replacementBytes=replacement?replacement->Read():w2v2disk::B{};
  auto originalPrefix=replacement||useResident?w2v2disk::B{}:v2Store.probe(entry,0,256+std::uint64_t(clusters)*128,16ull*1048576);
  const auto& prefix=useResident?resident.table:replacement?replacementBytes:originalPrefix;
  std::uint64_t decode=0;
  for(unsigned i=0;i<clusters;++i){auto n=w2v2disk::r64(prefix,256+std::size_t(i)*128+16);w2v2disk::need(n<=1048576,"pre-decode cluster bound");decode=w2v2disk::add(decode,n);}
  auto tableBytes=w2v2disk::r64(prefix,208);w2v2disk::need(tableBytes<=16ull*1048576,"pre-decode table bound");
  w2v2::Lease stage(v2StageBudget,w2v2disk::add(decode,tableBytes*2+2*1048576));if(!stage){++v2BudgetWaits;return false;}
  auto original=replacement||useResident?w2v2disk::B{}:v2Store.read(entry);
  const auto& packed=useResident?resident.table:replacement?replacementBytes:original;
  w2v2disk::PageSummary summary{};
  if(useResident){
   summary.section=id;
   summary.clusters=clusters;
   summary.draws=w2v2disk::r32(packed,32);
  }else summary=replacement?replacement->summary:w2v2disk::validateGeometryPage(packed);
  w2v2disk::need(summary.section==id&&summary.clusters==clusters,"section page identity");
  auto result=std::make_unique<PageCpu>();result->id=id;result->epoch=epoch;result->stage=std::move(stage);
  if(useResident)result->residentView.emplace(std::move(resident.view));
  auto copy=[&](auto& vector,std::uint64_t offset,unsigned count){
   using T=typename std::decay_t<decltype(vector)>::value_type;
   const auto bytes=std::uint64_t(count)*sizeof(T);
   w2v2disk::need(offset<=packed.size()&&bytes<=packed.size()-offset,"page table extent");
   vector.resize(count);
   if(count)memcpy(vector.data(),packed.data()+offset,static_cast<std::size_t>(bytes));
  };
  copy(result->draws,w2v2disk::r64(packed,64),summary.draws);copy(result->spans,w2v2disk::r64(packed,72),w2v2disk::r32(packed,40));copy(result->shadowSpans,w2v2disk::r64(packed,80),w2v2disk::r32(packed,44));copy(result->order[0],w2v2disk::r64(packed,88),w2v2disk::r32(packed,48));copy(result->order[1],w2v2disk::r64(packed,96),w2v2disk::r32(packed,52));
  result->textureKeys=V2PageTextureKeys(result->draws);
  auto rawOffset=static_cast<std::size_t>(tableBytes);
  for(unsigned i=0;i<summary.clusters;++i){
   const auto at=256+std::size_t(i)*128;
   PageBlock block{w2v2disk::r32(packed,at+24),w2v2disk::r32(packed,at+28),w2v2disk::r32(packed,at+32),{}};
   if(useResident){
    const auto size=static_cast<std::size_t>(w2v2disk::r64(packed,at+16));
    block.resident=residentBytes.subspan(rawOffset,size);
    rawOffset+=size;
   }else block.bytes=w2v2disk::decodePageCluster(packed,i);
   result->blocks.push_back(std::move(block));
  }
#ifdef W2C_PUBLIC_BUILD
  ApplyClosureDraws(*result);
  for(auto& draw:result->draws){
   ApplyPublicDayCeilingLights(draw.minimum,draw.maximum,draw.lights.day);
   if(replacement&&!tunnelAddonLights.empty())
    ApplyPublicDayCeilingLights(draw.minimum,draw.maximum,draw.lights.night);
  }
#endif
  result->preparationStage=w2v2::Lease(v2StageBudget,std::uint64_t(result->draws.size())*(sizeof(Mesh)+sizeof(std::pair<unsigned,unsigned>)));
  if(result->preparationStage){
   result->packetMemory=w2v2::Lease(packetMemoryBudget,PacketBytes(result->order[0].size(),result->order[1].size()));
   if(result->packetMemory)PreparePageDraws(result->prepared,result->draws,result->spans,result->order);
   else result->preparationStage.reset();
  }
  if(worldLogEnabled&&residentEnabled){
   static std::atomic<bool> recordedResident=false,recordedFallback=false;
   auto& recorded=useResident?recordedResident:recordedFallback;
   if((useResident||!mutablePage)&&!recorded.exchange(true,std::memory_order_relaxed))
    Log(useResident?"W2C_RESIDENT_GEOMETRY_ACTIVE section=%u":"W2C_RESIDENT_GEOMETRY_FALLBACK section=%u",id);
  }
  std::lock_guard lock(ioMutex);if(epoch!=V2Epoch()){++v2StaleDrops;return true;}v2PageReady.push_back(std::move(result));return true;
 }catch(const std::exception& error){++v2Failures;Log("V2_READ_REFUSED section=%u reason=%s",id,error.what());return true;}
}
static void V2Cancel(){
 V2CancelTextureUpload();++v2WorldEpoch;{std::lock_guard lock(ioMutex);for(auto id:pending)sections[id].queued=false;pending.clear();texturePending.clear();textureReady.clear();textureRequested.clear();propReadPending.clear();propReadReady.clear();v2PageReady.clear();v2Uploading.reset();v2Partial.reset();v2NextBlock=0;for(auto& [id,s]:sections)if(!s.vb)s.queued=false;}V2CancelPropReads();ioSignal.notify_all();
}
static void V2Retire(unsigned id,Section& s){if(!s.page)return;v2ResidentSections.erase(id);resident-=s.bytes;s.page.reset();s.vb=nullptr;s.ib=nullptr;s.bytes=0;s.meshes.clear();s.queued=false;}
static void V2Select(){
 DWORD now=GetTickCount();if(now-lastSelect<100)return;lastSelect=now;
 // Travel must not leave a distant queue ahead of the new local coverage.
 static bool focusSeen=false;static Vec3 previousFocus{};
 if(focusSeen){auto delta=Sub(cameraPosition,previousFocus);if(Dot(delta,delta)>800.f*800.f)V2Cancel();}
 previousFocus=cameraPosition;focusSeen=true;
 if(!zones.empty()){
  const float x=cameraPosition.x+6016,y=cameraPosition.y-546;
  unsigned selected=~0u;
  for(unsigned n=0;n<zones.size();++n){const auto& z=zones[n];if(x>=z.low.x&&x<=z.high.x&&y>=z.low.y&&y<=z.high.y&&Inside(z,x,y)){selected=n;break;}}
  if(selected==~0u){
   float best=FLT_MAX;
   for(unsigned n=0;n<zones.size();++n){const auto& z=zones[n];const float bx=std::max({z.low.x-x,0.f,x-z.high.x}),by=std::max({z.low.y-y,0.f,y-z.high.y});
    if(bx*bx+by*by>best)continue;
    for(std::size_t i=0;i<z.boundary.size();++i){const auto a=z.boundary[i],b=z.boundary[(i+1)%z.boundary.size()];const float dx=b.x-a.x,dy=b.y-a.y,l=dx*dx+dy*dy,t=l?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/l,0.f,1.f):0;
     const float ex=x-a.x-t*dx,ey=y-a.y-t*dy,distance=ex*ex+ey*ey;if(distance<best){best=distance;selected=n;}
    }
   }
  }
  zoneIndex=selected;
 }
 std::vector<std::tuple<unsigned,float,unsigned>> wanted;
 SelectTerrainForwardZones();
 const bool drivingDx12=!initialWorldHold&&terrainMainFrustum&&LargerDx12Uploads();
 DWORD retentionMs=5000;
 if(drivingDx12){
  MEMORYSTATUSEX memory{sizeof(memory)};
  const bool headroom=GlobalMemoryStatusEx(&memory)&&memory.ullAvailVirtual>=768ull*1048576&&
   v2GeometryBudget.limit()-v2GeometryBudget.used()>=64ull*1048576&&
   v2TextureBudget.limit()-v2TextureBudget.used()>=96ull*1048576;
  retentionMs=headroom?12000:1000;
 }
 const bool measure=worldLogEnabled&&worldRenderTelemetry&&terrainMainFrustum.has_value();
 unsigned visible=0,missing=0;
 std::set<unsigned> selected;
 for(auto& [id,s]:sections){
  const auto distance=Distance(s.header,cameraPosition);
  const bool forward=terrainMainFrustum&&TerrainForwardVisible(id,s.header,terrainMainEye,*terrainMainFrustum,terrainPrefetchRange);
  const bool nearby=distance<900.f*900.f;
  if(measure&&TerrainSectionVisible(id,s.header,terrainMainEye,*terrainMainFrustum,true)&&
     terrainMainFrustum->Visible(s.header.minimum,s.header.maximum)&&!terrainPanoramaSections.count(id)){
   ++visible;if(!s.vb)++missing;
  }
  if(nearby||forward||id==26000){
   selected.insert(id);
   if(s.vb)s.used=now;
   else if(!s.queued||drivingDx12){
    unsigned priority=0;
    if(drivingDx12&&id!=26000&&distance>100.f*100.f){
     const bool onScreen=terrainMainFrustum->Visible(s.header.minimum,s.header.maximum)&&
      TerrainSectionVisible(id,s.header,terrainMainEye,*terrainMainFrustum,true);
     priority=onScreen?0u:forward?1u:2u;
    }
    wanted.emplace_back(priority,distance+(!drivingDx12&&forward&&!nearby?900.f*900.f:0.f),id);
   }
  }else if(s.vb&&now-s.used>retentionMs)V2Retire(id,s);
 }
 if(measure){
  auto& m=W2CStreamMetricsV1;m.enabled=1;m.boost=LargerDx12Uploads();++m.scans;
  m.visible=visible;m.missing=missing;m.visibleSamples+=visible;m.missingSamples+=missing;
 }
 // Release non-visible old pages before budget exhaustion can block publication
 // of a nearby page. Preserve both the local coverage and the forward frustum.
 if(v2GeometryBudget.limit()-v2GeometryBudget.used()<32ull*1048576){
  for(auto& [id,s]:sections)
   if(s.page&&!selected.count(id)&&now-s.used>1000)V2Retire(id,s);
 }

 // A decoded page cannot publish until its textures are ready. Reserve
 // request capacity for those dependencies even when completed terrain is full.
 std::sort(wanted.begin(),wanted.end());{std::lock_guard lock(ioMutex);
 for(auto it=pending.begin();it!=pending.end();){
  // Re-rank requests that the worker has not taken yet after a camera turn.
  // Active, decoded and partially uploaded pages retain their queued flag.
  if(!drivingDx12&&selected.count(it->id)){++it;continue;}
  sections.at(it->id).queued=false;it=pending.erase(it);
 }
 for(const auto& entry:wanted){if(pending.size()>=16||!V2RequestRoom())break;const auto id=std::get<2>(entry);if(sections[id].queued)continue;sections[id].queued=true;pending.push_back(id);}}ioSignal.notify_all();
}
static bool V2UploadBlock(PageBlock& source,PageGpuBlock& out){
 if(!V2Owner()||v2Lost)return false;
 const auto bytes=source.data();
 if(bytes.size()!=std::uint64_t(source.vertices)*40+(std::uint64_t(source.indices)+source.shadows)*2)return false;
 w2v2::Lease gpu(v2GeometryBudget,bytes.size());if(!gpu){++v2BudgetWaits;return false;}
 void* memory=nullptr;auto fail=[&](){if(out.vb){out.vb->Release();out.vb=nullptr;}if(out.ib){out.ib->Release();out.ib=nullptr;}if(out.shadow){out.shadow->Release();out.shadow=nullptr;}return false;};
 if(FAILED(device->CreateVertexBuffer(source.vertices*40,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&out.vb,nullptr))||FAILED(device->CreateIndexBuffer(source.indices*2,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&out.ib,nullptr)))return fail();
 if(FAILED(out.vb->Lock(0,0,&memory,0)))return fail();memcpy(memory,bytes.data(),source.vertices*40);out.vb->Unlock();
 if(FAILED(out.ib->Lock(0,0,&memory,0)))return fail();memcpy(memory,bytes.data()+source.vertices*40,source.indices*2);out.ib->Unlock();
 if(source.shadows){if(FAILED(device->CreateIndexBuffer(source.shadows*2,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&out.shadow,nullptr))||FAILED(out.shadow->Lock(0,0,&memory,0)))return fail();memcpy(memory,bytes.data()+source.vertices*40+source.indices*2,source.shadows*2);out.shadow->Unlock();}
 out.vertices=source.vertices;out.indices=source.indices;out.shadows=source.shadows;out.bytes=std::move(gpu);return true;
}
static bool V2PumpTerrain(bool loading){
 if(!V2Owner()||v2Lost)return false;
 if(!v2Uploading){std::lock_guard lock(ioMutex);if(v2PageReady.empty())return false;v2Uploading=std::move(v2PageReady.front());v2PageReady.pop_front();ioSignal.notify_all();}
 auto& cpu=*v2Uploading;if(cpu.epoch!=V2Epoch()){v2Uploading.reset();v2Partial.reset();++v2StaleDrops;return true;}
 if(!v2Partial){const auto visibilityBytes=terrainFadeMs?sizeof(VisibilityFade):0u;auto bytes=std::uint64_t(cpu.draws.size())*(sizeof(PageDraw)+sizeof(Mesh)+8+visibilityBytes)+(cpu.spans.size()+cpu.shadowSpans.size())*sizeof(PageSpan)+(cpu.order[0].size()+cpu.order[1].size()+cpu.textureKeys.capacity())*4+cpu.blocks.size()*sizeof(PageGpuBlock);w2v2::Lease metadata(v2MetadataBudget,bytes);if(!metadata){++v2BudgetWaits;return false;}v2Partial=std::make_shared<PageGpu>();v2Partial->metadata=std::move(metadata);v2Partial->epoch=cpu.epoch;v2Partial->blocks.reserve(cpu.blocks.size());v2Partial->pins.acquire(std::move(cpu.textureKeys));v2NextBlock=0;}
 // Dispatch texture reads while the geometry uploads; publication remains
 // atomic and still requires every verified dependency to be resident.
 const bool boosted=!loading&&LargerDx12Uploads();
 const bool texturesReady=V2PageTexturesReady(*v2Partial);
 if(boosted&&!texturesReady&&!cpu.prioritizedTextures){
  const auto& keys=v2Partial->pins.keys; // V2PageTextureKeys produces sorted, unique keys.
  std::lock_guard lock(ioMutex);
  auto needed=[&](unsigned hash){return std::binary_search(keys.begin(),keys.end(),hash);};
  std::stable_partition(texturePending.begin(),texturePending.end(),[&](const V2Request& r){return needed(r.id);});
  std::stable_partition(textureReady.begin(),textureReady.end(),[&](const TextureData& t){return needed(t.hash);});
  cpu.prioritizedTextures=true;
 }
 const auto maxBytes=boosted?1536ull*1024:1048576ull;
 UploadSlice slice(geometryWorkMs,boosted);
 while(v2NextBlock<cpu.blocks.size()){
  auto& block=cpu.blocks[v2NextBlock];
  const auto bytes=block.data().size();
  if(v2UploadBytes+bytes>maxBytes||(!loading&&(boosted?slice.expired(1.0):Clock()-v2UploadStarted>=1.0))){
   if(worldLogEnabled&&worldRenderTelemetry)++W2CStreamMetricsV1.geometryLimit;
   return false;
  }
  PageGpuBlock gpu;if(!V2UploadBlock(block,gpu))return false;v2UploadBytes+=bytes;v2Partial->blocks.push_back(std::move(gpu));w2v2disk::B{}.swap(block.bytes);++v2NextBlock;
 }
 auto& s=sections[cpu.id];if(v2Partial->draws.empty()){
 v2Partial->draws=std::move(cpu.draws);v2Partial->spans=std::move(cpu.spans);v2Partial->shadowSpans=std::move(cpu.shadowSpans);v2Partial->order=std::move(cpu.order);
 if(terrainFadeMs)v2Partial->visibility.resize(v2Partial->draws.size());
 if(cpu.preparationStage){
  v2Partial->drawSpans=std::move(cpu.prepared.spans);
  v2Partial->bounds=std::move(cpu.prepared.bounds);
  v2Partial->packetMemory=std::move(cpu.packetMemory);
 }else{
  v2Partial->drawSpans.resize(v2Partial->draws.size());
  for(unsigned i=0;i<v2Partial->spans.size();++i){auto& span=v2Partial->spans[i];auto& range=v2Partial->drawSpans[span.source];if(!range.second)range.first=i;++range.second;}
 }
 cpu.blocks.clear();cpu.residentView.reset();cpu.stage.reset();
 }
 if(!texturesReady){
  if(worldLogEnabled&&worldRenderTelemetry)++W2CStreamMetricsV1.waitedForTexture;
  return false;
 }
 if(worldLogEnabled&&worldRenderTelemetry)++W2CStreamMetricsV1.published;
 if(cpu.preparationStage)s.meshes=std::move(cpu.prepared.meshes);
 else{s.meshes.clear();s.meshes.reserve(v2Partial->draws.size());for(auto& d:v2Partial->draws)s.meshes.push_back({d.texture,d.oldFirstVertex,d.oldVertexCount,d.oldFirstIndex,d.indexCount,d.minimum,d.maximum});}
 s.page=std::move(v2Partial);s.vb=s.page->blocks.empty()?nullptr:s.page->blocks[0].vb;s.ib=s.page->blocks.empty()?nullptr:s.page->blocks[0].ib;s.bytes=0;for(auto& b:s.page->blocks)s.bytes+=b.bytes.bytes;resident+=s.bytes;s.used=GetTickCount();s.queued=false;++v2Published;
 if(s.vb)v2ResidentSections[cpu.id]=&s;
 v2Uploading.reset();return true;
}
