#pragma once
// Converted bounds are in render axes, as is the same world matrix submitted
// with the model. Retain all eight corners for rotated or displaced props.
static void TransformRenderBounds(Vec3 low,Vec3 high,const D3DMATRIX& matrix,Vec3& minimum,Vec3& maximum){
 minimum={FLT_MAX,FLT_MAX,FLT_MAX};maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
 for(unsigned corner=0;corner<8;++corner){Vec3 p{corner&1?high.x:low.x,corner&2?high.y:low.y,corner&4?high.z:low.z};
  Vec3 q{p.x*matrix._11+p.y*matrix._21+p.z*matrix._31+matrix._41,p.x*matrix._12+p.y*matrix._22+p.z*matrix._32+matrix._42,p.x*matrix._13+p.y*matrix._23+p.z*matrix._33+matrix._43};
  minimum={std::min(minimum.x,q.x),std::min(minimum.y,q.y),std::min(minimum.z,q.z)};maximum={std::max(maximum.x,q.x),std::max(maximum.y,q.y),std::max(maximum.z,q.z)};
 }
}
