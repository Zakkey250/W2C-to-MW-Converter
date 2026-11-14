#pragma once
// D3D9 drivers can omit transforms from a predefined state block. Explicitly
// preserve the four transforms our scenery/props/barriers modify, as the
// bundled ImGui DX9 backend also does for world/view/projection.
struct RenderTransformState {
 D3DMATRIX matrices[4]{};unsigned valid=0;
 static D3DTRANSFORMSTATETYPE Type(unsigned index){const D3DTRANSFORMSTATETYPE types[]{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION,D3DTS_TEXTURE0};return types[index];}
 void Capture(IDirect3DDevice9* dev){valid=0;for(unsigned n=0;n<4;++n)if(SUCCEEDED(dev->GetTransform(Type(n),&matrices[n])))valid|=1u<<n;}
 unsigned Differences(IDirect3DDevice9* dev)const{unsigned changed=0;for(unsigned n=0;n<4;++n)if(valid&(1u<<n)){D3DMATRIX after{};if(FAILED(dev->GetTransform(Type(n),&after))||memcmp(&after,&matrices[n],sizeof(after)))changed|=1u<<n;}return changed;}
 void Restore(IDirect3DDevice9* dev)const{for(unsigned n=0;n<4;++n)if(valid&(1u<<n))dev->SetTransform(Type(n),&matrices[n]);}
};
