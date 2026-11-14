#pragma once
// New four-file generation. The accepted V2 reader below remains unchanged;
// only its bounded byte/CRC/SHA utilities and geometry validator are reused.
#include "../format/BoundedDisk.h"
#include <memory>
namespace w2source {
namespace d=w2v2disk;
constexpr d::U Header=256,BlockLimit=1048576,AssetLimit=64*d::MiB,IndexLimit=64*d::MiB,TotalLimit=80*1024*d::MiB;
constexpr unsigned AssetStride=128,BlockStride=64,DepStride=40,MaxAssets=131072,MaxBlocks=524288;
struct Block {d::U offset=0;unsigned stored=0,decoded=0,role=0,packedCrc=0,decodedCrc=0;std::string sha;};
struct Asset {std::string id,sha;unsigned kind=0,first=0,blocks=0,count=0,stride=0,metaBytes=0,depFirst=0,depCount=0;d::U bytes=0,metaAt=0;};
struct Dependency {unsigned role=0,kind=0;std::string target;};
class Bundle {
 std::filesystem::path root_;d::B index_;std::array<std::filesystem::path,4> paths_;std::array<d::U,4> sizes_{};std::array<d::B,4> headers_;
 std::vector<Block> blocks_;std::map<std::string,Asset> assets_;std::vector<Dependency> deps_;
 static d::B header(d::File& file,unsigned role){
  auto h=file.read(0,256,256);const char* magic[]={"","W2SRCI1","W2SRCD1","W2SRCM1"};
  d::need(!memcmp(h.data(),magic[role],8)&&d::r16(h,8)==1&&d::r16(h,10)==0&&d::r32(h,12)==256&&d::r32(h,16)==role&&d::r32(h,20)==3&&d::r32(h,24)==128&&d::r32(h,28)==64,"source envelope contract");
  d::need(d::r64(h,32)==file.size&&file.size<=TotalLimit,"source volume extent");unsigned stored=d::r32(h,224);std::fill(h.begin()+224,h.begin()+228,static_cast<unsigned char>(0));d::need(d::crc(h)==stored,"source header CRC");
  d::need(d::r32(h,232)==BlockLimit&&d::r32(h,236)==0&&d::r32(h,240)==1&&d::r32(h,244)==1&&d::zeros(h,248,8),"source header features/reserved");
  if(role>1)d::need(d::zeros(h,40,52)&&d::r32(h,92)==40,"source data header reserved");return h;
 }
public:
 std::string buildId,sourceId,compilerFingerprint;d::U metadataBytes=0;
 const auto& assets()const{return assets_;}const auto& blocks()const{return blocks_;}
 void open(const std::filesystem::path& root,const std::string& approvedIndexSha={}){
  d::need(assets_.empty()&&index_.empty(),"source bundle already open");root_=root;
  static const char* names[]={"","L5RA.w2i","STREAML5RA.w2d","L5RA.w2m"};d::U total=0;
  for(unsigned role=1;role<=3;++role){paths_[role]=root/std::filesystem::path(names[role]);d::File f(paths_[role]);sizes_[role]=f.size;total=d::add(total,f.size);d::need(total<=TotalLimit,"source generation size cap");headers_[role]=header(f,role);}
  const auto& h=headers_[1];buildId=d::hex(h.data()+96,32);sourceId=d::hex(h.data()+128,32);compilerFingerprint=d::hex(h.data()+160,32);
  for(unsigned role=2;role<=3;++role)d::need(!memcmp(h.data()+96,headers_[role].data()+96,96),"source mixed generation");
  d::File file(paths_[1]);if(!approvedIndexSha.empty())d::need(file.fullHash()==approvedIndexSha,"source index not approved");
  auto assetAt=d::r64(h,40);unsigned ac=d::r32(h,48),bc=d::r32(h,52);auto blockAt=d::r64(h,56),metaAt=d::r64(h,64),metaBytes=d::r64(h,72),depAt=d::r64(h,80);unsigned dc=d::r32(h,88);
  d::need(ac<=MaxAssets&&bc<=MaxBlocks&&d::r32(h,92)==40&&assetAt==256&&blockAt==d::add(assetAt,d::mul(ac,128))&&metaAt==d::add(blockAt,d::mul(bc,64))&&depAt==d::add(metaAt,metaBytes)&&d::add(depAt,d::mul(dc,40))==file.size&&file.size-256<=IndexLimit,"source index dimensions");
  auto body=file.read(256,file.size-256,IndexLimit);d::need(d::sha(body)==d::hex(h.data()+192,32)&&d::crc(body)==d::r32(h,228),"source index integrity");index_=h;index_.insert(index_.end(),body.begin(),body.end());body.clear();body.shrink_to_fit();
  std::array<std::vector<std::pair<d::U,d::U>>,4> ranges;blocks_.reserve(bc);
  for(unsigned i=0;i<bc;++i){auto p=size_t(blockAt+d::U(i)*64);Block b;b.offset=d::r64(index_,p);b.stored=d::r32(index_,p+8);b.decoded=d::r32(index_,p+12);b.role=d::r16(index_,p+18);b.packedCrc=d::r32(index_,p+24);b.decodedCrc=d::r32(index_,p+28);b.sha=d::hex(index_.data()+p+32,32);
   d::need((b.role==2||b.role==3)&&d::r16(index_,p+16)==0&&d::r32(index_,p+20)==0&&b.decoded>0&&b.decoded<=BlockLimit&&b.stored==b.decoded&&b.offset>=256&&b.offset%64==0&&b.offset<=sizes_[b.role]&&b.stored<=sizes_[b.role]-b.offset,"source block bounds/codec");ranges[b.role].emplace_back(b.offset,b.offset+b.stored);blocks_.push_back(std::move(b));}
  std::vector<unsigned char> blockOwners(bc),depOwners(dc);std::vector<std::pair<d::U,d::U>> metaRanges;std::string previous;
  for(unsigned i=0;i<ac;++i){auto p=size_t(assetAt+d::U(i)*128);Asset a;a.id=d::hex(index_.data()+p,32);a.kind=d::r32(index_,p+32);a.bytes=d::r64(index_,p+40);a.first=d::r32(index_,p+48);a.blocks=d::r32(index_,p+52);a.count=d::r32(index_,p+56);a.stride=d::r32(index_,p+60);a.metaAt=d::r64(index_,p+64);a.metaBytes=d::r32(index_,p+72);a.depFirst=d::r32(index_,p+76);a.depCount=d::r32(index_,p+80);a.sha=d::hex(index_.data()+p+88,32);
   d::need((previous.empty()||previous<a.id)&&a.kind&&d::r32(index_,p+36)==0&&d::r32(index_,p+84)==0&&d::r32(index_,p+124)==0&&a.bytes<=AssetLimit&&a.metaBytes>0&&a.metaBytes<=65536&&a.metaAt>=metaAt&&a.metaAt%8==0&&a.metaAt<=depAt&&a.metaBytes<=depAt-a.metaAt&&d::U(a.first)+a.blocks<=bc&&a.depCount<=1024&&d::U(a.depFirst)+a.depCount<=dc,"source asset dimensions/order");
   previous=a.id;auto mb=d::B(index_.begin()+size_t(a.metaAt),index_.begin()+size_t(a.metaAt+a.metaBytes));d::need(d::crc(mb)==d::r32(index_,p+120),"source metadata CRC");auto parsed=d::parse(mb,65536);d::need(parsed.is_object()&&id(a.kind,d::str(parsed["sourceKey"],4096))==a.id,"source metadata object/identity");
   d::U n=0;for(unsigned b=a.first;b<a.first+a.blocks;++b){d::need(blockOwners[b]++==0,"source block multiple owners");n=d::add(n,blocks_[b].decoded);}d::need(n==a.bytes,"source asset block byte sum");
   for(unsigned dep=a.depFirst;dep<a.depFirst+a.depCount;++dep)d::need(depOwners[dep]++==0,"source dependency multiple owners");metaRanges.emplace_back(a.metaAt,a.metaAt+a.metaBytes);assets_.emplace(a.id,std::move(a));}
  d::need(std::all_of(blockOwners.begin(),blockOwners.end(),[](auto x){return x==1;})&&std::all_of(depOwners.begin(),depOwners.end(),[](auto x){return x==1;}),"source unowned table record");
  deps_.reserve(dc);for(unsigned i=0;i<dc;++i){auto p=size_t(depAt+d::U(i)*40);deps_.push_back({d::r32(index_,p),d::r32(index_,p+4),d::hex(index_.data()+p+8,32)});}
  std::sort(metaRanges.begin(),metaRanges.end());d::U end=metaAt;for(auto [lo,hi]:metaRanges){d::need(lo>=end&&lo-end<=7&&d::zeros(index_,size_t(end),size_t(lo-end)),"source metadata overlap/padding");end=hi;}d::need(depAt>=end&&depAt-end<=7&&d::zeros(index_,size_t(end),size_t(depAt-end)),"source metadata tail");
  for(unsigned role=2;role<=3;++role){auto& r=ranges[role];std::sort(r.begin(),r.end());d::U cursor=256;d::File data(paths_[role]);for(auto [lo,hi]:r){d::need(lo>=cursor&&lo-cursor<=63,"source data overlap/unowned gap");auto pad=data.read(cursor,lo-cursor,63);d::need(d::zeros(pad,0,pad.size()),"source data padding");cursor=hi;}d::need(cursor==data.size,"source data unowned tail");}
  std::map<std::string,unsigned> colors;
  for(auto& [id,a]:assets_){if(colors[id]==2)continue;std::vector<std::pair<std::string,unsigned>> stack{{id,0}};colors[id]=1;while(!stack.empty()){d::need(stack.size()<=64,"source dependency depth");auto& frame=stack.back();auto& current=assets_.at(frame.first);if(frame.second==current.depCount){colors[frame.first]=2;stack.pop_back();continue;}auto dep=deps_.at(current.depFirst+frame.second++);auto next=assets_.find(dep.target);d::need(dep.role&&next!=assets_.end()&&next->second.kind==dep.kind,"source typed dependency missing");d::need(colors[dep.target]!=1,"source dependency cycle");if(colors[dep.target]!=2){colors[dep.target]=1;stack.emplace_back(dep.target,0);}}}
  metadataBytes=index_.size()+d::U(assets_.size())*(sizeof(Asset)+192)+d::U(blocks_.size())*(sizeof(Block)+64)+d::U(deps_.size())*(sizeof(Dependency)+64);d::need(metadataBytes<=128*d::MiB,"source metadata resident budget");
 }
 std::string id(unsigned kind,const std::string& key)const{auto source=d::unhex(sourceId,32);d::Hash h;const char domain[]="W2CMW.SourceAsset.r1";h.append(domain,sizeof(domain));h.append(source.data(),source.size());unsigned char k[4]={static_cast<unsigned char>(kind),static_cast<unsigned char>(kind>>8),static_cast<unsigned char>(kind>>16),static_cast<unsigned char>(kind>>24)};h.append(k,4);h.append(key.data(),key.size());return h.finish();}
 const Asset* find(unsigned kind,const std::string& key)const{auto it=assets_.find(id(kind,key));return it==assets_.end()?nullptr:&it->second;}
 d::J metadata(const Asset& a)const{return d::parse(d::B(index_.begin()+size_t(a.metaAt),index_.begin()+size_t(a.metaAt+a.metaBytes)),65536);}
 d::B read(const Asset& a)const{
  d::need(a.bytes<=AssetLimit,"source asset read budget");d::B output;output.reserve(size_t(a.bytes));std::unique_ptr<d::File> file;unsigned role=0;
  for(unsigned i=a.first;i<a.first+a.blocks;++i){auto& b=blocks_.at(i);if(b.role!=role){file=std::make_unique<d::File>(paths_[b.role]);role=b.role;}auto bytes=file->read(b.offset,b.stored,BlockLimit);d::need(d::crc(bytes)==b.packedCrc&&d::crc(bytes)==b.decodedCrc&&d::sha(bytes)==b.sha,"source block integrity before use");output.insert(output.end(),bytes.begin(),bytes.end());}
  d::need(output.size()==a.bytes&&d::sha(output)==a.sha,"source asset integrity before use");return output;
 }
 d::B read(unsigned kind,const std::string& key)const{auto a=find(kind,key);d::need(a!=nullptr,"required source asset absent");return read(*a);}
 d::B probe(const Asset& a,d::U offset,d::U n,d::U budget)const{
  d::need(n<=budget&&offset<=a.bytes&&n<=a.bytes-offset,"source bounded logical probe");d::B output(static_cast<size_t>(n));d::U logical=0,written=0;
  for(unsigned i=a.first;i<a.first+a.blocks&&written<n;++i){const auto& b=blocks_.at(i);auto stop=d::add(logical,b.decoded);if(stop>offset&&logical<offset+n){d::File file(paths_[b.role]);auto bytes=file.read(b.offset,b.stored,BlockLimit);d::need(d::crc(bytes)==b.packedCrc&&d::crc(bytes)==b.decodedCrc&&d::sha(bytes)==b.sha,"source probed block integrity");auto lo=std::max(logical,offset),hi=std::min(stop,offset+n);memcpy(output.data()+size_t(lo-offset),bytes.data()+size_t(lo-logical),size_t(hi-lo));written+=hi-lo;}logical=stop;}
  d::need(written==n,"source logical probe coverage");return output;
 }
 void validateSymbols()const{
  d::File file(root_/L"L5RA.symbols.json");auto bytes=file.read(0,file.size,32*d::MiB);auto symbols=d::parse(bytes,32*d::MiB);
  d::keys(symbols,{"schema","buildId","sourceId","compilerFingerprint","sourceManifest","worldDefinition","assets"});d::need(d::uintv(symbols["schema"])==1&&d::str(symbols["buildId"],64)==buildId&&d::str(symbols["sourceId"],64)==sourceId&&d::str(symbols["compilerFingerprint"],64)==compilerFingerprint,"source symbols mixed generation");
  auto jsonBytes=[](const d::J& j){auto s=j.dump(-1,' ',false,d::J::error_handler_t::strict);return d::B(s.begin(),s.end());};d::need(d::sha(jsonBytes(symbols["sourceManifest"]),"W2CMW.Source.r1")==sourceId,"source manifest identity");
  d::J expected=d::J::array(),recipeAssets=d::J::array();
  for(auto& [identity,a]:assets_){auto meta=metadata(a);expected.push_back({{"id",identity},{"kind",a.kind},{"key",meta["sourceKey"]},{"sha256",a.sha},{"bytes",a.bytes}});d::J deps=d::J::array();for(unsigned n=0;n<a.depCount;++n){auto& dep=deps_.at(a.depFirst+n);deps.push_back(d::J::array({dep.role,dep.kind,dep.target}));}auto mb=d::B(index_.begin()+size_t(a.metaAt),index_.begin()+size_t(a.metaAt+a.metaBytes));recipeAssets.push_back({{"id",identity},{"kind",a.kind},{"bytes",a.bytes},{"sha",a.sha},{"meta",d::sha(mb)},{"count",a.count},{"stride",a.stride},{"deps",deps}});}
  d::need(symbols["assets"]==expected,"source symbols asset ledger");d::J recipe={{"schema",1},{"source",sourceId},{"compiler",compilerFingerprint},{"world",symbols["worldDefinition"]},{"assets",recipeAssets}};d::need(d::sha(jsonBytes(recipe),"W2CMW.Build.r1")==buildId,"source build recipe semantics");
 }
 void validateAllPayloads()const{for(auto& [id,a]:assets_)read(a);}
};
}
