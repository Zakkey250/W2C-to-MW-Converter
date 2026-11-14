#pragma once
#include "Bundle.h"
namespace w2source {
// Keep only compact page/texture lookup metadata. Full source model/header
// metadata remains indexed on disk and is parsed only for the active asset.
struct SourceEntry {unsigned kind=0,count=0,stride=0;d::U bytes=0;std::string id,key,sha;d::J meta;};
class SourceStore {
 Bundle bundle_;std::map<std::string,SourceEntry> entries_;std::map<unsigned,std::string> pages_,textures_;std::set<unsigned> originalUnavailableTextures_;
 SourceEntry entry(const Asset& a)const{
  auto meta=bundle_.metadata(a);SourceEntry e;e.id=a.id;e.kind=a.kind;e.bytes=a.bytes;e.count=a.count;e.stride=a.stride;e.sha=a.sha;e.key=d::str(meta["sourceKey"],4096);
  if(a.kind==21){for(auto key:{"sourceSection","sourceMeshCount","sourceVertexCount","sourceIndexCount","bounds"})e.meta[key]=meta.at(key);}
  else if(a.kind==32){e.meta["sourceTextureHash"]=meta.at("sourceTextureHash");e.meta["sourceSha256"]=a.sha;}
  else e.meta=std::move(meta);return e;
 }
public:
 std::string buildId;d::U indexBytes=0;
 void open(const std::filesystem::path& root,const std::string& approvedIndexSha,bool trial=false){
  d::need(!approvedIndexSha.empty(),"source launch requires an approved index");bundle_.open(root,approvedIndexSha);buildId=bundle_.buildId;
  auto world=bundle_.read(1,"world-definition");auto definition=d::parse(world,65536);d::need(definition.at("gameNativeClosureComplete")==true||(trial&&definition.value("runtimeTrialReady",false)),"source generation native adapters incomplete; draft cannot launch");
  if(auto gaps=bundle_.find(99,"audit/original-unavailable-runtime-textures")){auto data=d::parse(bundle_.read(*gaps),65536);d::need(data.is_array()&&data.size()<=128,"original texture absence ledger cap");for(const auto& v:data){unsigned hash=unsigned(std::stoul(d::str(v["hash"],8),nullptr,16));d::need(hash&&originalUnavailableTextures_.insert(hash).second,"original texture absence identity");}}
  for(auto& [identity,a]:bundle_.assets()){
   if(a.kind==21){auto e=entry(a);unsigned section=unsigned(d::uintv(e.meta["sourceSection"]));d::need(pages_.emplace(section,identity).second,"source duplicate terrain section");entries_.emplace(identity,std::move(e));}
   else if(a.kind==32){auto meta=bundle_.metadata(a);if(meta.at("variant")==true)continue;auto e=entry(a);unsigned hash=unsigned(std::stoul(d::str(e.meta["sourceTextureHash"]),nullptr,16));d::need(textures_.emplace(hash,identity).second,"source duplicate canonical texture");entries_.emplace(identity,std::move(e));}
  }
  d::need(!pages_.empty()&&!textures_.empty(),"source terrain/texture world empty");indexBytes=bundle_.metadataBytes+d::U(entries_.size())*800;d::need(indexBytes<=128*d::MiB,"source runtime index reservation");
 }
 const Bundle& bundle()const{return bundle_;}const auto& pages()const{return pages_;}const auto& entries()const{return entries_;}
 const SourceEntry& page(unsigned id)const{return entries_.at(pages_.at(id));}
 const SourceEntry* texture(unsigned hash)const{auto it=textures_.find(hash);return it==textures_.end()?nullptr:&entries_.at(it->second);}
 bool originalUnavailableTexture(unsigned hash)const{return originalUnavailableTextures_.count(hash)!=0;}
 SourceEntry find(unsigned kind,const std::string& key)const{auto a=bundle_.find(kind,key);d::need(a!=nullptr,"required source record missing");return entry(*a);}
 // Initialization-only fixed records are retained before workers start.
 const SourceEntry& retain(unsigned kind,const std::string& key){auto a=bundle_.find(kind,key);d::need(a!=nullptr,"required source runtime record missing");auto it=entries_.find(a->id);if(it!=entries_.end())return it->second;return entries_.emplace(a->id,entry(*a)).first->second;}
 d::B read(const SourceEntry& e)const{return bundle_.read(bundle_.assets().at(e.id));}
 d::B probe(const SourceEntry& e,d::U relative,d::U n,d::U budget)const{return bundle_.probe(bundle_.assets().at(e.id),relative,n,budget);}
};
}
