#pragma once
#include <cmath>
#include <algorithm>
namespace w2mw {
// A native barrier is a flat rectangle. Longitudinal road grade must not turn
// a driveable low kerb into a tall rectangle along its full projected edge.
// Exported kerbs retain an exactly vertical pair in each triangle. Unknown
// clipped/slanted geometry stays collidable rather than guessing its height.
template<class Triangle> inline bool IsLowKerb(const Triangle& t){
 for(unsigned a=0;a<3;++a)for(unsigned b=a+1;b<3;++b){
  const auto& p=t.p[a];const auto& q=t.p[b];
  if(std::abs(p.x-q.x)>.0001f||std::abs(p.z-q.z)>.0001f)continue;
  const float rise=std::abs(p.y-q.y);
  if(rise>.00001f&&rise<=.2501f)return true;
 }
 return false;
}
struct NativeWallPoint {float x,y,z;};
struct NativeWallGeometry {NativeWallPoint a{},b{};float bottom=0,top=0,lengthSquared=0;};
template<class Triangle> inline bool ClipNativeWall(const Triangle& t,float low,float high,NativeWallGeometry& result){
 if(IsLowKerb(t))return false;
 NativeWallPoint points[8]{},next[8]{};unsigned count=3;
 for(unsigned i=0;i<3;++i)points[i]={t.p[i].x,t.p[i].y,t.p[i].z};
 // Native barriers are rectangles, unlike the exact WMC triangles. Clip to
 // this contact query's vertical band before projecting: a high triangular
 // remainder above a removed opening must not become a full-height wall.
 for(unsigned pass=0;pass<2;++pass){unsigned n=0;
  for(unsigned i=0;i<count;++i){auto a=points[i],b=points[(i+1)%count];
   const float da=pass?high-a.y:a.y-low,db=pass?high-b.y:b.y-low;
   const bool ai=da>=0,bi=db>=0;
   if(ai)next[n++]=a;
   if(ai!=bi){const float f=da/(da-db);next[n++]={a.x+(b.x-a.x)*f,a.y+(b.y-a.y)*f,a.z+(b.z-a.z)*f};}
  }
  count=n;if(count<3)return false;for(unsigned i=0;i<count;++i)points[i]=next[i];
 }
 result.bottom=points[0].y;result.top=points[0].y;result.lengthSquared=0;
 for(unsigned i=0;i<count;++i){result.bottom=std::min(result.bottom,points[i].y);result.top=std::max(result.top,points[i].y);
  for(unsigned j=i+1;j<count;++j){const float x=points[i].x-points[j].x,z=points[i].z-points[j].z,length=x*x+z*z;
   if(length>result.lengthSquared){result.lengthSquared=length;result.a=points[i];result.b=points[j];}
  }
 }
 result.top-=.25f;return result.lengthSquared>1e-10f&&result.top>result.bottom;
}
}
