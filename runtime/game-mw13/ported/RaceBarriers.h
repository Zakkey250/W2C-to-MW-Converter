#pragma once
#include "../integration/MWBarrierSpatialIndex.h"
// Main-authored placement, source mesh and native group activation. No walls
// are enabled by proximity alone and no course is inferred from road geometry.
struct RaceBarrier {unsigned section,index,model,flags,group,key;CpuSection cpu;std::vector<Vertex> reversed;Vec3 center,a,b;};
struct RaceBarrierScroll {int type,step,s,t;};
static std::map<unsigned,RaceBarrierScroll> raceBarrierScrolls;
static std::vector<RaceBarrier> raceBarriers;
static mw_barrier_index::Index raceBarrierContactIndex;
static std::set<unsigned> raceBarrierActiveKeys;
static bool raceBarriersLoaded=false;
static bool RaceBarrierEnabled(const RaceBarrier& barrier){
 return (*reinterpret_cast<BYTE*>(0x902160+barrier.group)&1)!=0||raceBarrierActiveKeys.count(barrier.key)!=0;
}
static void RefreshRaceBarrierActivation(){
 raceBarrierActiveKeys.clear();if(editorStock)return;unsigned count=*reinterpret_cast<unsigned*>(0x9AA540+0x484);
 auto rows=*reinterpret_cast<BYTE**>(0x9AA540+0x488);if(count>4096||!rows)return;
 for(unsigned n=0;n<count;n++){auto r=rows+n*24;if(r[16])raceBarrierActiveKeys.insert(*reinterpret_cast<unsigned*>(r+20));}
}
static void RaceBarrierContacts(Vec3 center,float radius,std::vector<NativeBarrier>& walls){
 if(!raceBarriersLoaded)return;RefreshRaceBarrierActivation();
 static thread_local std::vector<unsigned> candidates;const bool indexed=raceBarrierContactIndex.Query(center.x,center.z,radius,candidates);
 for(unsigned n=0;n<(indexed?candidates.size():raceBarriers.size());++n){const auto& b=raceBarriers[indexed?candidates[n]:n];if(!RaceBarrierEnabled(b)||(*reinterpret_cast<BYTE*>(0x902160+b.group)&4))continue;
  // Render axes (x,y,z-up) -> native physics (negative-y,z,x).
  Vec3 a{-b.a.y,b.cpu.header.minimum.z,b.a.x},end{-b.b.y,b.cpu.header.maximum.z,b.b.x};
  float dx=end.x-a.x,dz=end.z-a.z,length=dx*dx+dz*dz;if(length<.001f)continue;
  float t=std::clamp(((center.x-a.x)*dx+(center.z-a.z)*dz)/length,0.f,1.f);
  float x=a.x+t*dx-center.x,z=a.z+t*dz-center.z;
  if(x*x+z*z>radius*radius||center.y+4<a.y||center.y-4>end.y)continue;
  walls.push_back({a,512,end,1/std::sqrt(length),{0,0}});
 }
}
#include "SourceBarriers.h"
static void LoadRaceBarriers(){
 if(sourceRuntime){LoadSourceRaceBarriers();return;}
 FILE *f=nullptr,*g=nullptr;fopen_s(&f,Path("Environment/RaceBarriers/barrier-meshes.bin").c_str(),"rb");
 fopen_s(&g,Path("Environment/RaceBarriers/barrier-groups.bin").c_str(),"rb");
 if(!f||!g){if(f)fclose(f);if(g)fclose(g);Log("RACE_BARRIER_PACK_MISSING");return;}
 unsigned mh[2]{},gh[2]{};bool ok=fread(mh,4,2,f)==2&&fread(gh,4,2,g)==2&&mh[0]==0x31424D57&&gh[0]==0x31474D57&&mh[1]>0&&mh[1]<=50000&&gh[1]==mh[1];
 std::vector<RaceBarrier> staged;staged.reserve(ok?mh[1]:0);
 for(unsigned n=0;ok&&n<mh[1];n++){
  RaceBarrier b{};unsigned bind[5]{};auto& c=b.cpu;auto& h=c.header;
  ok=fread(&b.section,4,4,f)==4&&fread(&h.meshes,4,3,f)==3&&fread(&h.minimum,12,2,f)==2&&fread(bind,4,5,g)==5;
  if(!ok||bind[0]!=b.section||bind[1]!=b.index||bind[2]>=4096||h.meshes>16||h.vertices>1024||h.indices>4096){ok=false;break;}
  b.group=bind[2];b.key=bind[3];h.magic=Magic;h.version=Version;h.section=b.model;
  c.meshes.resize(h.meshes);c.vertices.resize(h.vertices);c.indices.resize(h.indices);
  ok=fread(c.meshes.data(),sizeof(Mesh),h.meshes,f)==h.meshes&&fread(c.vertices.data(),sizeof(Vertex),h.vertices,f)==h.vertices&&fread(c.indices.data(),4,h.indices,f)==h.indices;
  for(auto& m:c.meshes){if(uint64_t(m.firstVertex)+m.vertexCount>h.vertices||uint64_t(m.firstIndex)+m.indexCount>h.indices||m.indexCount%3){ok=false;break;}for(unsigned i=0;i<m.indexCount;i++)if(c.indices[m.firstIndex+i]>=m.vertexCount)ok=false;}
  b.center={(h.minimum.x+h.maximum.x)*.5f,(h.minimum.y+h.maximum.y)*.5f,(h.minimum.z+h.maximum.z)*.5f};
  float longest=-1;for(auto& a:c.vertices)for(auto& end:c.vertices){float x=end.position.x-a.position.x,y=end.position.y-a.position.y,length=x*x+y*y;if(length>longest){longest=length;b.a=a.position;b.b=end.position;}}
  staged.push_back(std::move(b));
 }
 ok=ok&&fgetc(f)==EOF&&fgetc(g)==EOF;fclose(f);fclose(g);
 if(!ok){Log("RACE_BARRIER_PACK_REFUSED");return;}
 fopen_s(&f,Path("Environment/RaceBarriers/barrier-reverse-vertices.bin").c_str(),"rb");
 unsigned reverse[2]{};ok=f&&fread(reverse,4,2,f)==2&&reverse[0]==0x32524D57&&reverse[1]==staged.size();
 for(auto& b:staged){if(!ok)break;unsigned h[3]{};ok=fread(h,4,3,f)==3&&h[0]==b.section&&h[1]==b.index&&h[2]==b.cpu.vertices.size();if(!ok)break;
  b.reversed=b.cpu.vertices;for(auto& v:b.reversed){if(fread(&v.position,12,1,f)!=1||!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||!std::isfinite(v.position.z)){ok=false;break;}
   auto& low=b.cpu.header.minimum;auto& high=b.cpu.header.maximum;low={std::min(low.x,v.position.x),std::min(low.y,v.position.y),std::min(low.z,v.position.z)};high={std::max(high.x,v.position.x),std::max(high.y,v.position.y),std::max(high.z,v.position.z)};
  }
 }
 if(f){ok=ok&&fgetc(f)==EOF;fclose(f);}if(!ok){Log("RACE_BARRIER_PACK_REFUSED reverse geometry");return;}
 fopen_s(&f,Path("Environment/RaceBarriers/materials.txt").c_str(),"r");if(f){unsigned hash;Material m;while(fscanf_s(f,"%x %u %u %u %u",&hash,&m.usage,&m.blend,&m.flags,&m.tile)==5)materials[hash]=m;fclose(f);}
 fopen_s(&f,Path("Environment/RaceBarriers/scrolls.txt").c_str(),"r");if(f){unsigned hash;RaceBarrierScroll s;while(fscanf_s(f,"%x %d %d %d %d",&hash,&s.type,&s.step,&s.s,&s.t)==5)raceBarrierScrolls[hash]=s;fclose(f);}
 if(raceBarrierScrolls.empty()||raceBarrierScrolls.size()>256){Log("RACE_BARRIER_PACK_REFUSED scroll metadata");return;}
 for(const auto& b:staged)for(const auto& m:b.cpu.meshes)if(!raceBarrierScrolls.count(m.texture)){Log("RACE_BARRIER_PACK_REFUSED missing texture scroll metadata");return;}
 raceBarriers=std::move(staged);
 raceBarrierContactIndex.Build(raceBarriers.size(),[](unsigned i){const auto& b=raceBarriers[i];return mw_barrier_index::Bounds{std::min(-b.a.y,-b.b.y),std::min(b.a.x,b.b.x),std::max(-b.a.y,-b.b.y),std::max(b.a.x,b.b.x)};});
 raceBarriersLoaded=true;eventBarrierCollision=RaceBarrierContacts;
 for(auto& s:raceBarrierScrolls)Texture(s.first);
 Log("RACE_BARRIER_PACK_ACTIVE placements=%u source=target_game native_group_gating=1 collision=1 native_reverse=1 native_scroll=1",unsigned(raceBarriers.size()));
}
static void DrawRaceBarriers(const D3DMATRIX& clip,Vec3 eye,bool mainView){
 if(!raceBarriersLoaded)return;RefreshRaceBarrierActivation();unsigned visible=0,active=0,mirrored=0,pending=0;
 device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);
 auto identity=EnvironmentIdentity();device->SetTransform(D3DTS_WORLD,&identity);
 for(auto& b:raceBarriers){if(!RaceBarrierEnabled(b))continue;++active;
  auto delta=Sub(b.center,eye);if(Dot(delta,delta)>650.f*650.f||!VisibleBox(b.cpu.header.minimum,b.cpu.header.maximum,clip))continue;
  bool flip=(b.flags&0x800000)&&(*reinterpret_cast<BYTE*>(0x902160+b.group)&2);if(flip)++mirrored;
  const auto& vertices=flip?b.reversed:b.cpu.vertices;
  // Each authored barrier has only two quads. Immediate submission avoids
  // thousands of tiny managed-buffer allocations while retaining source UVs.
  for(auto& m:b.cpu.meshes){auto tex=Texture(m.texture);if(!tex){++pending;continue;}SetMaterial(m.texture);
   const auto& mat=MaterialOf(m.texture);device->SetRenderState(D3DRS_CULLMODE,(mat.flags&2)?D3DCULL_NONE:(flip?D3DCULL_CCW:D3DCULL_CW));
   // Stock course walls use this separate W2C renderer. Apply the requested
   // threefold wall animation rate without accelerating world/weather clocks.
   auto scroll=raceBarrierScrolls[m.texture];float time=envWorldTime*3.f;if(scroll.type==2&&scroll.step>0)time=std::floor(time/(scroll.step/256.f));
   auto uv=EnvironmentIdentity();if(scroll.type){uv._31=std::fmod(time*scroll.s/4096.f,1.f);uv._32=std::fmod(time*scroll.t/4096.f,1.f);}
   device->SetTransform(D3DTS_TEXTURE0,&uv);device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2);device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
   device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,m.vertexCount,m.indexCount/3,b.cpu.indices.data()+m.firstIndex,D3DFMT_INDEX32,vertices.data()+m.firstVertex,sizeof(Vertex));
  }++visible;
 }
 if(mainView){static DWORD last=0;DWORD now=GetTickCount();if(now-last>3000){last=now;Log("RACE_BARRIERS active=%u visible=%u reversed_visible=%u pending_meshes=%u native_enabled_keys=%u world_time=%.3f wall_animation_rate=3",active,visible,mirrored,pending,unsigned(raceBarrierActiveKeys.size()),envWorldTime);}}
}

