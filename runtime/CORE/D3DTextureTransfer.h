#pragma once
#include <d3d9.h>
#include "TextureTransfer.h"
namespace w2v2 {
// One bounded SYSTEMMEM strip is reused. DEFAULT destinations receive explicit
// UpdateSurface transfers instead of an unbounded first-use managed upload.
struct TextureStrip {
 IDirect3DTexture9* texture=nullptr;unsigned width=0,height=0,level=0,format=0;
 ~TextureStrip(){clear();}
 void clear(){if(texture)texture->Release();texture=nullptr;width=height=level=format=0;}
 bool prepare(IDirect3DDevice9* device,const TextureLayout& l,const TextureSlice& s){
  unsigned k=0;if(l.block)while(((s.width<<k)%4||(s.height<<k)%4)&&k<2)++k;
  unsigned w=s.width<<k,h=s.height<<k;std::size_t scratch=0;
  for(unsigned i=0;i<=k;++i){scratch+=l.block?std::size_t((std::max(1u,w>>i)+3)/4)*((std::max(1u,h>>i)+3)/4)*l.block:std::size_t(w)*h*4;}
  if(w>8192||h>8192||scratch>512u*1024u)return false;
  if(texture&&width==w&&height==h&&level==k&&format==l.format)return true;
  clear();if(FAILED(device->CreateTexture(w,h,k+1,0,D3DFORMAT(l.format),D3DPOOL_SYSTEMMEM,&texture,nullptr)))return false;
  width=w;height=h;level=k;format=l.format;return true;
 }
 bool upload(IDirect3DDevice9* device,IDirect3DTexture9* destination,const TextureLayout& l,const TextureSlice& s,const void* data){
  if(!s.rows||!prepare(device,l,s))return false;
  D3DLOCKED_RECT rect{};if(FAILED(texture->LockRect(level,&rect,nullptr,0)))return false;
  const auto& m=l.mip[s.level];bool ok=rect.pBits&&rect.Pitch>=int(m.rowBytes);
  if(ok)for(unsigned r=0;r<s.rows;++r)std::memcpy(static_cast<unsigned char*>(rect.pBits)+std::size_t(r)*rect.Pitch,static_cast<const unsigned char*>(data)+s.offset+std::size_t(r)*m.rowBytes,m.rowBytes);
  if(FAILED(texture->UnlockRect(level)))ok=false;if(!ok)return false;
  IDirect3DSurface9 *src=nullptr,*dst=nullptr;
  ok=SUCCEEDED(texture->GetSurfaceLevel(level,&src))&&SUCCEEDED(destination->GetSurfaceLevel(s.level,&dst));
  POINT point{0,LONG(s.y)};if(ok)ok=SUCCEEDED(device->UpdateSurface(src,nullptr,dst,&point));
  if(src)src->Release();if(dst)dst->Release();return ok;
 }
 TextureStrip()=default;TextureStrip(const TextureStrip&)=delete;TextureStrip& operator=(const TextureStrip&)=delete;
};
}
