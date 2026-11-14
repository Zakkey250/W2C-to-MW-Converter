#pragma once
// MW shadow Pinfo is world-to-texture, including perspective divide. Recover
// D3D clip coordinates for rasterisation without applying the bias twice.
static D3DMATRIX ShadowTextureToRaster(const D3DMATRIX& textureClip){
 D3DMATRIX clip{};for(unsigned row=0;row<4;++row){clip.m[row][0]=2*textureClip.m[row][0]-textureClip.m[row][3];clip.m[row][1]=textureClip.m[row][3]-2*textureClip.m[row][1];clip.m[row][2]=textureClip.m[row][2];clip.m[row][3]=textureClip.m[row][3];}return clip;
}
