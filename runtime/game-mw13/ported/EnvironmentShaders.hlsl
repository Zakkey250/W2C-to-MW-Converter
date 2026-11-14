row_major float4x4 Clip : register(c0);
float4 UV : register(c4);
row_major float4x4 World : register(c5);
row_major float4x4 View : register(c9);
struct VI {float3 p:POSITION;float3 n:NORMAL;float4 c:COLOR;float2 uv:TEXCOORD;float exposure:TEXCOORD1;};
struct PO {float4 p:POSITION;float3 world:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float4 reflection:TEXCOORD3;float exposure:TEXCOORD4;float4 c:COLOR;};
PO VS(VI i){PO o;float4 world=mul(float4(i.p,1),World);o.p=mul(world,Clip);o.reflection=o.p;o.reflection.y=-o.reflection.y;o.reflection.xy=(o.reflection.xy+o.reflection.w)*.5;o.world=world.xyz;o.n=mul(float4(i.n,0),World).xyz;o.c=i.c;o.uv=i.uv*UV.zw+UV.xy;o.exposure=i.exposure;return o;}
struct DO {float4 p:POSITION;float2 uv:TEXCOORD0;float depth:TEXCOORD1;float4 c:COLOR;};
struct SO {float4 p:POSITION;float2 uv:TEXCOORD0;float4 c:COLOR;};
SO VSShadow(VI i){SO o;o.p=mul(mul(float4(i.p,1),World),Clip);o.uv=i.uv*UV.zw+UV.xy;o.c=i.c;return o;}
DO VSDepth(VI i){DO o;float4 w=mul(float4(i.p,1),World);o.p=mul(w,Clip);o.depth=mul(w,View).z;o.uv=i.uv*UV.zw+UV.xy;o.c=i.c;return o;}
sampler2D Base:register(s0);sampler2D Mask:register(s1);
sampler2D RoadReflection:register(s2);sampler2D RoadMask:register(s3);sampler2D RoadDrops:register(s4);
sampler2D RoadDetail:register(s5);
float4 Diffuse:register(c0);float4 Ambient:register(c1);float4 Sun:register(c2);float4 Fog:register(c3);float4 FogSun:register(c4);float4 FogValue:register(c5);float4 Eye:register(c6);
float4 Emission:register(c7);float4 Settings:register(c8);
float4 LP[8]:register(c9);float4 LC[8]:register(c17);float4 LD[8]:register(c25);
float4 DP[16]:register(c33);float4 DD[16]:register(c49);float4 DC[16]:register(c65);float4 DynamicSettings:register(c81);float4 Signal:register(c82);float4 SignalOther:register(c83);
float4 LocalSettings:register(c84);
float4 Rain:register(c85);float4 RoadSettings:register(c86);float4 Specular:register(c87);
float4 RoadDetailSettings:register(c88);float4 RoadDiffuse:register(c89);float4 RoadAmbient:register(c90);
row_major float4x4 ShadowClip:register(c91);float4 ShadowSettings:register(c95);
sampler2D WorldShadow:register(s6);
float4 PSShadow(SO i):COLOR {return lerp(float4(1,1,1,1),float4(1,1,1,tex2D(Base,i.uv).a*i.c.a),Diffuse.x);}
float ShadowAt(float2 uv,float biased){if(ShadowSettings.y>0){float depth=tex2Dlod(WorldShadow,float4(uv,0,0)).r;return biased<=depth?1:0;}return tex2Dproj(WorldShadow,float4(uv,biased,1)).r;}
float WorldVisibility(float3 position,float ndotl){
 [branch]if(ShadowSettings.x<=0)return 1;
 float4 p=mul(float4(position,1),ShadowClip);float3 uv=p.xyz/max(p.w,.00001);
 // Neighbour taps lie on the same receiver plane at different depths. Using
 // the centre depth for every tap self-shadows sloped roads as the texels move.
 // Derivatives precede the varying bounds branch so a whole pixel quad agrees.
 float3 dx=ddx(uv),dy=ddy(uv);float det=dx.x*dy.y-dx.y*dy.x;
 float2 depthGradient=float2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/(abs(det)>1e-12?det:1);
 depthGradient=abs(det)>1e-12?depthGradient:0;
 if(any(uv.xy<0)||any(uv.xy>1)||uv.z<0||uv.z>1)return 1;
 // A small floor also avoids an exactly-zero bias on a sun-facing flat road.
 float2 step=ShadowSettings.zw;
 // Hardware PCF also compares adjacent texel centres inside each tap. Cover
 // that footprint with a receiver-plane bias; the rectangular map has two
 // distinct texel sizes.
 float footprint=clamp(dot(abs(depthGradient),step),0,.002);
 float bias=uv.z-max(.00002,.0001*(1-ndotl))-footprint;
 float2 offset=step;float a=clamp(dot(depthGradient,offset),-.002,.002),b=clamp(dot(depthGradient,float2(-offset.x,offset.y)),-.002,.002);
 float shadow=(ShadowAt(uv.xy,bias)+ShadowAt(uv.xy+offset,bias+a)+ShadowAt(uv.xy+float2(-offset.x,offset.y),bias+b)+ShadowAt(uv.xy+float2(offset.x,-offset.y),bias-b)+ShadowAt(uv.xy-offset,bias-a))*.2;

 // Match the native perspective shadow map fade at its moving boundary.
 return saturate(shadow+1-saturate((uv.y+.3)*2));
}
float4 PSDepth(DO i):COLOR {return float4(i.depth,i.depth,i.depth,tex2D(Base,i.uv).a*i.c.a);}
float4 PS(PO i):COLOR {float4 tex=tex2D(Base,i.uv);float3 color;float2 roadDx=ddx(i.world.xy*.6),roadDy=ddy(i.world.xy*.6);
if(Settings.x>0&&Settings.y>0){color=tex.rgb*Emission.rgb*Emission.w*lerp(1,i.c.rgb,Settings.z);}
else {float3 n=normalize(i.n);
float3 albedo=tex.rgb;float4 diffuse=Diffuse,ambient=Ambient;
[branch]if(RoadSettings.x>0){diffuse=RoadDiffuse;ambient=RoadAmbient;
 [branch]if(RoadDetailSettings.x>0){
  // Port SunSet's MISCMAP3 detail normal in the same render axes (Z up).
  // A missing native texture retains the unperturbed geometric normal.
  float3 detail=lerp(float3(0,0,1),tex2Dgrad(RoadDetail,i.world.xy*.6,roadDx,roadDy).rgb*2-1,saturate(n.z));
  // WorldReflect uses the raw detail red channel for albedo; normalization
  // applies only to its normal. Preserve that order on steep road geometry.
  float detailAlbedo=detail.x*.2;
  float3 bitangent=cross(n,float3(1,0,0));float norm=dot(bitangent,bitangent);
  [branch]if(norm>.000001&&dot(detail,detail)>.000001){
   float3 tangent=normalize(cross(bitangent,n));bitangent=cross(n,tangent);
   detail=normalize(detail);n=normalize(detail.x*tangent+detail.y*bitangent+detail.z*n);
   albedo=max(0,albedo+detailAlbedo);
  }
 }
}
float exposure=saturate(i.exposure);
float visibility=WorldVisibility(i.world,saturate(dot(normalize(i.n),Sun.xyz)));
if(RoadSettings.x>0)albedo*=lerp(saturate(dot(n,normalize(i.n))),1,visibility);
float3 illumination=lerp(.5,i.c.rgb*2,diffuse.w)*ambient.rgb+saturate(dot(n,Sun.xyz))*diffuse.rgb*exposure*visibility;
float3 spotIllumination=0;
[loop]for(int k=0;k<LocalSettings.x;k++){float3 d=LP[k].xyz-i.world;float dist=length(d);float atten=saturate(1-dist*LP[k].w);[branch]if(atten<=0)continue;float3 toward=d/max(dist,.0001);float cone=smoothstep(LD[k].w,LC[k].w,dot(-toward,LD[k].xyz));spotIllumination+=LC[k].rgb*atten*atten*saturate(dot(n,toward))*cone;}
[loop]for(int j=0;j<DynamicSettings.x;j++){float3 d=DP[j].xyz-i.world;float dist=length(d);float atten=saturate(1-dist*DP[j].w);[branch]if(atten<=0)continue;float3 toward=d/max(dist,.0001);float cone=smoothstep(DD[j].w,DC[j].w,dot(-toward,DD[j].xyz));spotIllumination+=DC[j].rgb*atten*atten*smoothstep(-.5,1,dot(n,toward))*cone;}
// SunSet spotlights.fx ApplySpotLights caps the combined diffuse vector
// at 4.5 before HDR compression. Match it across world and vehicle lights.
float spotPower=length(spotIllumination);
illumination+=spotIllumination*(min(spotPower,4.5)/max(spotPower,.0001));
color=albedo*illumination;
// SunSet WorldReflectShader uses render axes (Z up), road wetness in
// cvRainParams.y, an optional puddle mask, and its native reflection target.
// Only audited pavement materials can enter this branch.
[branch]if(RoadSettings.x>0){float upward=saturate(normalize(i.n).z);float wet=saturate(Rain.y)*upward*exposure;
float puddle=RoadSettings.z>0?saturate(tex2Dlod(RoadMask,float4(i.world.xy/20,0,0)).r+.2):1;
puddle=lerp(1,puddle,saturate(Rain.w))*wet;
// When native road reflections are disabled, their allocated texture has
// no valid scene. Preserve albedo rather than replacing it with that target.
if(RoadSettings.y>0)color*=1-puddle;
float3 toEye=normalize(Eye.xyz-i.world);float spec=pow(saturate(dot(reflect(-Sun.xyz,n),toEye)),max(1,Specular.w));
color+=Specular.rgb*ambient.w*spec*wet;
[branch]if(RoadSettings.y>0){float4 uv=i.reflection;
if(RoadSettings.w>0){float2 drops=tex2Dlod(RoadDrops,float4(i.world.xy,0,0)).rg-4./255.;uv.xy-=(drops*2-1)*Rain.x;}
uv.xy+=n.xy*.1;float2 projected=uv.xy/max(uv.w,.0001);
color+=tex2Dlod(RoadReflection,float4(saturate(projected),0,0)).rgb*1.3*puddle;}}
if(Settings.x>0)color+=tex.rgb*Emission.rgb*Emission.w*tex2D(Mask,i.uv).rgb;}
// Lens coordinates are taken from the original W2C signal texture atlases.
if(Signal.w>=0){float lens=1-smoothstep(Signal.z-.008,Signal.z,length(frac(i.uv)-Signal.xy));float other=max(1-smoothstep(Signal.z-.008,Signal.z,length(frac(i.uv)-SignalOther.xy)),1-smoothstep(Signal.z-.008,Signal.z,length(frac(i.uv)-SignalOther.zw)));color=lerp(color,tex.rgb*.025,other);color=lerp(color,tex.rgb*(.025+Signal.w*4),lens);}
float distance=length(i.world-Eye.xyz);float fog=pow(1-saturate((FogValue.x-distance)*FogValue.y),max(.01,FogValue.w))*FogValue.z;
if(Settings.w<.5)color=lerp(color,lerp(Fog.rgb,FogSun.rgb,smoothstep(.3,1,dot(normalize(i.world-Eye.xyz),Sun.xyz))),saturate(fog));
color=color/(1+max(color,0));return float4(color,tex.a*i.c.a);}

// Converted props previously wrote uncompressed colours into SunSet's HDR
// scene/cubemap. A sample near 1 decoded to arbitrarily bright car reflections.
float4 PSPropColour(float4 c:COLOR0,float2 uv:TEXCOORD0):COLOR{float4 value=tex2D(Base,uv)*c;float3 radiance=max(0,value.rgb);value.rgb=radiance/(1+radiance);return value;}
