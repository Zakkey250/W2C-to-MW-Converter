#pragma once
// Active only on the submitting thread during this terrain pass. Device texture
// lifetimes and all actual state transitions still belong to the render thread.
struct TerrainLookup {
 struct TextureEntry {unsigned key=0;IDirect3DTexture9* texture=nullptr;DWORD* lastUse=nullptr;};
 struct MaterialEntry {unsigned key=0;Material value{};bool known=false;};
 std::array<TextureEntry,128> textures{};
 std::array<MaterialEntry,128> materials{};
 TerrainLookup* previous;
 static inline thread_local TerrainLookup* current=nullptr;
 TerrainLookup():previous(current){current=this;}
 ~TerrainLookup(){current=previous;}
 static void invalidateTextures(){for(auto* p=current;p;p=p->previous)p->textures.fill({});}
 static unsigned slot(unsigned key){return (key*2654435761u)>>25;}
};

struct TerrainDeviceState {
 IDirect3DDevice9* device;
 explicit TerrainDeviceState(IDirect3DDevice9* graphics):device(graphics){}
 struct Word {DWORD value=0;bool valid=false;};
 std::array<Word,D3DRS_BLENDOPALPHA+1> render{};
 std::array<Word,D3DTSS_CONSTANT+1> stage{};
 std::array<Word,D3DSAMP_DMAPOFFSET+1> sampler{};
 IDirect3DTexture9* bound=nullptr;
 bool textureKnown=false;
 template<class Set> static void change(Word& state,DWORD value,Set submit){
  if(state.valid&&state.value==value)return;
  state.valid=SUCCEEDED(submit());state.value=value;
 }
 void SetRenderState(D3DRENDERSTATETYPE key,DWORD value){
  change(render[key],value,[&]{return device->SetRenderState(key,value);});
 }
 void SetTextureStageState(DWORD slot,D3DTEXTURESTAGESTATETYPE key,DWORD value){
  change(stage[key],value,[&]{return device->SetTextureStageState(slot,key,value);});
 }
 void SetSamplerState(DWORD slot,D3DSAMPLERSTATETYPE key,DWORD value){
  change(sampler[key],value,[&]{return device->SetSamplerState(slot,key,value);});
 }
 void SetTexture(DWORD slot,IDirect3DTexture9* value){
  if(!textureKnown||bound!=value){
   textureKnown=SUCCEEDED(device->SetTexture(slot,value));
   bound=value;
  }
 }
};
