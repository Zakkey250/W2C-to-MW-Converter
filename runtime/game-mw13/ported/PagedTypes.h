#pragma once
#include "../../core/Budget.h"
#include "../../source-format/SourceStore.h"
struct PageDraw {unsigned ordinal,texture,oldFirstVertex,oldVertexCount,oldFirstIndex,indexCount;w2mw::Vec3 minimum,maximum;EnvironmentMeshLights lights;unsigned owner,pass,reserved[2];};
struct PageSpan {unsigned source,chunk,first,count,local,sequence,reserved[2];};
static_assert(sizeof(PageDraw)==128&&sizeof(PageSpan)==32);
struct PageBlock {unsigned vertices=0,indices=0,shadows=0;w2v2disk::B bytes;};
struct PageCpu {unsigned id=0;w2v2::Epoch epoch;w2v2::Lease stage;std::vector<PageDraw> draws;std::vector<PageSpan> spans,shadowSpans;std::array<std::vector<unsigned>,2> order;std::vector<PageBlock> blocks;std::vector<unsigned> textureKeys;};
struct PageGpuBlock {
 IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr,*shadow=nullptr;unsigned vertices=0,indices=0,shadows=0;w2v2::Lease bytes;
 PageGpuBlock()=default;PageGpuBlock(const PageGpuBlock&)=delete;PageGpuBlock& operator=(const PageGpuBlock&)=delete;
 PageGpuBlock(PageGpuBlock&& x)noexcept:vb(std::exchange(x.vb,nullptr)),ib(std::exchange(x.ib,nullptr)),shadow(std::exchange(x.shadow,nullptr)),vertices(x.vertices),indices(x.indices),shadows(x.shadows),bytes(std::move(x.bytes)){}
 ~PageGpuBlock(){if(vb)vb->Release();if(ib)ib->Release();if(shadow)shadow->Release();}
};
static std::map<unsigned,unsigned> v2TexturePins;
struct TexturePins {std::vector<unsigned> keys;void acquire(std::vector<unsigned> values){for(auto key:values)++v2TexturePins[key];keys=std::move(values);}~TexturePins(){for(auto key:keys){auto it=v2TexturePins.find(key);if(it!=v2TexturePins.end()&&!--it->second)v2TexturePins.erase(it);}}};
struct PageGpu {TexturePins pins;w2v2::Epoch epoch;w2v2::Lease metadata;std::vector<PageDraw> draws;std::vector<PageSpan> spans,shadowSpans;std::array<std::vector<unsigned>,2> order;std::vector<PageGpuBlock> blocks;std::vector<std::pair<unsigned,unsigned>> drawSpans;};
static w2source::SourceStore v2Store;static w2v2disk::B v2CachedMaterials;
static w2v2::Budget v2ReadBudget(32ull*1048576),v2StageBudget(64ull*1048576),v2MetadataBudget(128ull*1048576),v2GeometryBudget(384ull*1048576),v2TextureBudget(512ull*1048576);
static std::atomic<std::uint64_t> v2WorldEpoch{1},v2DeviceEpoch{1};
static std::atomic<unsigned> v2Failures{0},v2StaleDrops{0},v2BudgetWaits{0};
static DWORD v2OwnerThread=0;static std::atomic<bool> v2Lost{false};static std::uint64_t v2UploadBytes=0;static double v2UploadStarted=0;static unsigned v2Published=0;
static w2v2::Epoch V2Epoch(){return {v2WorldEpoch.load(),v2DeviceEpoch.load()};}
static bool V2Owner(){if(!v2OwnerThread)v2OwnerThread=GetCurrentThreadId();return v2OwnerThread==GetCurrentThreadId();}

struct V2Request {unsigned id=0;w2v2::Epoch epoch;V2Request(unsigned value):id(value),epoch(V2Epoch()){}V2Request(unsigned value,w2v2::Epoch captured):id(value),epoch(captured){}operator unsigned()const{return id;}};
