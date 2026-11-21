#pragma once
#include <windows.h>

// Process-local observation only. A loaded provider does not prove rendering.
struct MWLBRendererStatusV1 {
    DWORD structBytes=sizeof(MWLBRendererStatusV1),abiVersion=1;
    DWORD requestedApi=9,providerApi=0,initializationError=0;
    DWORD rootsCreated=0,devicesCreated=0,presentsSucceeded=0;
    DWORD flipExRequested=0,flipExDevices=0,flipExFallbacks=0;
    DWORD d3d11DeviceCreates=0,d3d11SwapchainCreates=0,featureLevel=0;
    DWORD adapterLuidLow=0,adapterLuidHigh=0,lastHresult=0;
    DWORD d3d9ExFactories=0,flipExPresentsSucceeded=0;
    DWORD windowedResetCorrections=0;
};
using MWLBGetRendererStatusV1=BOOL(WINAPI*)(MWLBRendererStatusV1*,DWORD);
