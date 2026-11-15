#pragma once
#ifndef W2C_PUBLIC_BUILD
#include "SourcePinnedGeneration.h"
#endif
#include "SourceGlobalContext.h"
#ifdef W2C_PUBLIC_BUILD
#include "../../format/CollisionAddon.h"
#endif
static bool V2InitializePacks(){try{
#ifdef W2C_PUBLIC_BUILD
 v2Store.open(std::filesystem::path(root)/sourcePublicGeneration/"World",sourcePublicIndexSha);
 auto definition=w2v2disk::parse(SourceBytes(1,"world-definition"),65536);
 w2v2disk::need(v2Store.buildId==sourcePublicBuild&&w2v2disk::str(definition.at("quality"))==sourcePublicQuality
  &&w2v2disk::str(definition.at("runtime"))=="W2CMWPublic","public W2C world identity");
#else
 v2Store.open(std::filesystem::path(root)/"W2CMWv2"/"World",SourcePinnedIndexSha256,SourcePinnedTrial);
#endif
 v2CachedMaterials=SourceBytes(11,"effective-materials");w2v2disk::readEffectiveMaterials(v2CachedMaterials);
 for(auto& [id,unused]:v2Store.pages()){if(id==26000){ValidateSourceGlobalContext();continue;}const auto& e=v2Store.page(id);Header h{};h.magic=Magic;h.version=Version;h.section=id;h.meshes=unsigned(w2v2disk::uintv(e.meta["sourceMeshCount"]));h.vertices=unsigned(w2v2disk::uintv(e.meta["sourceVertexCount"]));h.indices=unsigned(w2v2disk::uintv(e.meta["sourceIndexCount"]));float* bounds=&h.minimum.x;for(unsigned i=0;i<6;++i)bounds[i]=float(w2v2disk::number(e.meta["bounds"][i]));sections[id].header=h;}
 auto e=v2Store.find(60,"collision");auto bytes=v2Store.read(e);w2v2disk::Record record;record.kind=e.kind;record.source=e.key;record.meta=e.meta;record.count=e.count;record.stride=e.stride;w2v2disk::wmc(record,bytes);
 // The source ledger was validated above; the frozen legacy count is not
 // the identity of a newly generated original-source collision world.
 unsigned collisionCount=unsigned(e.count);
#ifdef W2C_PUBLIC_BUILD
 try {
  const auto path=std::filesystem::path(root)/"W2CMWPublic"/"AddOns"/"collision.json";
  if(std::filesystem::exists(path)){
   w2v2disk::File file(path);
   const auto patch=w2v2disk::parse(file.read(0,file.size,4*w2v2disk::MiB),4*w2v2disk::MiB);
   auto closures=ReadWorldClosureAddon(patch);
   bytes=w2collision::apply(bytes,patch,sourcePublicBuild);
   worldClosureAddon=std::move(closures);
   collisionCount=w2v2disk::r32(bytes,8);
   Log("COLLISION_ADDON_ACTIVE walls=%u floor=%u",unsigned(patch["removeWalls"].size()),unsigned(patch["addFloor"].size()));
   if(worldLogEnabled&&!worldClosureAddon.draws.empty())
    Log("WORLD_CLOSURE_ADDON_ACTIVE sections=%u props=%u",unsigned(worldClosureAddon.draws.size()),unsigned(worldClosureAddon.props.size()));
  }
 } catch(const std::exception& error) { Log("COLLISION_ADDON_REFUSED reason=%s",error.what()); }
#endif
 if(!collision.LoadBytes(bytes,collisionCount))throw std::runtime_error("native collision load failed");
 auto calibration=w2v2disk::parse(SourceBytes(71,"world-and-radar-calibration"),65536);auto p=calibration.at("parameters");w2v2disk::need(p.is_array()&&p.size()==4&&w2v2disk::str(calibration["sourceSha256"])==v2Store.find(70,"world-and-radar-map").sha,"original map calibration identity");double sx=w2v2disk::number(p[0]),ox=w2v2disk::number(p[1]),sy=w2v2disk::number(p[2]),oy=w2v2disk::number(p[3]);w2v2disk::need(sx>.01&&sx<10&&sy>.01&&sy<10,"original map scale bounds");world_radar::World={float(-6016-ox/sx),float(546+(oy-2048)/sy),float(2048/((sx+sy)*.5)),0};
 Log("V2_PACKS_ACTIVE build=%s sections=%u index_bytes=%llu collision_from_pack=1",v2Store.buildId.c_str(),unsigned(sections.size()),v2Store.indexBytes);return true;
 }catch(const std::exception& error){W2V2Status.error=1;++v2Failures;Log("V2_INIT_REFUSED reason=%s",error.what());return false;}}
static void V2EffectiveMaterials(){
 auto bytes=std::move(v2CachedMaterials);w2v2disk::readEffectiveMaterials(bytes);materials.clear();envMaterials.clear();envAnimations.clear();envFrames.clear();envNightOverlays.clear();
 unsigned count=w2v2disk::r32(bytes,12);for(unsigned i=0;i<count;++i){std::size_t at=128+std::size_t(i)*88;unsigned hash=w2v2disk::r32(bytes,at);Material material;EnvironmentMaterial env;memcpy(&material,bytes.data()+at+4,16);memcpy(&env,bytes.data()+at+20,64);materials[hash]=material;envMaterials[hash]=env;if(w2v2disk::r32(bytes,at+84))envNightOverlays.insert(hash);}
#ifdef W2C_PUBLIC_BUILD
 for(unsigned hash:environmentWindowLayers){
  auto texture=materials.find(hash);
  if(texture==materials.end()||(texture->second.blend!=1&&texture->second.blend!=2))continue;
  envNightOverlays.insert(hash);
  auto& material=envMaterials.at(hash);
  material.brightness=1.5f;material.prelit=1;material.always=0;material.vertexColor=0;
 }
 // Original opaque casino sign animation frames are the authored sign face;
 // keep that face in the day pass and only light it when SunSet says night.
 for(unsigned hash:{0x57887489u,0x5788748Au,0x5788748Bu,0x7CB4A328u,0x7CB4A329u,
                   0xBDDAA271u,0xBDDAA272u,0xBDDAA273u,0xBDDAA274u,
                   0x9AF85125u,0x9AF85126u,0xD9B7CBBAu,0xD9B7CBBBu,
                   0x90A432DDu,0x90A432DEu,0x357E346Du,0x357E346Eu,0x357E346Fu,0x357E3470u}){
  auto material=envMaterials.find(hash);
  if(material==envMaterials.end()||MaterialOf(hash).blend)continue;
  auto& emission=material->second;emission.mask=0;emission.rgb[0]=emission.rgb[1]=emission.rgb[2]=1;
  emission.brightness=2.35f;emission.prelit=1;emission.always=0;emission.vertexColor=0;
 }
 // These additive source textures are actual light overlays, not normal maps,
 // lane arrows, animated track barriers, or opaque daytime signage.
 for(unsigned hash:{0x0C524741u,0x5B423AABu,0x4AAECF67u,0x76C1F2EAu,0x6B7CB830u,
                   0xD89CE9A7u,0xF5444BF6u,0x93ADF857u,0x65226648u,0x692CF769u,
                   0x19FD4C14u,0xE6CAEE20u,0xADD7F6F5u,0xB7E12654u,0xE2C1940Eu}){
  if(!envMaterials.count(hash)||MaterialOf(hash).blend!=2)continue;
  auto& emission=envMaterials.at(hash);emission.mask=0;emission.rgb[0]=emission.rgb[1]=emission.rgb[2]=1;
  emission.brightness=2.8f;emission.prelit=1;emission.always=0;emission.vertexColor=0;
  envNightOverlays.insert(hash);
 }
 if(worldLogEnabled)Log("PUBLIC_NIGHT_WINDOW_LAYERS count=%u",unsigned(envNightOverlays.size()));
#endif
 std::size_t at=128+std::size_t(count)*88;for(unsigned i=0;i<w2v2disk::r32(bytes,16);++i){EnvironmentAnimation a{w2v2disk::r32(bytes,at),w2v2disk::r32(bytes,at+4),{}};unsigned n=w2v2disk::r32(bytes,at+8);at+=12;for(unsigned j=0;j<n;++j){auto hash=w2v2disk::r32(bytes,at);at+=4;a.frames.push_back(hash);envFrames[hash]={i,j};}envAnimations.push_back(std::move(a));}
}
