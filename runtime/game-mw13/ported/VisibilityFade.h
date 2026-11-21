#pragma once
#include <cstdint>

struct VisibilityFade {
 std::uint32_t frame=0;
 std::uint32_t start=0;

 float sample(std::uint32_t currentFrame,std::uint32_t now,unsigned duration,bool inExtendedRange){
  if(!inExtendedRange){
   frame=currentFrame;
   start=now-duration;
   return 1.f;
  }
  // Depth and colour use the same frame stamp. A single missed frame
  // starts a new transition; time subtraction is valid across tick wrap.
  if(!frame||currentFrame-frame>1)start=now;
  frame=currentFrame;
  const auto elapsed=now-start;
  if(elapsed>=duration)return 1.f;
  const float t=float(elapsed)/float(duration);
  return t*t*(3.f-2.f*t);
 }
};
