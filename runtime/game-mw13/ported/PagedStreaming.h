#pragma once
static std::unique_ptr<PageCpu> v2Uploading;
static std::shared_ptr<PageGpu> v2Partial;
static unsigned v2NextBlock=0;
static bool V2ReadPage(unsigned id,w2v2::Epoch epoch){
 const auto& entry=v2Store.page(id);w2v2::Lease input(v2ReadBudget,entry.bytes+entry.count*128ull+256);
 if(!input){++v2BudgetWaits;return false;}
 try{

  // First inspect bounded chunk declarations. Reserve all decode and table
  // scratch before allocating raw block output. Never map a whole pack.
  // Header and cluster declarations share the same verified source block.
  // Probe them together instead of reading/hashing that block twice.
  auto prefix=v2Store.probe(entry,0,256+std::uint64_t(entry.count)*128,16ull*1048576);std::uint64_t decode=0;
  for(unsigned i=0;i<entry.count;++i){auto n=w2v2disk::r64(prefix,256+std::size_t(i)*128+16);w2v2disk::need(n<=1048576,"pre-decode cluster bound");decode=w2v2disk::add(decode,n);}
  auto tableBytes=w2v2disk::r64(prefix,208);w2v2disk::need(tableBytes<=16ull*1048576,"pre-decode table bound");
  w2v2::Lease stage(v2StageBudget,w2v2disk::add(decode,tableBytes*2+2*1048576));if(!stage){++v2BudgetWaits;return false;}
  auto packed=v2Store.read(entry);auto summary=w2v2disk::validateGeometryPage(packed);w2v2disk::need(summary.section==id&&summary.clusters==entry.count,"section page identity");
  auto result=std::make_unique<PageCpu>();result->id=id;result->epoch=epoch;result->stage=std::move(stage);
  auto copy=[&](auto& vector,std::uint64_t offset,unsigned count){using T=typename std::decay_t<decltype(vector)>::value_type;vector.resize(count);if(count)memcpy(vector.data(),packed.data()+offset,std::size_t(count)*sizeof(T));};
  copy(result->draws,w2v2disk::r64(packed,64),summary.draws);copy(result->spans,w2v2disk::r64(packed,72),w2v2disk::r32(packed,40));copy(result->shadowSpans,w2v2disk::r64(packed,80),w2v2disk::r32(packed,44));copy(result->order[0],w2v2disk::r64(packed,88),w2v2disk::r32(packed,48));copy(result->order[1],w2v2disk::r64(packed,96),w2v2disk::r32(packed,52));
  result->textureKeys=V2PageTextureKeys(result->draws);
  for(unsigned i=0;i<summary.clusters;++i){std::size_t at=256+std::size_t(i)*128;result->blocks.push_back({w2v2disk::r32(packed,at+24),w2v2disk::r32(packed,at+28),w2v2disk::r32(packed,at+32),w2v2disk::decodePageCluster(packed,i)});}
  std::lock_guard lock(ioMutex);if(epoch!=V2Epoch()){++v2StaleDrops;return true;}v2PageReady.push_back(std::move(result));return true;
 }catch(const std::exception& error){++v2Failures;Log("V2_READ_REFUSED section=%u reason=%s",id,error.what());return true;}
}
static void V2Cancel(){
 V2CancelTextureUpload();++v2WorldEpoch;{std::lock_guard lock(ioMutex);for(auto id:pending)sections[id].queued=false;pending.clear();texturePending.clear();textureReady.clear();textureRequested.clear();propReadPending.clear();propReadReady.clear();v2PageReady.clear();v2Uploading.reset();v2Partial.reset();v2NextBlock=0;for(auto& [id,s]:sections)if(!s.vb)s.queued=false;}V2CancelPropReads();ioSignal.notify_all();
}
static void V2Retire(Section& s){if(!s.page)return;resident-=s.bytes;s.page.reset();s.vb=nullptr;s.ib=nullptr;s.bytes=0;s.meshes.clear();s.queued=false;}
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
 std::vector<std::pair<float,unsigned>> wanted;
 for(auto& [id,s]:sections){auto distance=Distance(s.header,cameraPosition);if(distance<900.f*900.f||id==26000){if(s.vb)s.used=now;else if(!s.queued)wanted.emplace_back(distance,id);}else if(s.vb&&distance>1100.f*1100.f&&now-s.used>5000)V2Retire(s);}
 // A decoded page cannot publish until its textures are ready. Reserve
 // request capacity for those dependencies even when completed terrain is full.
 std::sort(wanted.begin(),wanted.end());{std::lock_guard lock(ioMutex);for(auto [distance,id]:wanted){if(pending.size()>=16||!V2RequestRoom())break;sections[id].queued=true;pending.push_back(id);}}ioSignal.notify_all();
}
static bool V2UploadBlock(PageBlock& source,PageGpuBlock& out){
 if(!V2Owner()||v2Lost)return false;w2v2::Lease gpu(v2GeometryBudget,source.bytes.size());if(!gpu){++v2BudgetWaits;return false;}
 void* memory=nullptr;auto fail=[&](){if(out.vb){out.vb->Release();out.vb=nullptr;}if(out.ib){out.ib->Release();out.ib=nullptr;}if(out.shadow){out.shadow->Release();out.shadow=nullptr;}return false;};
 if(FAILED(device->CreateVertexBuffer(source.vertices*40,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&out.vb,nullptr))||FAILED(device->CreateIndexBuffer(source.indices*2,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&out.ib,nullptr)))return fail();
 if(FAILED(out.vb->Lock(0,0,&memory,0)))return fail();memcpy(memory,source.bytes.data(),source.vertices*40);out.vb->Unlock();
 if(FAILED(out.ib->Lock(0,0,&memory,0)))return fail();memcpy(memory,source.bytes.data()+source.vertices*40,source.indices*2);out.ib->Unlock();
 if(source.shadows){if(FAILED(device->CreateIndexBuffer(source.shadows*2,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&out.shadow,nullptr))||FAILED(out.shadow->Lock(0,0,&memory,0)))return fail();memcpy(memory,source.bytes.data()+source.vertices*40+source.indices*2,source.shadows*2);out.shadow->Unlock();}
 out.vertices=source.vertices;out.indices=source.indices;out.shadows=source.shadows;out.bytes=std::move(gpu);return true;
}
static bool V2PumpTerrain(bool loading){
 if(!V2Owner()||v2Lost)return false;
 if(!v2Uploading){std::lock_guard lock(ioMutex);if(v2PageReady.empty())return false;v2Uploading=std::move(v2PageReady.front());v2PageReady.pop_front();ioSignal.notify_all();}
 auto& cpu=*v2Uploading;if(cpu.epoch!=V2Epoch()){v2Uploading.reset();v2Partial.reset();++v2StaleDrops;return true;}
 if(!v2Partial){auto bytes=std::uint64_t(cpu.draws.size())*(sizeof(PageDraw)+sizeof(Mesh)+8)+(cpu.spans.size()+cpu.shadowSpans.size())*sizeof(PageSpan)+(cpu.order[0].size()+cpu.order[1].size()+cpu.textureKeys.capacity())*4+cpu.blocks.size()*sizeof(PageGpuBlock);w2v2::Lease metadata(v2MetadataBudget,bytes);if(!metadata){++v2BudgetWaits;return false;}v2Partial=std::make_shared<PageGpu>();v2Partial->metadata=std::move(metadata);v2Partial->epoch=cpu.epoch;v2Partial->blocks.reserve(cpu.blocks.size());v2Partial->pins.acquire(std::move(cpu.textureKeys));v2NextBlock=0;}
 // Dispatch texture reads while the geometry uploads; publication remains
 // atomic and still requires every verified dependency to be resident.
 const bool texturesReady=V2PageTexturesReady(*v2Partial);
 const auto maxBytes=1048576ull;
 while(v2NextBlock<cpu.blocks.size()){
  auto& block=cpu.blocks[v2NextBlock];if(v2UploadBytes+block.bytes.size()>maxBytes||(!loading&&Clock()-v2UploadStarted>=1.0))return false;
  PageGpuBlock gpu;if(!V2UploadBlock(block,gpu))return false;v2UploadBytes+=block.bytes.size();v2Partial->blocks.push_back(std::move(gpu));w2v2disk::B{}.swap(block.bytes);++v2NextBlock;
 }
 auto& s=sections[cpu.id];if(v2Partial->draws.empty()){
 v2Partial->draws=std::move(cpu.draws);v2Partial->spans=std::move(cpu.spans);v2Partial->shadowSpans=std::move(cpu.shadowSpans);v2Partial->order=std::move(cpu.order);v2Partial->drawSpans.resize(v2Partial->draws.size());
 for(unsigned i=0;i<v2Partial->spans.size();++i){auto& span=v2Partial->spans[i];auto& range=v2Partial->drawSpans[span.source];if(!range.second)range.first=i;++range.second;}cpu.blocks.clear();cpu.stage.reset();
 }
 if(!texturesReady)return false;
 s.meshes.clear();s.meshes.reserve(v2Partial->draws.size());for(auto& d:v2Partial->draws)s.meshes.push_back({d.texture,d.oldFirstVertex,d.oldVertexCount,d.oldFirstIndex,d.indexCount,d.minimum,d.maximum});
 s.page=std::move(v2Partial);s.vb=s.page->blocks.empty()?nullptr:s.page->blocks[0].vb;s.ib=s.page->blocks.empty()?nullptr:s.page->blocks[0].ib;s.bytes=0;for(auto& b:s.page->blocks)s.bytes+=b.bytes.bytes;resident+=s.bytes;s.used=GetTickCount();s.queued=false;++v2Published;v2Uploading.reset();return true;
}
