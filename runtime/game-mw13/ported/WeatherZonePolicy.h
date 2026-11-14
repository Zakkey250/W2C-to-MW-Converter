#pragma once
struct W2CWeatherZone {unsigned type=0;float elevation=0;std::vector<std::array<float,2>> polygon;};
static bool W2CWeatherGridBounds(const W2CWeatherZone& zone,int& loX,int& loY,int& hiX,int& hiY){
 if(zone.polygon.size()<3||zone.polygon.size()>128)return false;
 float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
 for(auto point:zone.polygon){if(!std::isfinite(point[0])||!std::isfinite(point[1])||fabs(point[0])>=100000||fabs(point[1])>=100000)return false;
  x0=std::min(x0,point[0]);y0=std::min(y0,point[1]);x1=std::max(x1,point[0]);y1=std::max(y1,point[1]);}
 loX=int(std::floor(x0/64));loY=int(std::floor(y0/64));hiX=int(std::floor(x1/64));hiY=int(std::floor(y1/64));
 return uint64_t(hiX-loX+1)*uint64_t(hiY-loY+1)<=4096;
}
static W2CWeatherCover W2CSourceWeatherCover(const std::vector<W2CWeatherZone>& zones,Vec3 point){
 W2CWeatherCover cover;float x=point.z,y=-point.x;
 for(const auto& zone:zones){
  if(zone.polygon.size()<3||point.y>zone.elevation)continue;
  bool inside=false;
  for(unsigned i=0,j=unsigned(zone.polygon.size()-1);i<zone.polygon.size();j=i++){
   auto a=zone.polygon[j],b=zone.polygon[i];
   if((a[1]>y)!=(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])inside=!inside;
  }
  if(inside){cover.roof=true;cover.tunnel|=zone.type==3;cover.roofHeight=std::max(cover.roofHeight,zone.elevation);}
 }
 return cover;
}
