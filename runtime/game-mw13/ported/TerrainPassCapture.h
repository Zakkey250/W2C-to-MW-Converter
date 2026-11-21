#pragma once
// Two diagnostic snapshots from a parked camera. Never create a readback
// surface or touch the filesystem in an ordinary performance run.
static void CaptureTerrainPass(Vec3 eye,const D3DMATRIX& clip){
 static const bool enabled=PrepSwitch("MWLB_TERRAIN_PICTURES");
 if(!enabled||(!envDepthPass&&!envRoadReflectionPass)||initialWorldHold)return;
 static bool seen[2]{};
 const unsigned id=envDepthPass?0:1;
 if(seen[id])return;
 char ready[MAX_PATH]{},dir[MAX_PATH]{};
 const auto a=GetEnvironmentVariableA("MWLB_TERRAIN_READY",ready,MAX_PATH);
 const auto b=GetEnvironmentVariableA("MWLB_TERRAIN_OUTPUT",dir,MAX_PATH);
 if(!a||a>=MAX_PATH||!b||b>=MAX_PATH||GetFileAttributesA(ready)==INVALID_FILE_ATTRIBUTES)return;
 seen[id]=true;
 IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr;
 D3DSURFACE_DESC d{};D3DLOCKED_RECT rect{};
 HRESULT hr=device->GetRenderTarget(0,&target);
 if(SUCCEEDED(hr))hr=target->GetDesc(&d);
 unsigned bytes=d.Format==D3DFMT_A16B16G16R16F?8:4;
 if(SUCCEEDED(hr)&&d.Format!=D3DFMT_A16B16G16R16F&&d.Format!=D3DFMT_A8R8G8B8&&d.Format!=D3DFMT_X8R8G8B8&&d.Format!=D3DFMT_R32F)hr=E_NOTIMPL;
 if(SUCCEEDED(hr)&&d.MultiSampleType!=D3DMULTISAMPLE_NONE){
  hr=device->CreateRenderTarget(d.Width,d.Height,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr);
  if(SUCCEEDED(hr))hr=device->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE);
 }
 if(SUCCEEDED(hr))hr=device->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr);
 if(SUCCEEDED(hr))hr=device->GetRenderTargetData(resolved?resolved:target,staging);
 if(SUCCEEDED(hr))hr=staging->LockRect(&rect,nullptr,D3DLOCK_READONLY);
 char path[MAX_PATH];sprintf_s(path,"%s/%s.bin",dir,id?"road-target":"depth-target");
 FILE* file=nullptr;fopen_s(&file,path,"wb");
 if(file){
  const unsigned header[]={unsigned(hr),d.Width,d.Height,unsigned(d.Format),bytes,frameCounter};
  fwrite(header,sizeof(header),1,file);fwrite(&eye,sizeof(eye),1,file);fwrite(&clip,sizeof(clip),1,file);
  if(SUCCEEDED(hr))for(unsigned y=0;y<d.Height;++y)
   fwrite(static_cast<const char*>(rect.pBits)+y*rect.Pitch,bytes,d.Width,file);
  fclose(file);
 }
 if(SUCCEEDED(hr))staging->UnlockRect();
 if(staging)staging->Release();if(resolved)resolved->Release();if(target)target->Release();
}
