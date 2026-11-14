#pragma once
struct W2CWeatherCover {bool roof=false,tunnel=false;float roofHeight=0;};
static W2CWeatherCover W2CWorldWeatherCover(const CollisionWorld& world,Vec3 eye){
 W2CWeatherCover cover;
 if(world.nodes.empty()||!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z))return cover;
 RayHit roof;
 if(!world.Ray({eye.x,eye.y+.2f,eye.z},{eye.x,world.nodes[0].maximum.y+1,eye.z},roof))return cover;
 cover.roof=true;cover.roofHeight=roof.point.y;
 unsigned enclosed=0;
 for(Vec3 direction:{Vec3{12,0,0},Vec3{-12,0,0},Vec3{0,0,12},Vec3{0,0,-12}}){RayHit wall;
  if(world.Ray(eye,{eye.x+direction.x,eye.y,eye.z+direction.z},wall,false,true))++enclosed;}
 cover.tunnel=enclosed>=2;return cover;
}
