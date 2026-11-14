#pragma once
#include <cstdint>
#include "PackFormat.h"
static w2mw::Vec3 WorldLoadCenterFromNative(const float* raw){return {raw[2],-raw[0],raw[1]};}
struct WorldLoadReadiness {
 unsigned sections=0,missingSections=0,textures=0,missingTextures=0,props=0,missingProps=0,refusedProps=0;
 bool collision=false,center=false,shaders=true;
 bool Complete()const{return center&&collision&&shaders&&sections&&!missingSections&&!missingTextures&&!missingProps;}
};
struct WorldLoadStable {
 bool centerSeen=false,readySeen=false;
 std::uint32_t centerSince=0,readySince=0;
 void Reset(){*this={};}
 void Moved(std::uint32_t now){centerSeen=true;centerSince=now;readySeen=false;}
 bool Update(std::uint32_t now,const WorldLoadReadiness& r){
  if(!centerSeen||now-centerSince<500||!r.Complete()){readySeen=false;return false;}
  if(!readySeen){readySeen=true;readySince=now;}
  return now-readySince>=100;
 }
};
