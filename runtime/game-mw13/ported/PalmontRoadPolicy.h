#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
namespace palmont_road {
struct Segment {float ax,ay,az,bx,by,bz;uint32_t segment,voice,area;};
static_assert(sizeof(Segment)==36);
struct Match {bool found=false;uint32_t segment=0,voice=0,area=0;float distance=0;};
// Query only on a location-report request, never once per rendered frame.
inline Match Locate(const std::vector<Segment>& roads,float x,float y,float z){
 Match result;float best=35.f*35.f;
 if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return result;
 for(const auto& s:roads){
  if(x<std::fmin(s.ax,s.bx)-35||x>std::fmax(s.ax,s.bx)+35||z<std::fmin(s.az,s.bz)-35||z>std::fmax(s.az,s.bz)+35)continue;
  float dx=s.bx-s.ax,dz=s.bz-s.az,den=dx*dx+dz*dz;
  float t=den>0?((x-s.ax)*dx+(z-s.az)*dz)/den:0;t=std::fmax(0.f,std::fmin(1.f,t));
  if(std::fabs(y-(s.ay+t*(s.by-s.ay)))>12)continue;
  dx=x-(s.ax+t*dx);dz=z-(s.az+t*dz);float d=dx*dx+dz*dz;
  if(d<best){best=d;result={true,s.segment,s.voice,s.area,std::sqrt(d)};}
 }
 return result;
}
}
