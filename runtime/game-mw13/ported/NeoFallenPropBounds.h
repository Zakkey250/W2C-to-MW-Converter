#pragma once
#include <algorithm>
#include <cmath>
#include "../integration/MWNeoObstacles.h"
namespace mwneo_obstacles {
inline bool FallenVolume(float mass,const float* position,const float* velocity,const float* dimensions,const float* rotation,
 float originalHeight,unsigned owner,Volume& result){
 if(!position||!velocity||!dimensions||!rotation||!std::isfinite(originalHeight)||!std::isfinite(mass)||mass<5000||mass>1e7f)return false;
 for(unsigned i=0;i<3;++i)if(!std::isfinite(position[i])||!std::isfinite(velocity[i])||!std::isfinite(dimensions[i])||dimensions[i]<.05f||dimensions[i]>50)return false;
 if(velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2]>4||position[1]>=originalHeight-2)return false;
 for(unsigned i=0;i<16;++i)if(!std::isfinite(rotation[i]))return false;
 for(unsigned i=0;i<3;++i){float norm=0;for(unsigned j=0;j<3;++j)norm+=rotation[i*4+j]*rotation[i*4+j];if(norm<.9f||norm>1.1f)return false;}
 result={};result.owner=owner;result.mass=mass;std::copy(position,position+3,result.position);
 result.halfWidth=std::abs(rotation[0])*dimensions[0]+std::abs(rotation[4])*dimensions[1]+std::abs(rotation[8])*dimensions[2];
 result.halfHeight=std::abs(rotation[1])*dimensions[0]+std::abs(rotation[5])*dimensions[1]+std::abs(rotation[9])*dimensions[2];
 result.halfLength=std::abs(rotation[2])*dimensions[0]+std::abs(rotation[6])*dimensions[1]+std::abs(rotation[10])*dimensions[2];
 return result.halfWidth>=.5f&&result.halfLength>=.5f;
}
}
