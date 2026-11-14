#pragma once
// Extract once per pass; all boxes retain the original six-plane test.
struct RenderFrustum {
 float plane[6][4];
 explicit RenderFrustum(const D3DMATRIX& matrix){for(unsigned a=0;a<4;++a){plane[0][a]=matrix.m[a][3]+matrix.m[a][0];plane[1][a]=matrix.m[a][3]-matrix.m[a][0];plane[2][a]=matrix.m[a][3]+matrix.m[a][1];plane[3][a]=matrix.m[a][3]-matrix.m[a][1];plane[4][a]=matrix.m[a][2];plane[5][a]=matrix.m[a][3]-matrix.m[a][2];}}
 bool Visible(Vec3 low,Vec3 high)const{for(auto& p:plane){float x=p[0]>=0?high.x:low.x,y=p[1]>=0?high.y:low.y,z=p[2]>=0?high.z:low.z;if(p[0]*x+p[1]*y+p[2]*z+p[3]<0)return false;}return true;}
};
