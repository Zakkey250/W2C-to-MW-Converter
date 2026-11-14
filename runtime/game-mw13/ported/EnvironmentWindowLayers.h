#pragma once
// Authored transparent window-light textures, verified against exported DDS
// and world meshes. _N textures are normal maps, not night replacements.
static constexpr unsigned environmentWindowLayers[]={
 0x1AFB43F0,0x6A05D5E8,0x6F0B800B,0xA306C029,0xAA9F304D,
 0xB9597D42,0xB9598183,0xB95985C4,0xC5A29DC0,0xAD37C59D,
 0x06A486F0,0x1AFB3FAF,0x3D4DCB3F
};
static bool IsEnvironmentWindowLayer(unsigned hash){
 for(auto layer:environmentWindowLayers)if(layer==hash)return true;
 return false;
}
// For a shaded window move only z/w towards the eye, regardless of the
// slope of the facade. Leave clip X, Y and W (and therefore the UVs) alone.
static D3DMATRIX EnvironmentWindowClip(D3DMATRIX clip){
 for(unsigned row=0;row<4;++row)clip.m[row][2]-=.000003f*clip.m[row][3];
 return clip;
}
