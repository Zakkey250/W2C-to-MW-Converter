#pragma once
#include "../../format/GeometryAddon.h"
#include "AuthoredPropsAddon.h"
#include "../../format/RoadEnvironment.h"
static w2road::Environment authoredGroundEnvironment;

struct WorldClosureAddon {
 std::map<unsigned,std::set<unsigned>> draws;
 std::set<unsigned> props;
};
static WorldClosureAddon worldClosureAddon;
static std::map<unsigned, w2geometry::Page> worldGeometryAddon;
struct RockportCushions {
 unsigned section=0, templateId=0;
 std::string templateSha;
 std::map<unsigned,std::array<float,7>> placements;
};
static RockportCushions rockportCushions;

static RockportCushions ReadRockportCushions(const std::filesystem::path& path,
    const std::string& collisionSha,const std::map<unsigned,w2geometry::Page>& geometry){
 using namespace w2v2disk;
 RockportCushions result;
 File file(path);
 const auto doc=parse(file.read(0,file.size,8192),8192);
 keys(doc,{"schema","generation","collisionSha256","geometrySha256","section","template",
           "templateSha256","placements"});
 const auto schema=uintv(doc.at("schema"));
 need((schema==1||schema==2)&&doc.at("generation")==v2Store.buildId&&
      doc.at("collisionSha256")==collisionSha,"Rockport cushion collision identity");
 result.section=unsigned(uintv(doc.at("section"),65535));
 need(result.section==2015&&geometry.count(result.section),"Rockport cushion geometry section");
 const auto& page=geometry.at(result.section);
 need(doc.at("geometrySha256")==page.digest,"Rockport cushion geometry bytes");
 result.templateId=unsigned(uintv(doc.at("template"),99999));
 result.templateSha=str(doc.at("templateSha256"),64);
 const auto bytes=page.Read();
 auto drawOffset=r64(bytes,64);
 const auto& rows=doc.at("placements");
 need(rows.is_array()&&(schema==1?rows.size()==5:rows.size()<=5),"Rockport cushion placement count");
 for(const auto& row:rows){
  keys(row,{"draw","drawSha256","position","rotation"});
  const unsigned id=unsigned(uintv(row.at("draw"),65535));
  auto at=drawOffset+U(id)*sizeof(PageDraw);
  need(id<page.summary.draws&&r32(bytes,at)==id&&
       row.at("drawSha256")==sha(B(bytes.begin()+at,bytes.begin()+at+sizeof(PageDraw))),
       "Rockport cushion draw identity");
  const auto& q=row.at("rotation");
  const auto& p=row.at("position");
  need(q.is_array()&&q.size()==4&&p.is_array()&&p.size()==3,"Rockport cushion transform shape");
  std::array<float,7> transform{};
  double norm=0;
  for(unsigned i=0;i<4;++i){auto v=number(q[i]);norm+=v*v;transform[i]=float(v);}
  need(std::abs(norm-1.)<.001,"Rockport cushion rotation norm");
  for(unsigned i=0;i<3;++i)transform[4+i]=float(number(p[i]));
  need(transform[4]>-283&&transform[4]<-276&&transform[5]>94&&transform[5]<97&&
       transform[6]>291&&transform[6]<299,"Rockport cushion transform bounds");
  need(result.placements.emplace(id,transform).second,"duplicate Rockport cushion");
 }
 return result;
}

static WorldClosureAddon ReadWorldClosureAddon(const w2v2disk::J& patch){
 using namespace w2v2disk;
 WorldClosureAddon result;
 if(uintv(patch.at("schema"))<2)return result;
 need(patch.at("generation")==v2Store.buildId,"closure addon generation");
 const auto& draws=patch.at("closureDraws");
 const auto& props=patch.at("closureProps");
 need(draws.is_array()&&draws.size()<=64&&props.is_array()&&props.size()<=64,"closure addon size");
 unsigned loadedSection=~0u,drawCount=0;
 U drawOffset=0;
 B page;
 for(const auto& row:draws){
  need(row.is_array()&&row.size()==4,"closure addon draw fields");
  const unsigned section=unsigned(uintv(row[0],65535)),ordinal=unsigned(uintv(row[1],65535));
  if(section!=loadedSection){
   page=v2Store.read(v2Store.page(section));
   auto summary=validateGeometryPage(page);
   need(summary.section==section,"closure addon page identity");
   loadedSection=section;
   drawCount=summary.draws;
   drawOffset=r64(page,64);
  }
  need(ordinal<drawCount,"closure addon draw index");
  const auto at=drawOffset+U(ordinal)*sizeof(PageDraw);
  need(r32(page,at)==ordinal&&r32(page,at+4)==uintv(row[2],UINT_MAX)&&r32(page,at+20)==uintv(row[3],UINT_MAX),
       "closure addon draw identity");
  need(result.draws[section].insert(ordinal).second,"duplicate closure draw");
 }
 if(!props.empty()){
  const auto bank=v2Store.read(v2Store.find(61,"native/prop-placements"));
  const unsigned count=r32(bank,4);
  need(r32(bank,0)==0x31504d57&&count&&bank.size()==8+64ull*count,"closure addon prop bank");
  for(const auto& row:props){
   need(row.is_array()&&row.size()==2,"closure addon prop fields");
   const unsigned ordinal=unsigned(uintv(row[0],count-1));
   const auto* record=bank.data()+8+std::size_t(ordinal)*64;
   need(sha(B(record,record+64))==str(row[1],64),"closure addon prop identity");
   need(result.props.insert(ordinal).second,"duplicate closure prop");
  }
 }
 return result;
}

static void ApplyClosureDraws(PageCpu& page){
 const auto entry=worldClosureAddon.draws.find(page.id);
 if(entry==worldClosureAddon.draws.end())return;
 const auto& hidden=entry->second;
 // Spans share a cluster's vertex table with its packed shadow indices.
 // Remove matching triangles there as well as omitting the visible draw.
 using Triangle=std::array<std::uint16_t,3>;
 const auto triangle=[](const BYTE* bytes){
  Triangle indices;
  memcpy(indices.data(),bytes,sizeof(indices));
  std::sort(indices.begin(),indices.end());
  return indices;
 };
 std::map<unsigned,std::set<Triangle>> shadowTriangles;
 for(const auto& span:page.spans){
  if(!hidden.count(span.source))continue;
  auto& block=page.blocks[span.chunk];
  auto* indices=block.bytes.data()+std::size_t(block.vertices)*40;
  auto& masked=shadowTriangles[span.chunk];
  // The alpha shadow pass also uses the draw's ordinary indices.
  for(unsigned i=span.local;i<span.local+span.count;i+=3){
   auto* face=indices+i*2;
   masked.insert(triangle(face));
   memcpy(face+2,face,2);
   memcpy(face+4,face,2);
  }
 }
 for(auto& [chunk,masked]:shadowTriangles){
  auto& block=page.blocks[chunk];
  auto* indices=block.bytes.data()+std::size_t(block.vertices)*40;
  auto* shadows=indices+std::size_t(block.indices)*2;
  for(unsigned i=0;i<block.shadows;i+=3){
   auto* face=shadows+i*2;
   if(masked.count(triangle(face))){
    memcpy(face+2,face,2);
    memcpy(face+4,face,2);
   }
  }
 }
 for(auto& order:page.order)std::erase_if(order,[&](unsigned ordinal){return hidden.count(ordinal)!=0;});
 if(worldLogEnabled)Log("WORLD_CLOSURE_DRAWS_APPLIED section=%u draws=%u",page.id,unsigned(hidden.size()));
}
