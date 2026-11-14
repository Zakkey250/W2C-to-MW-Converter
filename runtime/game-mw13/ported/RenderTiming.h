#pragma once
// Optional lab observer. Intervals are application Present cadence, not scanout.
// Use the existing V2 lifecycle hook, avoiding a competing MinHook.
static double renderPresentStarted=0,renderPresentTotal=0,renderPresentMax=0;
static unsigned renderPresentFailures=0,renderPresentCount=0;
static std::uint64_t renderMaximumTextureBytesPerFrame=0;
static std::vector<double> renderPresentSamples;
static double renderPresentPrevious=0,renderPresentReported=0;
static void V2ObservePresentBegin(){
 auto& samples=renderPresentSamples;auto& previous=renderPresentPrevious;
 const double now=Clock();renderPresentStarted=now;
 renderMaximumTextureBytesPerFrame=std::max(renderMaximumTextureBytesPerFrame,v2TextureUploadBytes);
 if(previous&&*reinterpret_cast<int*>(0x925E90)==6){const double interval=now-previous;samples.push_back(interval);static double slowReported=0;if(interval>1000./60&&now-slowReported>=1000){slowReported=now;Log("W2C_SLOW_FRAME interval_ms=%.3f query_ms=%.3f queries=%u collision_ms=%.3f collisions=%u heli_ms=%.3f heli_queries=%u texture_upload_bytes=%llu shadow_ms=%.3f state_ms=%.3f submit_ms=%.3f passes=%u camera=%.3f,%.3f,%.3f",interval,renderFrameQueryMs,renderFrameQueries,renderFrameCollisionMs,renderFrameCollisionQueries,renderFrameHeliMs,renderFrameHeliQueries,static_cast<unsigned long long>(v2TextureUploadBytes),renderFrameShadowMs,renderFrameStateMs,renderFrameSubmitMs,renderFramePasses,cameraPosition.x,cameraPosition.y,cameraPosition.z);}}
 renderFrameHeliMs=0;renderFrameHeliQueries=0;renderFrameQueryMs=renderFrameSubmitMs=0;renderFrameQueries=renderFramePasses=0;
 renderFrameCollisionMs=renderFrameShadowMs=renderFrameStateMs=0;renderFrameCollisionQueries=0;
 previous=now;
}
static void V2ObservePresentEnd(HRESULT result){
 auto& samples=renderPresentSamples;auto& reported=renderPresentReported;auto& presentTotal=renderPresentTotal;auto& presentMax=renderPresentMax;auto& presentCount=renderPresentCount;auto& failures=renderPresentFailures;
 const double now=renderPresentStarted;
 const double callMs=Clock()-now;presentTotal+=callMs;presentMax=std::max(presentMax,callMs);++presentCount;
 if(FAILED(result))++failures;
 if(now-reported>=2000&&!samples.empty()){
  reported=now;std::sort(samples.begin(),samples.end());double sum=0;unsigned missed=0;
  for(auto ms:samples){sum+=ms;if(ms>1000./60)++missed;}
  Log("W2C_PRESENT_PERF frames=%u mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f over60=%u failures=%u camera=%.3f,%.3f,%.3f",unsigned(samples.size()),sum/samples.size(),samples[samples.size()/2],samples[samples.size()*95/100],samples[samples.size()*99/100],samples.back(),missed,failures,cameraPosition.x,cameraPosition.y,cameraPosition.z);
  Log("W2C_PRESENT_CALL mean_ms=%.3f max_ms=%.3f texture_max_frame_bytes=%llu",presentTotal/presentCount,presentMax,static_cast<unsigned long long>(renderMaximumTextureBytesPerFrame));presentTotal=presentMax=0;presentCount=0;
  samples.clear();failures=0;
 }

}
static void InstallRenderTiming(){
 static bool attempted=false;if(attempted||!device)return;attempted=true;
 if(!WorldDiagnosticFlag(Path("W2CMWv2.ini").c_str(),"Renderer","ProfilePresent"))return;
 renderWorkProfiling=true;
 Log("W2C_PRESENT_OBSERVER_ACTIVE application_cadence=1 scanout_measured=0 existing_v2_hook=1");
}
