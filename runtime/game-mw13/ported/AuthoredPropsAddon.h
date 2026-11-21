#pragma once
struct AuthoredPropPlacement {unsigned templateId=0,sourceModel=0;std::array<float,8> transform{};};
static std::vector<AuthoredPropPlacement> authoredPropPlacements;
static std::vector<AuthoredPropPlacement> ReadAuthoredProps(const std::filesystem::path& path,const std::string& collisionSha){
 using namespace w2v2disk;File file(path);auto doc=parse(file.read(0,file.size,MiB),MiB);
 keys(doc,{"schema","generation","collisionSha256","placements"});
 need(doc.at("schema")==1&&doc.at("generation")==v2Store.buildId&&doc.at("collisionSha256")==collisionSha,"Authored prop generation/collision identity");
 const auto& rows=doc.at("placements");need(rows.is_array()&&rows.size()<=4096,"Authored prop count");
 const auto bank=v2Store.read(v2Store.find(61,"native/prop-placements"));
 need(bank.size()>=8&&r32(bank,0)==0x31504d57&&bank.size()==8+64ull*r32(bank,4),"Authored prop source catalog");
 std::vector<AuthoredPropPlacement> result;
 for(const auto& row:rows){
  keys(row,{"template","templateSha256","sourceModel","transform","editorTransform"});AuthoredPropPlacement p;
  vec(row.at("editorTransform"),16);
  p.templateId=unsigned(uintv(row.at("template"),r32(bank,4)-1));p.sourceModel=unsigned(uintv(row.at("sourceModel"),UINT_MAX));
  const auto at=8+U(p.templateId)*64;
  need(sha(B(bank.begin()+at,bank.begin()+at+64))==str(row.at("templateSha256"),64),"Authored prop template bytes");
  const auto& t=row.at("transform");need(t.is_array()&&t.size()==8,"Authored prop transform shape");
  double norm=0;for(unsigned i=0;i<8;++i){p.transform[i]=float(number(t[i]));need(std::abs(p.transform[i])<100000,"Authored prop transform range");if(i<4)norm+=p.transform[i]*p.transform[i];}
  need(std::abs(norm-1)<.001&&p.transform[7]>.001f&&p.transform[7]<=100,"Authored prop rotation/scale");result.push_back(std::move(p));
 }
 return result;
}
