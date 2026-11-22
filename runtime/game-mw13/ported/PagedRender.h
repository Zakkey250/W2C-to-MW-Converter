#pragma once
#include "EnvironmentShaderPSVisibility.generated.h"
#include "EnvironmentShaderPSDepthVisibility.generated.h"
#include "EnvironmentShaderPSOpaqueDepth.generated.h"

static IDirect3DPixelShader9* PagedOpaqueDepthShader(){
 static IDirect3DPixelShader9* shader=nullptr;
 static bool attempted=false;
 if(!attempted){
  attempted=true;
  if(FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPSOpaqueDepth),&shader)))shader=nullptr;
 }
 return shader;
}

static IDirect3DPixelShader9* PagedVisibilityShader(){
 static bool attempted=false;
 static IDirect3DPixelShader9* color=nullptr;
 static IDirect3DPixelShader9* depth=nullptr;
 if(!attempted){
  attempted=true;
  device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPSVisibility),&color);
  device->CreatePixelShader(reinterpret_cast<const DWORD*>(envShaderPSDepthVisibility),&depth);
 }
 return color&&depth?(envDepthPass?depth:color):nullptr;
}

struct PagedDrawStats {unsigned sections=0,meshes=0,calls=0,nightDrawn=0,nightSkipped=0,overlapSkipped=0,farMeshes=0,rangeSkipped=0,frustumCulled=0,expandedMeshes=0,farRegularMeshes=0;float farthest=0,farthestRegular=0;};
struct PagedRenderCandidate {unsigned id; Section* section; bool eligible; bool panorama;unsigned planOffset=0;};
#include "TerrainPassPrep.h"
// Scoped to one captured terrain pass. Native, HUD and mirror passes can all
// change device state between calls. Night decals also override bias and Z writes.
struct PagedMaterialState {
 Material material{};
 bool valid=false,special=false,textured=false;
 void bind(unsigned hash,bool nightOverlay){
  const auto next=MaterialOf(hash);
  auto* texture=Texture(hash);
  const bool alpha64=hash==0x57FC91B7;
  if(valid&&special==alpha64&&textured==(texture!=nullptr)&&material.usage==next.usage&&
     material.blend==next.blend&&material.flags==next.flags&&material.tile==next.tile)
   device->SetTexture(0,texture);
  else SetMaterial(hash);
  material=next;special=alpha64;textured=texture!=nullptr;valid=!nightOverlay;
 }
};
static PagedDrawStats DrawPagedTerrain(Vec3 eye,const RenderFrustum& frustum,const D3DMATRIX& clip,bool environment,bool reflection,bool extended){
 TerrainPhaseTimer phaseTimer;
 PagedDrawStats stats;unsigned lastMaterial=0;bool materialSet=false,environmentSet=false,windowClipSet=false;std::array<unsigned,8> lastLights{};
 // The D3D state is scoped to this pass. The texture upload policy can be
 // different, including with the D3D11 scene used by LBx64 DLSS.
 const bool cacheMaterials=environment;
 const bool boundedLights=environment&&!envDepthPass&&envBoundedLights;
 TerrainLookup lookups;
 TerrainDeviceState bindings(device);
 const PageFrustumPackets packetFrustum(frustum.plane);
 static const bool auditPackets=PrepSwitch("MWLB_PACKET_AUDIT");
 const auto windowClip=EnvironmentWindowClip(clip);
 auto* fadeShader=extended&&environment&&terrainFadeMs?PagedVisibilityShader():nullptr;
 static unsigned fadeFrame=~0u;
 static DWORD fadeTime=0;
 if(fadeShader&&fadeFrame!=frameCounter){fadeFrame=frameCounter;fadeTime=GetTickCount();}
 auto* regularShader=environment?(envDepthPass?envDepthPS:envPS):nullptr;
 auto* opaqueDepthShader=environment&&envDepthPass&&sceneTargetFormat==D3DFMT_R32F&&
  !reflection&&!envRoadReflectionPass?PagedOpaqueDepthShader():nullptr;
 auto* boundShader=regularShader;
 float boundOpacity=1.f;
 const auto bindShader=[&](float opacity,bool opaqueDepth){
  if(opacity!=boundOpacity&&opacity!=1.f){const float value[4]{opacity};device->SetPixelShaderConstantF(96,value,1);}
  auto* shader=opaqueDepth?opaqueDepthShader:(opacity==1.f?regularShader:fadeShader);
  if(shader!=boundShader){device->SetPixelShader(shader);boundShader=shader;}
  boundOpacity=opacity;
 };
 // Evaluate section visibility once per view, rather than per triangle group.
 // Native render callbacks run on the render thread. Retain capacity across
 // views, while recomputing visibility and overlap ownership per view.
 static std::vector<PagedRenderCandidate> candidates;
 static std::vector<unsigned> ownerCache;
 candidates.clear();
 for(const auto& [id,residentSection]:v2ResidentSections){
  auto& section=*residentSection;
  if(!section.page||!section.vb)continue;
  const bool eligible=TerrainSectionVisible(id,section.header,eye,frustum,extended);
  bool authored=false;
#ifdef W2C_PUBLIC_BUILD
  authored=!eligible&&worldGeometryAddon.count(id)!=0;
#endif
  if(eligible||authored)candidates.push_back({id,&section,eligible,terrainPanoramaSections.count(id)!=0});
 }
 const auto eligible=[&](unsigned id){
  const auto it=std::lower_bound(candidates.begin(),candidates.end(),id,
   [](const PagedRenderCandidate& candidate,unsigned value){return candidate.id<value;});
  return it!=candidates.end()&&it->id==id&&it->eligible;
 };
 ownerCache.assign(overlapGroups.size(),~0u);
 auto& plan=pagedPrep;
 plan.start(candidates,frustum,eye,envRoadReflectionPass,extended&&environment);
 if(plan.active()&&!plan.overlapping()){
  if(plan.workers())plan.complete();
  else{for(size_t s=0;s<candidates.size();++s)plan.ready(s);plan.finish();}
 }
 const auto drawOwner=[&](unsigned id,unsigned group){
  if(!group||group>=overlapGroups.size())return group!=~0u;
  auto& owner=ownerCache[group];
  if(owner==~0u){owner=0;for(auto candidate:overlapGroups[group])if(eligible(candidate)){owner=candidate;break;}}
  return !owner||owner==id;
 };
 for(unsigned pass=0;pass<(envDepthPass?1u:2u);++pass)for(const auto& candidate:candidates){
  if(plan.overlapping())plan.ready(size_t(&candidate-candidates.data()));
  const auto id=candidate.id;
  auto& s=*candidate.section;
  const bool sectionEligible=candidate.eligible;
  const bool extendedDraw=extended&&!candidate.panorama;
  Mesh sectionBounds{};sectionBounds.minimum=s.header.minimum;sectionBounds.maximum=s.header.maximum;
  const unsigned sectionLights=boundedLights?EnvironmentDynamicMask(&sectionBounds):0;
  auto& page=*s.page;++stats.sections;PageGpuBlock* bound=nullptr,*batch=nullptr;unsigned first=0,count=0;
  auto flush=[&](){if(batch){if(bound!=batch){device->SetStreamSource(0,batch->vb,0,40);device->SetIndices(batch->ib);bound=batch;}device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,batch->vertices,first,count/3);++stats.calls;}batch=nullptr;};
  const auto& drawOrder=plan.order(size_t(&candidate-candidates.data()),pass,page);
  const auto& packets=page.bounds[pass];
  const bool packetPath=!plan.active()&&!packets.empty();
  unsigned laneMask=0;
  for(size_t at=0;at<drawOrder.size();++at){
   auto ordinal=drawOrder[at];auto& draw=page.draws[ordinal];
   if(packetPath){
    const unsigned lane=at%4;
    if(lane==0)laneMask=packetFrustum.visible(packets[at/4]);
    const bool kept=(laneMask&(1u<<lane))!=0;
    if(auditPackets){
     ++W2C_TerrainPrepStatusV1.checked;
     if(kept!=frustum.Visible(draw.minimum,draw.maximum))++W2C_TerrainPrepStatusV1.mismatches;
    }
    if(!kept&&!worldLogEnabled){
     if(!laneMask&&!auditPackets)at=std::min(at+(3-lane),drawOrder.size()-1);
     continue;
    }
   }
   if(!sectionEligible&&draw.owner!=0xFFFFFFFEu)continue;if(!drawOwner(id,draw.owner)){++stats.overlapSkipped;continue;}if(draw.texture==0xD2E09151||draw.texture==0x29D9478C)continue;
   const auto* prepared=plan.get(candidate.planOffset+ordinal);
   const bool visible=packetPath?(laneMask&(1u<<(at%4)))!=0:(prepared?prepared->visible:frustum.Visible(draw.minimum,draw.maximum));
   if(plan.validating()){
    ++W2C_TerrainPrepStatusV1.checked;
    if(visible!=frustum.Visible(draw.minimum,draw.maximum))++W2C_TerrainPrepStatusV1.mismatches;
   }
   if(!visible){++stats.frustumCulled;continue;}
   Header bounds{};bounds.minimum=draw.minimum;bounds.maximum=draw.maximum;
   const auto distance=prepared?prepared->distance:Distance(bounds,eye);
   if(plan.validating()&&distance!=Distance(bounds,eye))++W2C_TerrainPrepStatusV1.mismatches;
   if(extendedDraw&&distance>terrainForwardRange*terrainForwardRange){++stats.rangeSkipped;continue;}
   const bool nightOverlay=EnvironmentNightOverlayOf(draw.texture);
   if(nightOverlay&&(!envNight||envDepthPass)){++stats.nightSkipped;continue;}
   if(reflection){auto extent=Sub(draw.maximum,draw.minimum);Vec3 mid{(draw.minimum.x+draw.maximum.x)*.5f,(draw.minimum.y+draw.maximum.y)*.5f,(draw.minimum.z+draw.maximum.z)*.5f};auto delta=Sub(mid,eye);if(Dot(extent,extent)*.25f<Dot(delta,delta)/65536.f)continue;}
   if(draw.texture&&!Texture(draw.texture))continue;
   float opacity=1.f;
   // Keep authored panorama, AddOn overrides, and overlap ownership
   // transitions on their original path. Fade eligible ordinary terrain.
   if(fadeShader&&extendedDraw&&!draw.owner&&!nightOverlay){
    opacity=page.visibility[ordinal].sample(frameCounter,fadeTime,terrainFadeMs,
     !initialWorldHold&&distance>terrainSourceRange*terrainSourceRange);
    if(opacity==0.f)continue;
   }
   Mesh m{draw.texture,0,0,0,draw.indexCount,draw.minimum,draw.maximum};
   // Broad rejection is exact only for draws enclosed by the section AABB.
   const auto checkEnclosed=[&](){return draw.minimum.x>=s.header.minimum.x&&draw.minimum.y>=s.header.minimum.y&&draw.minimum.z>=s.header.minimum.z&&draw.maximum.x<=s.header.maximum.x&&draw.maximum.y<=s.header.maximum.y&&draw.maximum.z<=s.header.maximum.z;};
   unsigned lightMask=0;
   if(boundedLights){
    // An enclosed draw cannot intersect a light rejected by the section.
    // Authored draws extending outside it still need the full light set.
    if(prepared)lightMask=prepared->lights;
    else if(envActiveDynamicLights)lightMask=EnvironmentDynamicMask(&m,checkEnclosed()?sectionLights:~0u);
   }
   if(plan.validating()&&((prepared&&prepared->enclosed!=checkEnclosed())||
                         (boundedLights&&lightMask!=EnvironmentDynamicMask(&m))))++W2C_TerrainPrepStatusV1.mismatches;
   auto [begin,n]=page.drawSpans[ordinal];
   const auto material=opaqueDepthShader&&opacity==1.f?MaterialOf(draw.texture):Material{};
   const bool opaqueDepth=opaqueDepthShader&&opacity==1.f&&draw.texture!=0x57FC91B7&&
    material.usage==0&&material.blend==0&&!(material.flags&2);
   const auto& activeLights=envNight?draw.lights.night:draw.lights.day;
   for(unsigned j=begin;j<begin+n;++j){auto& span=page.spans[j];auto& block=page.blocks[span.chunk];bool same=materialSet&&lastMaterial==draw.texture;
    if(opaqueDepth){
     const bool alreadyBound=boundShader==opaqueDepthShader;
     if(alreadyBound&&batch==&block&&first+count==span.local&&count+span.count<=65535u*3u){
      count+=span.count;
      continue;
     }
     flush();
     if(!alreadyBound){
      bindShader(1.f,true);
      bindings.SetRenderState(D3DRS_DEPTHBIAS,0);
      bindings.SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
      bindings.SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
      bindings.SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
      bindings.SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
      bindings.SetRenderState(D3DRS_CULLMODE,D3DCULL_CW);
      materialSet=environmentSet=false;
     }
     batch=&block;first=span.local;count=span.count;
     continue;
    }
    bool merge=batch==&block&&first+count==span.local&&same&&opacity==boundOpacity&&(!environment||envDepthPass||(lastLights==activeLights&&(!boundedLights||lightMask==envDynamicMask)));
    if(merge){count+=span.count;continue;}flush();bindShader(opacity,false);
    if(!same){if(cacheMaterials)ApplyTerrainMaterial(bindings,draw.texture);else SetMaterial(draw.texture);if(!environment&&EnvironmentFrameOf(draw.texture))device->SetTexture(0,Texture(EnvironmentTexture(draw.texture)));
     const bool window=nightOverlay&&environment&&IsEnvironmentWindowLayer(draw.texture);
     if(window!=windowClipSet){device->SetVertexShaderConstantF(0,window?&windowClip._11:&clip._11,4);windowClipSet=window;}
     if(nightOverlay){
      // Keep facade/foreground occlusion and never let the decal write depth.
      // Retain the original offset for other night decals and fixed function.
      if(!window){
       constexpr float bias=-.000003f,slope=-1.f;DWORD bits,slopeBits;memcpy(&bits,&bias,4);memcpy(&slopeBits,&slope,4);
       if(cacheMaterials){bindings.SetRenderState(D3DRS_DEPTHBIAS,bits);bindings.SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,slopeBits);}
       else{device->SetRenderState(D3DRS_DEPTHBIAS,bits);device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,slopeBits);}
      }
      if(cacheMaterials)bindings.SetRenderState(D3DRS_ZWRITEENABLE,FALSE);else device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
     }
     lastMaterial=draw.texture;materialSet=true;
    }
    if(environment){if(!same||!environmentSet||lastLights!=activeLights){bindings.SetTexture(0,Texture(EnvironmentTexture(draw.texture)));SetEnvironmentMaterial(draw.texture,draw.lights,&m,lightMask,&bindings);lastLights=activeLights;environmentSet=true;}else SetEnvironmentDynamicLights(&m,lightMask);}
    batch=&block;first=span.local;count=span.count;
   }
   if(worldLogEnabled){
    const float distanceMeters=std::sqrt(distance);
    if(distance>terrainSourceRange*terrainSourceRange){++stats.farMeshes;if(!candidate.panorama)++stats.farRegularMeshes;}
    if(!candidate.panorama)stats.farthestRegular=std::max(stats.farthestRegular,distanceMeters);
    if(!TerrainSourceVisible(id))++stats.expandedMeshes;
    stats.farthest=std::max(stats.farthest,distanceMeters);
   }
   ++stats.meshes;if(nightOverlay)++stats.nightDrawn;
  }flush();
 }
 plan.finish();
 if(extended&&worldLogEnabled&&!envDepthPass){
  static DWORD reported=0;const auto now=GetTickCount();
  if(now-reported>2000){reported=now;
   Log("W2C_FORWARD_RENDER range_m=%u prefetch_m=%u selected_sections=%u panorama_sections=%u drawn_section_passes=%u meshes=%u far_meshes=%u far_regular_meshes=%u farthest_regular_m=%.1f expanded_pvs_meshes=%u farthest_box_m=%.1f range_culled=%u frustum_culled=%u duplicate_culled=%u zone=%u eye=%.3f,%.3f,%.3f",terrainForwardRange,terrainPrefetchRange,unsigned(terrainForwardSections.size()),unsigned(terrainPanoramaSections.size()),stats.sections,stats.meshes,stats.farMeshes,stats.farRegularMeshes,stats.farthestRegular,stats.expandedMeshes,stats.farthest,stats.rangeSkipped,stats.frustumCulled,stats.overlapSkipped,zoneIndex==~0u?0:zones[zoneIndex].id,eye.x,eye.y,eye.z);
  }
 }
 bindShader(1.f,false);
 if(windowClipSet)device->SetVertexShaderConstantF(0,&clip._11,4);
 return stats;
}
