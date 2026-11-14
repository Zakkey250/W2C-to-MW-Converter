#pragma once

namespace source_mw_tolls {
constexpr char payloadPath[]="W2CMWv2\\Native\\mw-tolls.bin";
constexpr char texturePath[]="W2CMWv2\\Native\\mw-toll-textures.bin";
struct Asset {unsigned asset,model,parameter;};
constexpr Asset assets[]={
 {0x2528FE86,0xAFE2D563,0x2971FF64},{0x7BC807F9,0xE25C4C5E,0x5AEFC6A8},
 {0xAA27F4C6,0xF2FBEBC0,0x5AEFC6A8},{0xB6D45FE1,0x27042FE4,0x2971FF64},
 {0xBA0A9F07,0xE4614A67,0x1220B451},
};
static const Asset* Find(unsigned id){for(const auto& a:assets)if(a.asset==id)return &a;return nullptr;}
static std::vector<std::array<BYTE,64>> placements;
static std::map<unsigned,w2v2disk::B> trees;
static w2v2disk::B models;

static void Parse(const w2v2disk::B& bytes){
 using namespace w2v2disk;
  const auto digest=sha(bytes);
  const bool legacy=digest=="621eab1c8bb139349ba4cef31cadb211a1854d3d57d125c2dbb1cb44c69ea065";
#ifdef W2C_PUBLIC_BUILD
  need(legacy||digest=="5f429d65cd0111082f9c892d2e910b570ab7c081d577356b818a8edf8fa209b1","stock MW toll payload fingerprint");
#else
  need(legacy,"stock MW toll payload fingerprint");
#endif
  need(bytes.size()>=24&&!memcmp(bytes.data(),"W2TOLL1",8),"MW toll header");
  auto n=r32(bytes,8),treeCount=r32(bytes,12),geometryBytes=r32(bytes,16);
  need(n==(legacy?293u:302u)&&treeCount==std::size(assets)&&!r32(bytes,20)&&24+64*n<=bytes.size(),"MW toll dimensions");
  size_t at=24;std::vector<std::array<BYTE,64>> records(n);
  std::map<unsigned,B> definitions;
  for(auto& record:records){
   auto asset=Find(r32(bytes,at+32));
   need(asset&&r32(bytes,at+36)==asset->asset&&r32(bytes,at+40)==asset->parameter,"MW toll stock parameter");
   for(unsigned j=0;j<3;++j)rf(bytes,at+16+4*j);
   memcpy(record.data(),bytes.data()+at,record.size());at+=record.size();
  }
  for(unsigned i=0;i<treeCount;++i){
   auto asset=Find(r32(bytes,at));auto len=r32(bytes,at+4);at+=8;
   need(asset&&len>=24&&len<=520&&(len-8)%16==0&&at+len<=bytes.size(),"MW toll tree extent");
   need(r32(bytes,at)==asset->model&&r32(bytes,at+4)==(len-8)/16,"MW toll tree model");
   need(definitions.emplace(asset->asset,B(bytes.begin()+at,bytes.begin()+at+len)).second,"MW toll tree identity");
   at+=len;
  }
  need(geometryBytes>=16&&bytes.size()-at==geometryBytes&&r32(bytes,at)==0x80134000&&r32(bytes,at+4)==geometryBytes-8,"MW toll native geometry extent");
  B geometry(bytes.begin()+at,bytes.end());
  models=std::move(geometry);trees=std::move(definitions);placements=std::move(records);
  Log("SOURCE_MW_TOLLS_ACTIVE placements=%u trees=%u stock_attrib=1",n,treeCount);
}

static void CheckTextures(w2v2disk::File& tpk){
 w2v2disk::need(tpk.size==91904&&tpk.fullHash()=="60cbc93ced5abd2a3b653639031a95a96fcc00f62e518e7db6dad571fa34b2f6","stock MW toll texture fingerprint");
}

#ifndef W2C_PUBLIC_BUILD
static bool Load(){
 using namespace w2v2disk;
 std::error_code ec;const auto path=std::filesystem::path(root)/payloadPath;
 if(!std::filesystem::exists(path,ec))return false;
 try{
  File input(path),tpk(std::filesystem::path(root)/texturePath);
  CheckTextures(tpk);
  Parse(input.read(0,input.size,128*1024));
  return true;
 }catch(const std::exception& error){Log("SOURCE_MW_TOLLS_SKIPPED reason=%s",error.what());return false;}
}
#endif
}
