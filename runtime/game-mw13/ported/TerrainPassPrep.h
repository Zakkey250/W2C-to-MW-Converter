#pragma once
#include <xmmintrin.h>

// All page metadata is immutable between the render thread's residency pump
// and the end of this pass. Workers only read bounds, ordinals and light data.
struct TerrainDecision {
 float distance=0;
 unsigned lights=0;
 bool visible=false,enclosed=false;
};
struct TerrainPrepLight {Vec3 position;float rangeSquared;unsigned bit;};
struct TerrainSlice {
 const PageGpu* page;
 const std::vector<unsigned>* order;
 const Header* header;
 unsigned output,first,last,compactBase,kept;
 unsigned pass;
};
struct TerrainPrepStatus {
 unsigned abi=3,depthPasses=0,roadPasses=0,checked=0,mismatches=0;
 unsigned depthWorkerDraws=0,roadWorkerDraws=0,depthLocalDraws=0,roadLocalDraws=0;
 unsigned workerCpuMask=0,renderCpuMask=0;
 unsigned deferredSections=0,compactInput=0,compactOutput=0,compactDifferences=0;
 unsigned renderThreadId=0,timedDepth=0,timedRoad=0;
 unsigned long long depthTicks=0,roadTicks=0,depthCycles=0,roadCycles=0;
};
static_assert(offsetof(TerrainPrepStatus,depthTicks)==72);
extern "C" { __declspec(dllexport) TerrainPrepStatus W2C_TerrainPrepStatusV1; }
static bool PrepSwitch(const char* name){
 char value[3]{};return GetEnvironmentVariableA(name,value,sizeof(value))==1&&value[0]=='1';
}
struct TerrainPhaseTimer {
 LARGE_INTEGER begin{};
 ULONG64 cycles=0;
 bool depth;
 TerrainPhaseTimer():depth(envDepthPass){
  static const bool enabled=PrepSwitch("MWLB_TERRAIN_AUDIT")||PrepSwitch("MWLB_TERRAIN_PICTURES");
  if(!enabled||(!depth&&!envRoadReflectionPass))return;
  W2C_TerrainPrepStatusV1.renderThreadId=GetCurrentThreadId();
  QueryThreadCycleTime(GetCurrentThread(),&cycles);QueryPerformanceCounter(&begin);
 }
 ~TerrainPhaseTimer(){
  if(!begin.QuadPart)return;
  LARGE_INTEGER end;ULONG64 now=0;QueryPerformanceCounter(&end);QueryThreadCycleTime(GetCurrentThread(),&now);
  auto& s=W2C_TerrainPrepStatusV1;
  ++(depth?s.timedDepth:s.timedRoad);
  (depth?s.depthTicks:s.roadTicks)+=end.QuadPart-begin.QuadPart;
  (depth?s.depthCycles:s.roadCycles)+=now-cycles;
 }
};
class TerrainPassPrep {
 std::vector<TerrainSlice> slices;
 std::vector<TerrainDecision> decisions;
 std::vector<unsigned> sectionEnds,opaqueEnds,visibleOrdinals,submitOrdinals;
 std::unique_ptr<std::atomic<unsigned>[]> states;
 size_t stateCapacity=0;
 std::atomic<unsigned> next{0},offThread{0},cpuMask{0};
 std::array<TerrainPrepLight,16> lights;
 unsigned lightCount=0,mxcsr=0;
 std::optional<RenderFrustum> frustum;
 std::optional<PageFrustumPackets> packedFrustum;
 Vec3 eye{};
 bool road=false,audit=false;
 bool available=false,overlapSubmission=false,sectionPrepared=false;
 PTP_WORK work=nullptr;

 void prepare(unsigned index){
  auto& slice=slices[index];const auto& section=*slice.header;
  const auto& packets=slice.page->bounds[slice.pass];
  unsigned kept=0,visibleMask=0;
  for(unsigned at=slice.first;at<slice.last;++at){
   const unsigned ordinal=(*slice.order)[at];
   const auto& draw=slice.page->draws[ordinal];
   auto& out=decisions[slice.output+ordinal];
   if(!packets.empty()){
    if(!(at%4))visibleMask=packedFrustum->visible(packets[at/4]);
    out.visible=(visibleMask&(1u<<(at%4)))!=0;
   }else out.visible=frustum->Visible(draw.minimum,draw.maximum);
   if(!out.visible)continue;
   if(overlapSubmission)visibleOrdinals[slice.compactBase+kept++]=ordinal;
   Header bounds{};bounds.minimum=draw.minimum;bounds.maximum=draw.maximum;
   out.distance=Distance(bounds,eye);
   out.enclosed=draw.minimum.x>=section.minimum.x&&draw.minimum.y>=section.minimum.y&&draw.minimum.z>=section.minimum.z&&
                draw.maximum.x<=section.maximum.x&&draw.maximum.y<=section.maximum.y&&draw.maximum.z<=section.maximum.z;
   out.lights=0;
   for(unsigned i=0;i<lightCount;++i){
    const auto& l=lights[i];
    if(RenderLightIntersectsSquared(l.position,l.rangeSquared,draw.minimum,draw.maximum))out.lights|=l.bit;
   }
  }
  slice.kept=kept;
 }
 bool claim(unsigned i){
  unsigned idle=0;
  if(!states[i].compare_exchange_strong(idle,1,std::memory_order_acq_rel))return false;
  prepare(i);states[i].store(2,std::memory_order_release);return true;
 }
 static void CALLBACK worker(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK){
  auto& plan=*static_cast<TerrainPassPrep*>(context);
  const auto prior=_mm_getcsr();_mm_setcsr(plan.mxcsr);
  unsigned draws=0;
  for(;;){
   unsigned i=plan.next.fetch_add(1,std::memory_order_relaxed);
   if(i>=plan.slices.size())break;
   // In overlapped mode prepare upcoming sections first. The submitting
   // thread can always use its original code if a section is still in flight.
   if(!plan.overlapSubmission)i=unsigned(plan.slices.size())-1-i;
   if(plan.claim(i)&&plan.audit)draws+=plan.slices[i].last-plan.slices[i].first;
  }
  if(draws){plan.offThread.fetch_add(draws);plan.cpuMask.fetch_or(1u<<(GetCurrentProcessorNumber()%32));}
  _mm_setcsr(prior);
 }
public:
 static unsigned workers(){
  static const unsigned count=[](){
   char value[3]{};
   if(GetEnvironmentVariableA("MWLB_X3D_WORKERS",value,3)!=1||value[0]<'2'||value[0]>'8')return 0u;
   DWORD_PTR allowed=0,system=0;
   if(!GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system))return 0u;
   const auto logical=std::popcount(static_cast<unsigned>(allowed));
   return std::min(unsigned(value[0]-'0'),logical>1?unsigned(logical-1):0u);
  }();
  return count;
 }
 void start(std::vector<PagedRenderCandidate>& candidates,const RenderFrustum& view,Vec3 position,bool isRoad,bool mainPass){
  static const bool enabled=PrepSwitch("MWLB_TERRAIN_WORKERS");
  available=false;sectionPrepared=false;
  if((!enabled&&!workers())||(!envDepthPass&&!isRoad&&!(workers()&&mainPass))||candidates.empty())return;
  static const bool checking=PrepSwitch("MWLB_TERRAIN_AUDIT")||PrepSwitch("MWLB_PACKET_AUDIT");audit=checking;
  static const bool pipelined=PrepSwitch("MWLB_TERRAIN_PIPELINE");overlapSubmission=pipelined;
  slices.clear();sectionEnds.clear();opaqueEnds.clear();unsigned output=0,count=0;
  for(auto& c:candidates){
   c.planOffset=output;const auto& page=*c.section->page;
   output+=unsigned(page.draws.size());
   for(unsigned pass=0;pass<(envDepthPass?1u:2u);++pass){
    const auto& order=page.order[pass];
    for(unsigned i=0;i<order.size();i+=128)
     slices.push_back({&page,&order,&c.section->header,c.planOffset,i,std::min(i+128,unsigned(order.size())),count+i,0,pass});
    count+=unsigned(order.size());
    if(pass==0)opaqueEnds.push_back(unsigned(slices.size()));
   }
   sectionEnds.push_back(unsigned(slices.size()));
  }
  if(count<(workers()?1024u:256u))return;
  decisions.resize(output);
  if(overlapSubmission)visibleOrdinals.resize(count);
  if(states==nullptr||slices.size()>stateCapacity){
   stateCapacity=slices.size();states=std::make_unique<std::atomic<unsigned>[]>(stateCapacity);
  }
  for(size_t i=0;i<slices.size();++i)states[i].store(0,std::memory_order_relaxed);
  next=0;offThread=0;cpuMask=0;eye=position;frustum=view;packedFrustum.emplace(view.plane);mxcsr=_mm_getcsr();road=isRoad;lightCount=0;
  if(!envDepthPass&&envBoundedLights){
   auto candidates=envActiveDynamicLights;
   while(candidates){
    const unsigned i=std::countr_zero(candidates);
    candidates&=candidates-1;
    lights[lightCount++]={{envDP[i][0],envDP[i][1],envDP[i][2]},envDynamicRangeSquared[i],1u<<i};
   }
  }
  work=CreateThreadpoolWork(worker,this,nullptr);
  if(!work)return;
  available=true;
  const unsigned jobs=std::min(unsigned(slices.size()),workers()?workers():2u);
  for(unsigned job=0;job<jobs;++job)SubmitThreadpoolWork(work);
 }
 void complete(){
  for(unsigned i=0;i<slices.size();++i)claim(i);
  finish();
  sectionPrepared=true;
 }
 void ready(size_t section){
  if(!work)return;
  sectionPrepared=true;
  const unsigned first=section?sectionEnds[section-1]:0,end=sectionEnds[section];
  for(unsigned i=first;i<end;++i){
   auto state=states[i].load(std::memory_order_acquire);
   if(state==2)continue;
   if(overlapSubmission){sectionPrepared=false;if(audit)++W2C_TerrainPrepStatusV1.deferredSections;break;}
   if(!state&&claim(i)){
    if(audit)(road?W2C_TerrainPrepStatusV1.roadLocalDraws:W2C_TerrainPrepStatusV1.depthLocalDraws)+=slices[i].last-slices[i].first;
    continue;
   }
   while(states[i].load(std::memory_order_acquire)!=2)YieldProcessor();
  }
 }
 const TerrainDecision* get(unsigned ordinal)const{return available&&sectionPrepared?&decisions[ordinal]:nullptr;}
 bool validating()const{return available&&sectionPrepared&&audit;}
 bool active()const{return available;}
 bool overlapping()const{return available&&overlapSubmission;}
 const std::vector<unsigned>& order(size_t section,unsigned pass,const PageGpu& page){
  const auto& native=page.order[pass];
  if(!overlapping()||!sectionPrepared||worldLogEnabled)return native;
  const unsigned begin=pass?opaqueEnds[section]:(section?sectionEnds[section-1]:0);
  const unsigned end=pass?sectionEnds[section]:opaqueEnds[section];
  submitOrdinals.clear();
  for(unsigned i=begin;i<end;++i){
   const auto& slice=slices[i];
   submitOrdinals.insert(submitOrdinals.end(),visibleOrdinals.begin()+slice.compactBase,
                         visibleOrdinals.begin()+slice.compactBase+slice.kept);
  }
  if(audit){
   auto& s=W2C_TerrainPrepStatusV1;
   s.compactInput+=unsigned(native.size());s.compactOutput+=unsigned(submitOrdinals.size());
   size_t cursor=0;
   for(unsigned ordinal:native){
    const auto& draw=page.draws[ordinal];
    if(!frustum->Visible(draw.minimum,draw.maximum))continue;
    if(cursor>=submitOrdinals.size()||submitOrdinals[cursor]!=ordinal)++s.compactDifferences;
    ++cursor;
   }
   if(cursor!=submitOrdinals.size())++s.compactDifferences;
  }
  return submitOrdinals;
 }
 void finish(){
  if(!work)return;
  WaitForThreadpoolWorkCallbacks(work,FALSE);CloseThreadpoolWork(work);work=nullptr;
  if(audit){auto& s=W2C_TerrainPrepStatusV1;++(road?s.roadPasses:s.depthPasses);
   (road?s.roadWorkerDraws:s.depthWorkerDraws)+=offThread.load();
   s.workerCpuMask|=cpuMask.load();s.renderCpuMask|=1u<<(GetCurrentProcessorNumber()%32);}
 }
};

static TerrainPassPrep pagedPrep;
