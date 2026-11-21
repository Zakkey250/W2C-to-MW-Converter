#pragma once
#include "LBRendererStatusV1.h"

// Called after the game's graphics device exists. An absent LB or a graphics
// fallback keeps the stock upload policy for the lifetime of this device.
static bool LargerDx12Uploads(){
 static const bool enabled=[]{
  if(!GetPrivateProfileIntA("Streaming","DX12UploadBoost",1,Path("W2CMWv2.ini").c_str()))return false;
  const auto renderer=GetModuleHandleA(Path("d3d9.dll").c_str());
  const auto query=renderer?reinterpret_cast<MWLBGetRendererStatusV1>(GetProcAddress(renderer,"MWLB_GetRendererStatusV1")):nullptr;
  MWLBRendererStatusV1 status{};
  return query&&query(&status,sizeof(status))&&status.abiVersion==1&&status.requestedApi==12&&
         status.providerApi==12&&!status.initializationError&&
         (status.d3d11DeviceCreates>0||(status.featureLevel>=0xb000&&(status.adapterLuidLow||status.adapterLuidHigh)));
 }();
 return enabled;
}

static double geometryWorkMs=0,textureWorkMs=0;
struct UploadSlice {
 double& spent;
 double start;
 UploadSlice(double& total,bool enabled):spent(total),start(enabled?Clock():0){}
 ~UploadSlice(){if(start)spent+=Clock()-start;}
 bool expired(double budget) const {return spent+Clock()-start>=budget;}
};

struct W2CStreamMetrics {
 DWORD bytes=sizeof(W2CStreamMetrics),version=1,enabled=0,boost=0,scans=0;
 DWORD visible=0,missing=0,visibleSamples=0,missingSamples=0;
 DWORD published=0,waitedForTexture=0,geometryLimit=0;
};
extern "C" __declspec(dllexport) W2CStreamMetrics W2CStreamMetricsV1{};
#pragma comment(linker,"/EXPORT:W2CStreamMetricsV1=_W2CStreamMetricsV1")
