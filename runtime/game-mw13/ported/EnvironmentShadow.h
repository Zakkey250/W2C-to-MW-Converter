#pragma once
// Share MW's native shadow depth with SunSet and the vehicle shaders. No
// private shadow target, camera or independently timed world is introduced.
static D3DMATRIX envShadowClip{};
static bool envShadowReady=false;
static bool envMirrorShadowReceiver=true;
static unsigned envShadowWidth=0;
static bool ReadNativeShadowTransform(ID3DXEffect* effect,D3DMATRIX& result){
 if(!effect)return false;D3DXMATRIX world{},shadow{};auto w=effect->GetParameterByName(nullptr,"cmWorldMat"),s=effect->GetParameterByName(nullptr,"matShadowMapWVP");
 if(!w||!s||FAILED(effect->GetMatrix(w,&world))||FAILED(effect->GetMatrix(s,&shadow)))return false;
 double a[4][8]{};for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c){if(!std::isfinite(world.m[r][c])||!std::isfinite(shadow.m[r][c]))return false;a[r][c]=world.m[r][c];}a[r][4+r]=1;}
 for(unsigned c=0;c<4;++c){unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;if(std::abs(a[pivot][c])<1e-10)return false;if(pivot!=c)for(unsigned k=0;k<8;++k)std::swap(a[pivot][k],a[c][k]);double scale=a[c][c];for(unsigned k=0;k<8;++k)a[c][k]/=scale;for(unsigned r=0;r<4;++r)if(r!=c){scale=a[r][c];for(unsigned k=0;k<8;++k)a[r][k]-=scale*a[c][k];}}
 result={};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)result.m[r][c]+=float(a[r][k+4]*shadow.m[k][c]);return true;
}
static void BindEnvironmentShadow(){
 // A mirror consumes a different camera view while sharing the player's shadow
 // map. Allow this receiver alone to be disabled; leave the map and main view intact.
 auto target=*reinterpret_cast<BYTE**>(0x982A20);
 if(!envMirrorShadowReceiver&&target&&*reinterpret_cast<unsigned*>(target+4)==3){
  const float disabled[4]{};device->SetTexture(6,nullptr);
  device->SetPixelShaderConstantF(95,disabled,1);return;
 }
 float settings[4]{};IDirect3DBaseTexture9* texture=nullptr;
 auto effect=EnvironmentEffect(0);float enabled=0;
 if(effect){auto flag=effect->GetParameterByName(nullptr,"cfShadowsEnabled");if(flag)effect->GetFloat(flag,&enabled);}
 if(envShadowReady&&enabled>0&&effect){
  auto handle=effect->GetParameterByName(nullptr,"ShadowMapTex");if(handle)effect->GetTexture(handle,&texture);
  D3DSURFACE_DESC desc{};
  if(texture&&texture->GetType()==D3DRTYPE_TEXTURE&&SUCCEEDED(reinterpret_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc))&&desc.Width>1){
   settings[0]=1;settings[1]=(desc.Format==D3DFMT_R32F||desc.Format==D3DFORMAT(MAKEFOURCC('I','N','T','Z'))||desc.Format==D3DFORMAT(MAKEFOURCC('D','F','2','4')))?1.f:0.f;settings[2]=1.f/desc.Width;settings[3]=1.f/desc.Height;
  }
 }
 device->SetTexture(6,settings[0]>0?texture:nullptr);if(texture)texture->Release();
 device->SetPixelShaderConstantF(91,&envShadowClip._11,4);device->SetPixelShaderConstantF(95,settings,1);
 device->SetSamplerState(6,D3DSAMP_ADDRESSU,D3DTADDRESS_BORDER);device->SetSamplerState(6,D3DSAMP_ADDRESSV,D3DTADDRESS_BORDER);device->SetSamplerState(6,D3DSAMP_BORDERCOLOR,0xFFFFFFFF);
 device->SetSamplerState(6,D3DSAMP_MINFILTER,settings[1]>0?D3DTEXF_POINT:D3DTEXF_LINEAR);device->SetSamplerState(6,D3DSAMP_MAGFILTER,settings[1]>0?D3DTEXF_POINT:D3DTEXF_LINEAR);device->SetSamplerState(6,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
 static DWORD last=0;DWORD now=GetTickCount();if(worldLogEnabled&&now-last>10000){last=now;Log("WORLD_SHADOW_RECEIVER ready=%u enabled=%.1f bound=%.0f raw_depth=%.0f width=%u",unsigned(envShadowReady),enabled,settings[0],settings[1],envShadowWidth);
  D3DMATRIX native{};if(ReadNativeShadowTransform(effect,native)){auto project=[&](const D3DMATRIX& m,float* uv){float w=cameraPosition.x*m._14+cameraPosition.y*m._24+cameraPosition.z*m._34+m._44;for(unsigned c=0;c<3;++c)uv[c]=(cameraPosition.x*m.m[0][c]+cameraPosition.y*m.m[1][c]+cameraPosition.z*m.m[2][c]+m.m[3][c])/w;};float own[3]{},stock[3]{};project(envShadowClip,own);project(native,stock);Log("WORLD_SHADOW_COORD_COMPARE own=%.6f,%.6f,%.6f native=%.6f,%.6f,%.6f",own[0],own[1],own[2],stock[0],stock[1],stock[2]);}
 }
}
