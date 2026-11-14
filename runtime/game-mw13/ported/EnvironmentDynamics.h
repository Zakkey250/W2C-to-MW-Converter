#pragma once
// Offline-converted authored model/frame data. All I/O runs on the stream worker.
struct EnvironmentMotion {
 unsigned model=0,section=0,count=0,bits=0,scale=0;float rotation[3]{},amplitude=0;D3DMATRIX initial{};
 std::vector<D3DMATRIX> frames;
 float angle[3]{},phase[3]{};int sign[3]{1,1,1},hold[3]{};
 std::array<std::vector<float>,3> waves;
 D3DMATRIX current{};
};
struct EnvironmentModel {CpuSection cpu;Section gpu;};
static std::vector<EnvironmentMotion> envMotions;
static std::map<unsigned,EnvironmentModel> envModels;
static volatile LONG envMotionsReady=0;
static unsigned envMotionDraws=0;static float envMotionTime=-1,envMotionRemainder=0;
static D3DMATRIX EnvironmentIdentity(){D3DMATRIX m{};m._11=m._22=m._33=m._44=1;return m;}
static D3DMATRIX EnvironmentMultiply(const D3DMATRIX& a,const D3DMATRIX& b){D3DMATRIX out{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)out.m[i][j]+=a.m[i][k]*b.m[k][j];return out;}
static Vec3 EnvironmentTransform(Vec3 p,const D3DMATRIX& m){return {p.x*m._11+p.y*m._21+p.z*m._31+m._41,p.x*m._12+p.y*m._22+p.z*m._32+m._42,p.x*m._13+p.y*m._23+p.z*m._33+m._43};}
#include "SourceMotions.h"
static bool ReadEnvironmentModel(unsigned hash,EnvironmentModel& out){char name[100];sprintf_s(name,"Environment/Models/%08X.wma",hash);FILE* f=nullptr;fopen_s(&f,Path(name).c_str(),"rb");if(!f)return false;auto& c=out.cpu;auto& h=c.header;bool ok=fread(&h,48,1,f)==1&&h.magic==0x31414D57&&h.version==1&&h.section==hash&&h.vertices<1000000&&h.indices<3000000&&h.meshes<1000;
 if(ok){h.magic=Magic;c.meshes.resize(h.meshes);c.vertices.resize(h.vertices);c.normals.resize(h.vertices);c.indices.resize(h.indices);ok=fread(c.meshes.data(),44,h.meshes,f)==h.meshes;for(unsigned n=0;ok&&n<h.vertices;++n){EnvironmentVertex v;ok=fread(&v,36,1,f)==1;c.vertices[n]={v.position,v.color,v.u,v.v};c.normals[n]={v.normal,v.color};}ok=ok&&fread(c.indices.data(),4,h.indices,f)==h.indices&&fgetc(f)==EOF;}
 fclose(f);if(ok)for(auto& m:c.meshes){if(uint64_t(m.firstVertex)+m.vertexCount>h.vertices||uint64_t(m.firstIndex)+m.indexCount>h.indices||m.indexCount%3)return false;for(unsigned i=0;i<m.indexCount;++i)if(c.indices[m.firstIndex+i]>=m.vertexCount)return false;}if(ok){c.gpuVertices.resize(h.vertices);for(unsigned n=0;n<h.vertices;++n){const auto& v=c.vertices[n];const auto& nc=c.normals[n];c.gpuVertices[n]={v.position,nc.normal,nc.color,v.u,v.v,W2CSourceSunExposure(v.position)};}PrepareShadowIndices(c);PrepareMeshDrawLists(c);}return ok;
}
static void LoadEnvironmentDynamics(){if(sourceRuntime){LoadSourceEnvironmentDynamics();return;}FILE* f=nullptr;fopen_s(&f,Path("Environment/world-animations.bin").c_str(),"rb");if(!f)return;unsigned magic,count;if(fread(&magic,4,1,f)!=1||fread(&count,4,1,f)!=1||magic!=0x31444D57||count>1000){fclose(f);return;}bool ok=true;unsigned frameCount=0;
 for(unsigned n=0;ok&&n<count;++n){EnvironmentMotion m;ok=fread(&m.model,4,5,f)==5&&fread(m.rotation,4,3,f)==3&&fread(&m.amplitude,4,1,f)==1&&fread(&m.initial,64,1,f)==1&&m.count<100000&&m.scale<256;if(!ok)break;m.current=m.initial;if(m.count){m.frames.resize(m.count);ok=fread(m.frames.data(),64,m.count,f)==m.count;frameCount+=m.count;}
  for(unsigned axis=0;axis<3;++axis)if(m.rotation[axis]!=0&&m.amplitude!=0){unsigned length=unsigned(std::max(0.f,std::floor(2*std::abs(m.amplitude)/(m.rotation[axis]*.13333334f)+.5f)));if(length>10000){ok=false;break;}for(unsigned i=0;i<length;++i)m.waves[axis].push_back(std::sin(3.14159274f*i/length)*std::abs(m.amplitude));}
  envMotions.push_back(std::move(m));
 }
 ok=ok&&fgetc(f)==EOF;fclose(f);if(ok)for(auto& m:envMotions)if(!envModels.count(m.model)){EnvironmentModel model;if(!ReadEnvironmentModel(m.model,model)){ok=false;Log("ENVIRONMENT_MOTION_MODEL_REFUSED %08X",m.model);break;}envModels.emplace(m.model,std::move(model));}
 if(!ok){envMotions.clear();envModels.clear();Log("ENVIRONMENT_MOTIONS_REFUSED");return;}Log("ENVIRONMENT_MOTIONS_SOURCE instances=%u models=%u frames=%u",unsigned(envMotions.size()),unsigned(envModels.size()),frameCount);InterlockedExchange(&envMotionsReady,1);
}
static float EnvironmentPlaybackRate(unsigned packed){unsigned whole=packed>>4;if(!whole)whole=1;return whole+(packed&15)/15.f;}
static void TickEnvironmentMotions(){if(!envMotionsReady)return;float dt=envMotionTime<0?0:std::clamp(envWorldTime-envMotionTime,0.f,.25f);envMotionTime=envWorldTime;envMotionRemainder+=dt;unsigned steps=unsigned(envMotionRemainder*60);envMotionRemainder-=steps/60.f;
 for(auto& m:envMotions){float rate=EnvironmentPlaybackRate(m.scale);if(m.count){float frame=envWorldTime*15*rate;unsigned span=m.bits&0x3fffffffu;if(!span)span=m.count;if(m.bits&0x80000000)frame=std::fmod(frame,float(span));else frame=std::min(frame,float(span-1));if(m.bits&0x40000000){float ping=std::fmod(envWorldTime*15*rate,float(span*2));frame=ping<span?ping:span*2-ping-1;}unsigned a=std::min(unsigned(frame),m.count-1),b=std::min(a+1,m.count-1);float t=frame-a;for(unsigned i=0;i<16;++i)(&m.current._11)[i]=(&m.frames[a]._11)[i]*(1-t)+(&m.frames[b]._11)[i]*t;}
  else {for(unsigned step=0;step<steps;++step)for(unsigned axis=0;axis<3;++axis){auto& wave=m.waves[axis];if(wave.empty())m.angle[axis]=std::fmod(m.angle[axis]+m.rotation[axis]*rate/120.f,360.f);else if(m.hold[axis])--m.hold[axis];else {m.phase[axis]+=rate/120.f;unsigned i=unsigned(m.phase[axis]*30);if(i>=wave.size()){i-=unsigned(wave.size());m.phase[axis]=0;m.sign[axis]=-m.sign[axis];}if(i==wave.size()/2)m.hold[axis]=10;m.angle[axis]=wave[std::min(i,unsigned(wave.size()-1))]*m.sign[axis];}}
   auto rotate=EnvironmentIdentity();for(unsigned axis=0;axis<3;++axis){float a=m.angle[axis]*.01745329252f;auto r=EnvironmentIdentity();unsigned x=(axis+1)%3,y=(axis+2)%3;r.m[x][x]=r.m[y][y]=std::cos(a);r.m[x][y]=std::sin(a);r.m[y][x]=-std::sin(a);rotate=EnvironmentMultiply(rotate,r);}m.current=EnvironmentMultiply(rotate,m.initial);
  }m.current._41-=6016;m.current._42+=546;
 }
}
static void DrawEnvironmentMotions(const D3DMATRIX& clip,bool shaded){envMotionDraws=0;if(!envMotionsReady)return;EnvironmentMeshLights noLights;noLights.day.fill(~0u);noLights.night.fill(~0u);
 for(auto& motion:envMotions){if(motion.section!=26000&&zoneIndex!=~0u&&!zones[zoneIndex].sections.count(motion.section))continue;auto& model=envModels[motion.model];auto& h=model.cpu.header;Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};for(unsigned i=0;i<8;++i){auto p=EnvironmentTransform({i&1?h.minimum.x:h.maximum.x,i&2?h.minimum.y:h.maximum.y,i&4?h.minimum.z:h.maximum.z},motion.current);lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}if(!VisibleBox(lo,hi,clip))continue;
  if(!model.gpu.vb){if(!sceneUploadBudget.Available(Clock()))continue;if(!Upload(model.cpu,model.gpu))continue;model.cpu.vertices.clear();model.cpu.normals.clear();model.cpu.indices.clear();model.cpu.gpuVertices.clear();model.cpu.shadowIndices.clear();}
  if(shaded)device->SetVertexShaderConstantF(5,&motion.current._11,4);else device->SetTransform(D3DTS_WORLD,&motion.current);
  device->SetStreamSource(0,model.gpu.vb,0,sizeof(EnvironmentVertex));device->SetIndices(model.gpu.ib);
  for(auto& m:model.gpu.meshes){if(envDepthPass&&MaterialOf(m.texture).blend)continue;if(envNightOverlays.count(m.texture)&&!envNight)continue;if(m.texture&&!Texture(m.texture))continue;SetMaterial(m.texture);device->SetTexture(0,Texture(EnvironmentTexture(m.texture)));if(shaded)SetEnvironmentMaterial(m.texture,noLights);device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,m.firstVertex,0,m.vertexCount,m.firstIndex,m.indexCount/3);}++envMotionDraws;
 }auto identity=EnvironmentIdentity();if(shaded)device->SetVertexShaderConstantF(5,&identity._11,4);else device->SetTransform(D3DTS_WORLD,&identity);
}
