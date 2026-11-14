#pragma once
struct PagedDrawStats {unsigned sections=0,meshes=0,calls=0,nightDrawn=0,nightSkipped=0,overlapSkipped=0;};
static PagedDrawStats DrawPagedTerrain(Vec3 eye,const RenderFrustum& frustum,const D3DMATRIX& clip,bool environment,bool reflection){
 PagedDrawStats stats;unsigned lastMaterial=0;bool materialSet=false,environmentSet=false,windowClipSet=false;EnvironmentMeshLights lastLights{};
 const auto windowClip=EnvironmentWindowClip(clip);
 for(unsigned pass=0;pass<(envDepthPass?1u:2u);++pass)for(auto& [id,s]:sections){
  if(!s.page||!s.vb||Distance(s.header,eye)>650.f*650.f||(zoneIndex!=~0u&&id!=26000&&!zones[zoneIndex].sections.count(id)))continue;
  auto& page=*s.page;++stats.sections;PageGpuBlock* bound=nullptr,*batch=nullptr;unsigned first=0,count=0;
  auto flush=[&](){if(batch){if(bound!=batch){device->SetStreamSource(0,batch->vb,0,40);device->SetIndices(batch->ib);bound=batch;}device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,batch->vertices,first,count/3);++stats.calls;}batch=nullptr;};
  for(auto ordinal:page.order[pass]){auto& draw=page.draws[ordinal];if(!DrawOverlapOwner(id,draw.owner,eye)){++stats.overlapSkipped;continue;}if(draw.texture==0xD2E09151||draw.texture==0x29D9478C||!frustum.Visible(draw.minimum,draw.maximum))continue;
   const bool nightOverlay=envNightOverlays.count(draw.texture)!=0;
   if(nightOverlay&&(!envNight||envDepthPass)){++stats.nightSkipped;continue;}
   if(reflection){auto extent=Sub(draw.maximum,draw.minimum);Vec3 mid{(draw.minimum.x+draw.maximum.x)*.5f,(draw.minimum.y+draw.maximum.y)*.5f,(draw.minimum.z+draw.maximum.z)*.5f};auto delta=Sub(mid,eye);if(Dot(extent,extent)*.25f<Dot(delta,delta)/65536.f)continue;}
   if(draw.texture&&!Texture(draw.texture))continue;
   Mesh m{draw.texture,0,0,0,draw.indexCount,draw.minimum,draw.maximum};auto [begin,n]=page.drawSpans[ordinal];
   for(unsigned j=begin;j<begin+n;++j){auto& span=page.spans[j];auto& block=page.blocks[span.chunk];bool same=materialSet&&lastMaterial==draw.texture;
    bool merge=batch==&block&&first+count==span.local&&same&&(!environment||envDepthPass||(!memcmp(&lastLights,&draw.lights,sizeof(lastLights))&&(!envBoundedLights||EnvironmentDynamicMask(&m)==envDynamicMask)));
    if(merge){count+=span.count;continue;}flush();
    if(!same){SetMaterial(draw.texture);if(!environment&&envFrames.count(draw.texture))device->SetTexture(0,Texture(EnvironmentTexture(draw.texture)));
     const bool window=nightOverlay&&environment&&IsEnvironmentWindowLayer(draw.texture);
     if(window!=windowClipSet){device->SetVertexShaderConstantF(0,window?&windowClip._11:&clip._11,4);windowClipSet=window;}
     if(nightOverlay){
      // Keep facade/foreground occlusion and never let the decal write depth.
      // Retain the original offset for other night decals and fixed function.
      if(!window){
       constexpr float bias=-.000003f,slope=-1.f;DWORD bits,slopeBits;memcpy(&bits,&bias,4);memcpy(&slopeBits,&slope,4);
       device->SetRenderState(D3DRS_DEPTHBIAS,bits);device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,slopeBits);
      }
      device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
     }
     lastMaterial=draw.texture;materialSet=true;
    }
    if(environment){if(!same||!environmentSet||memcmp(&lastLights,&draw.lights,sizeof(lastLights))){device->SetTexture(0,Texture(EnvironmentTexture(draw.texture)));SetEnvironmentMaterial(draw.texture,draw.lights,&m);lastLights=draw.lights;environmentSet=true;}else SetEnvironmentDynamicLights(&m);}
    batch=&block;first=span.local;count=span.count;
   }
   ++stats.meshes;if(nightOverlay)++stats.nightDrawn;
  }flush();
 }
 if(windowClipSet)device->SetVertexShaderConstantF(0,&clip._11,4);
 return stats;
}
