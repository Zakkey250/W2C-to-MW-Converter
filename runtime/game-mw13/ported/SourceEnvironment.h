#pragma once
static void LoadSourceEnvironment(){
 using namespace w2v2disk;
 auto load=[](unsigned kind,const char* key,unsigned magic,auto& values,unsigned cap){auto input=SourceInput(kind,key);need(input.template get<unsigned>()==magic,"environment record magic");auto n=input.template get<unsigned>();need(n<=cap,"environment count cap");using T=typename std::decay_t<decltype(values)>::value_type;need(input.remaining()==U(n)*sizeof(T),"environment exact record bytes");values.resize(n);input.read(values.data(),values.size()*sizeof(T));input.end();};
 load(51,"environment/lights",0x324c4d57,envLights,100000);load(51,"environment/flares",0x32464d57,envFlares,100000);
 for(unsigned i=0;i<envLights.size();++i){const auto& l=envLights[i];need(std::isfinite(l.position.x)&&std::isfinite(l.position.y)&&std::isfinite(l.position.z)&&std::isfinite(l.range)&&l.range>0&&l.range<10000&&std::isfinite(l.intensity)&&l.intensity>=0,"source light bounds");envLightGrid[{int(std::floor(l.position.x/64)),int(std::floor(l.position.y/64))}].push_back(i);}
 for(unsigned i=0;i<envFlares.size();++i){const auto& l=envFlares[i];need(l.type<=20&&std::isfinite(l.position.x)&&std::isfinite(l.position.y)&&std::isfinite(l.position.z),"source flare bounds");envFlareGrid[{int(std::floor(l.position.x/64)),int(std::floor(l.position.y/64))}].push_back(i);}
#ifdef W2C_PUBLIC_BUILD
 PreparePublicCeilingLights();
#endif
 Log("SOURCE_ENVIRONMENT lights=%u flares=%u pack_only=1",unsigned(envLights.size()),unsigned(envFlares.size()));
}
static bool LoadSourceVisibility(){try{
 using namespace w2v2disk;auto bytes=SourceBytes(51,"environment/visibility");auto data=parse(bytes,4*MiB);need(data.is_array()&&data.size()<=10000,"source visibility count");std::vector<Zone> staged;
 for(const auto& row:data){Zone z;z.id=unsigned(uintv(row["section"],65535));auto polygon=row["polygon"];need(polygon.is_array()&&polygon.size()>=3&&polygon.size()<=128,"source visibility polygon");for(const auto& p:polygon){need(p.is_array()&&p.size()==2,"source visibility point");Vec3 point{float(number(p[0])),float(number(p[1])),0};need(std::isfinite(point.x)&&std::isfinite(point.y),"source visibility finite point");z.boundary.push_back(point);z.low.x=std::min(z.low.x,point.x);z.low.y=std::min(z.low.y,point.y);z.high.x=std::max(z.high.x,point.x);z.high.y=std::max(z.high.y,point.y);}auto visible=row["visible"];need(visible.is_array()&&visible.size()<=30000,"source visibility membership");for(const auto& v:visible)z.sections.insert(unsigned(uintv(v,65535)));staged.push_back(std::move(z));}zones=std::move(staged);return true;
 }catch(const std::exception& error){Log("SOURCE_VISIBILITY_REFUSED reason=%s",error.what());return false;}}
