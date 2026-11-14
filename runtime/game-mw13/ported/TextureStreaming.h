#pragma once
#include "../../core/D3DTextureTransfer.h"
struct TextureUpload {
 TextureData data{};w2v2::TextureLayout layout;w2v2::TextureCursor cursor;
 w2v2::Lease gpu;IDirect3DTexture9* texture=nullptr;
 ~TextureUpload(){if(texture)texture->Release();}
};
static std::unique_ptr<TextureUpload> v2TextureUpload;
static w2v2::TextureStrip v2TextureStrip;
static std::uint64_t v2TextureUploadBytes=0;
static double v2TextureUploadStarted=0;
static void V2CancelTextureUpload(){v2TextureUpload.reset();}
static bool V2RetryTextureUpload(const TextureData& data){
 ++v2Failures;
 std::lock_guard lock(ioMutex);
 if(data.epoch==V2Epoch())textureRequested.erase(data.hash);
 return false;
}
static void V2ReleaseDeviceTextures(){
 V2CancelTextureUpload();v2TextureStrip.clear();
 // Remove only our own bound DEFAULT textures before native Reset.
 for(unsigned stage=0;stage<16;++stage){IDirect3DBaseTexture9* bound=nullptr;
  if(SUCCEEDED(device->GetTexture(stage,&bound))&&bound){for(auto& [hash,texture]:textures)if(bound==texture){device->SetTexture(stage,nullptr);break;}bound->Release();}}
 for(auto& [hash,texture]:textures)if(texture)texture->Release();
 textures.clear();textureBytes.clear();textureUsed.clear();v2TextureLeases.clear();textureResident=0;
 {std::lock_guard lock(ioMutex);textureRequested.clear();}
 v2TextureUploadBytes=0;v2TextureUploadStarted=0;
}
static bool V2PumpTextures(bool loading){
 const std::size_t limit=loading?4u*1048576u:256u*1024u;
 const double milliseconds=loading?8.0:0.5;
 if(!v2TextureUploadStarted)v2TextureUploadStarted=Clock();
 unsigned completed=0;
 while(v2TextureUploadBytes<limit&&Clock()-v2TextureUploadStarted<milliseconds&&completed<16){
  if(!v2TextureUpload){
   auto next=std::make_unique<TextureUpload>();
   {std::lock_guard lock(ioMutex);if(textureReady.empty())return true;next->data=std::move(textureReady.front());textureReady.pop_front();}ioSignal.notify_all();
   if(next->data.epoch!=V2Epoch()){++v2StaleDrops;continue;}
   if(!next->layout.parse(next->data.bytes.data(),next->data.bytes.size())){++v2Failures;return false;}
   next->gpu=w2v2::Lease(v2TextureBudget,next->data.bytes.size());
   if(!next->gpu){++v2BudgetWaits;std::lock_guard lock(ioMutex);textureReady.push_front(std::move(next->data));return true;}
   if(FAILED(device->CreateTexture(next->layout.width,next->layout.height,next->layout.levels,0,D3DFORMAT(next->layout.format),D3DPOOL_DEFAULT,&next->texture,nullptr)))return V2RetryTextureUpload(next->data);
   v2TextureUpload=std::move(next);
  }
  auto& upload=*v2TextureUpload;
  if(upload.data.epoch!=V2Epoch()){V2CancelTextureUpload();++v2StaleDrops;continue;}
  auto slice=upload.cursor.next(upload.layout,std::min<std::size_t>(256u*1024u,limit-v2TextureUploadBytes));
  if(!slice.rows)return true;
  if(!v2TextureStrip.upload(device,upload.texture,upload.layout,slice,upload.data.bytes.data())){
   V2RetryTextureUpload(upload.data);V2CancelTextureUpload();return false;
  }
  upload.cursor.advance(upload.layout,slice);v2TextureUploadBytes+=slice.bytes;
  if(upload.cursor.done(upload.layout)){
   auto hash=upload.data.hash;auto old=textures.find(hash);if(old!=textures.end()&&old->second){old->second->Release();textureResident-=textureBytes[hash];}
   textures[hash]=std::exchange(upload.texture,nullptr);v2TextureLeases[hash]=std::move(upload.gpu);
   textureUsed[hash]=GetTickCount();textureBytes[hash]=upload.layout.bytes;textureResident+=upload.layout.bytes;
   if(worldLogEnabled&&(upload.layout.width>=1024||upload.layout.height>=1024))Log("WORLD_HD_TEXTURE hash=%08X dimensions=%ux%u mips=%u bytes=%u sliced=1",hash,upload.layout.width,upload.layout.height,upload.layout.levels,unsigned(upload.layout.bytes));
   V2CancelTextureUpload();++completed;
  }
 }
 return true;
}
