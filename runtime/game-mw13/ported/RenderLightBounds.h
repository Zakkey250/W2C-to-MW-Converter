#pragma once
// A sphere cannot light a mesh whose conservative AABB is outside its range.
// Invalid metadata takes the conservative path; no cone/occlusion approximation.
static float RenderLightRangeSquared(float inverseRange){
 if(!std::isfinite(inverseRange)||inverseRange<=0)return -1.f;
 const float range=1.f/inverseRange;
 return range*range;
}
static bool RenderLightIntersectsSquared(Vec3 position,float rangeSquared,Vec3 low,Vec3 high){
 if(rangeSquared<0.f)return true;
 const float x=std::max({low.x-position.x,0.f,position.x-high.x});
 const float y=std::max({low.y-position.y,0.f,position.y-high.y});
 const float z=std::max({low.z-position.z,0.f,position.z-high.z});
 return x*x+y*y+z*z<=rangeSquared;
}
static bool RenderLightIntersects(Vec3 position,float inverseRange,Vec3 low,Vec3 high){
 return RenderLightIntersectsSquared(position,RenderLightRangeSquared(inverseRange),low,high);
}
