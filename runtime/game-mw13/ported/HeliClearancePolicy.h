#pragma once
// Helicopter altitude uses its own HeliSheet, not car ground contacts.
// Highest world collision surface in the rotor/corridor footprint supplies
// a W2C-only minimum altitude; it does not change pursuit targets or physics.
static bool W2CHeliClippedHeight(const Vec3* triangle,Vec3 low,Vec3 high,float& highest){
 // A large sloped triangle can have its tallest vertex far outside the
 // rotor corridor. Clip its actual surface to the footprint before choosing
 // an altitude; a BVH/AABB overlap alone does not prove a surface is inside.
 std::vector<Vec3> polygon(triangle,triangle+3),next;
 for(unsigned edge=0;edge<4&&!polygon.empty();++edge){
  const bool xAxis=edge<2,minimum=(edge%2)==0;float bound=xAxis?(minimum?low.x:high.x):(minimum?low.z:high.z);
  auto coordinate=[&](Vec3 p){return xAxis?p.x:p.z;};
  auto inside=[&](Vec3 p){return minimum?coordinate(p)>=bound:coordinate(p)<=bound;};
  next.clear();Vec3 previous=polygon.back();bool previousInside=inside(previous);
  for(Vec3 current:polygon){bool currentInside=inside(current);
   if(currentInside!=previousInside){float t=(bound-coordinate(previous))/(coordinate(current)-coordinate(previous));
    next.push_back({previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t,previous.z+(current.z-previous.z)*t});}
   if(currentInside)next.push_back(current);previous=current;previousInside=currentInside;
  }polygon.swap(next);
 }
 if(polygon.empty())return false;
 for(Vec3 point:polygon)highest=std::max(highest,point.y);return true;
}
static bool W2CHeliWorldAltitude(const CollisionWorld& world,Vec3 position,Vec3 target,float& altitude){
 if(world.nodes.empty())return false;
 for(float v:{position.x,position.y,position.z,target.x,target.y,target.z})if(!std::isfinite(v)||fabs(v)>100000)return false;
 float distance=std::hypot(target.x-position.x,target.z-position.z);
 // Bound look-ahead from the helicopter, rather than dropping the beginning
 // of a long corridor near a building it is currently approaching.
 if(distance>250){float t=250/distance;target.x=position.x+(target.x-position.x)*t;target.z=position.z+(target.z-position.z)*t;distance=250;}
 unsigned steps=std::max(1u,unsigned(std::ceil(distance/15)));float highest=-1e30f;bool found=false;
 std::vector<unsigned> candidates;
 for(unsigned i=0;i<steps;++i){float a=float(i)/steps,b=float(i+1)/steps;
  float x0=position.x+(target.x-position.x)*a,z0=position.z+(target.z-position.z)*a;
  float x1=position.x+(target.x-position.x)*b,z1=position.z+(target.z-position.z)*b;
  // Cover every point of the swept rotor footprint. Sparse vertical rays
  // missed thin roofs and facades between samples.
  Vec3 low{std::min(x0,x1)-8,world.nodes[0].minimum.y-1,std::min(z0,z1)-8};
  Vec3 high{std::max(x0,x1)+8,world.nodes[0].maximum.y+1,std::max(z0,z1)+8};
  world.QueryBox(low,high,candidates,false);
  for(auto index:candidates){const auto& triangle=world.triangles[index];
   found=W2CHeliClippedHeight(triangle.p,low,high,highest)||found;}
 }if(found)altitude=std::max(target.y,highest+8);return found;
}
