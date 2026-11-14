#pragma once
#include "../integration/MWEventGeometryApi.h"
#include "CandidateIndexBounds.h"
namespace event_geometry_service {
// Immutable collision queries are also needed while the frontend prepares a
// race (flow 3/5), before the driving-only IsWorldActive gate reaches flow 6.
// Loader activation and the reference-world switch remain authoritative.
unsigned __cdecl Available(){return loaderActive&&!propNativeReference&&!collision.nodes.empty();}
bool Finite(mwelgeo::Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&fabs(p.x)<100000&&fabs(p.y)<100000&&fabs(p.z)<100000;}
Vec3 V(mwelgeo::Point p){return {p.x,p.y,p.z};}
static void(*TraceBegin)(unsigned)=nullptr;
static void(*TraceEnd)(unsigned)=nullptr;
unsigned __cdecl Begin(){unsigned previous=Available()?collision.BeginQuery():2u;if(TraceBegin)TraceBegin(previous);return previous;}
void __cdecl End(unsigned previous){if(previous<2)collision.EndQuery(previous);if(TraceEnd)TraceEnd(previous);}

unsigned __cdecl Generation(){return collision.QueryGeneration();}
unsigned __cdecl Ray(mwelgeo::Point from,mwelgeo::Point to,mwelgeo::Hit* out,unsigned flags){
 if(!out||flags>3||!Available()||!Finite(from)||!Finite(to))return 0;
 RayHit hit;bool found=collision.Ray(V(from),V(to),hit,(flags&1)!=0,(flags&2)!=0);memcpy(out,&hit,sizeof(hit));return unsigned(found);
}
unsigned __cdecl Box(mwelgeo::Point low,mwelgeo::Point high,unsigned walls,mwelgeo::IndexSink sink,void* context){
 if(!sink||walls>1||!Available()||!Finite(low)||!Finite(high)||low.x>high.x||low.y>high.y||low.z>high.z)return 0;
 static thread_local std::vector<unsigned> found;collision.QueryBox(V(low),V(high),found,walls!=0);
 for(auto index:found)sink(context,index);return 1;
}
unsigned __cdecl Triangle(unsigned index,mwelgeo::Triangle* out){if(!out||!Available()||index>=collision.triangles.size())return 0;memcpy(out,&collision.triangles[index],sizeof(*out));return 1;}
unsigned __cdecl Overlaps(unsigned index,mwelgeo::Point low,mwelgeo::Point high){return Available()&&Finite(low)&&Finite(high)&&index<collision.triangles.size()&&collision.TriangleOverlaps(index,V(low),V(high));}
mwelgeo::Point __cdecl Padding(){auto p=collision.WallRayPadding();return {p.x,p.y,p.z};}
unsigned __cdecl Candidates(mwelgeo::Point from,mwelgeo::Point to,mwelgeo::Hit* out,const unsigned* values,unsigned count,unsigned complete){
 if(!out||(!values&&count)||count>collision.triangles.size()||complete>1||!Available()||!Finite(from)||!Finite(to))return 0;
 std::span<const unsigned> indices(values,count);if(!CandidateIndicesValid(values,count,unsigned(collision.triangles.size())))return 0;
 RayHit hit;bool found=collision.RayCandidatesSpan(V(from),V(to),hit,indices,complete!=0);memcpy(out,&hit,sizeof(hit));return unsigned(found);
}
}
#pragma comment(linker,"/EXPORT:W2MW_GeometryApiV1=_W2MW_GeometryApiV1")
extern "C" __declspec(dllexport) unsigned __cdecl W2MW_GeometryApiV1(mwelgeo::Api* out,unsigned size){
 if(!out||size!=sizeof(*out))return 0;
 using namespace event_geometry_service;
 *out={sizeof(*out),mwelgeo::Version,mwelgeo::W2CWorld,Available,Begin,End,Generation,Ray,Box,Triangle,Overlaps,Padding,Candidates};return 1;
}
