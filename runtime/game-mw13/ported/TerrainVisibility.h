#pragma once
#include "RenderFrustum.h"

static constexpr unsigned terrainSourceRange=650;
static unsigned terrainForwardRange=2000,terrainPrefetchRange=2000,terrainFadeMs=250;
static std::set<unsigned> terrainPanoramaSections,terrainForwardSections;
static std::optional<RenderFrustum> terrainMainFrustum;
static Vec3 terrainMainEye{};

static void LoadTerrainVisibilitySettings(const char* ini){
 const auto read=[ini](const char* key,unsigned fallback,unsigned minimum,unsigned maximum){
  const unsigned value=GetPrivateProfileIntA("Renderer",key,fallback,ini);
  return value>=minimum&&value<=maximum?value:fallback;
 };
 terrainForwardRange=read("ForwardDrawMeters",2000,terrainSourceRange,2000);
 terrainPrefetchRange=std::max(terrainForwardRange,read("ForwardPrefetchMeters",2000,terrainSourceRange,2000));
 terrainFadeMs=read("ForwardFadeMs",250,0,1000);
}

static bool TerrainSourceVisible(unsigned id){
 return zoneIndex==~0u||id==26000||zones[zoneIndex].sections.count(id)!=0;
}

static void SetTerrainMainCamera(Vec3 eye,const D3DMATRIX& clip){
 terrainMainEye=eye;
 terrainMainFrustum.emplace(clip);
}

static void SelectTerrainForwardZones(){
 terrainForwardSections.clear();
 if(!terrainMainFrustum||zoneIndex==~0u)return;
 // Source PVS membership also activates backdrop/terrain variants. Unioning
 // neighboring PVS sets exposes partial landscape and incompatible LODs.
 // Extend the draw distance of the current source selection only.
 for(auto id:zones[zoneIndex].sections)
  if(!terrainPanoramaSections.count(id))terrainForwardSections.insert(id);
}

static bool TerrainForwardVisible(unsigned id,const Header& bounds,Vec3 eye,const RenderFrustum& frustum,unsigned meters){
 return terrainForwardSections.count(id)&&
        Distance(bounds,eye)<=float(meters*meters)&&
        frustum.Visible(bounds.minimum,bounds.maximum);
}

static bool TerrainSectionVisible(unsigned id,const Header& bounds,Vec3 eye,const RenderFrustum& frustum,bool extended){
 // Panorama and authored LOD sections retain their source PVS. Extend regular
 // scenery only. Every draw receives the same range and six-plane test below.
 if(TerrainSourceVisible(id)&&Distance(bounds,eye)<=terrainSourceRange*terrainSourceRange)return true;
 return extended&&TerrainForwardVisible(id,bounds,eye,frustum,terrainForwardRange);
}
