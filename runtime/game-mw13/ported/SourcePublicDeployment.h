#pragma once

static std::string sourcePublicGeneration;
static std::string sourcePublicIndexSha;
static std::string sourcePublicQuality;
static std::string sourcePublicBuild;

static bool IsPublicDigest(const std::string& value) {
 return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){
  return (c>='0'&&c<='9')||(c>='a'&&c<='f');
 });
}

static bool SourceDeploymentAllowed() {
 try {
  using namespace w2v2disk;
  auto directory=std::filesystem::path(root)/"W2CMWPublic";
  need(!std::filesystem::exists(directory/"transaction.json"),"public W2C placement recovery required");
  File file(directory/"installation.json");
  auto manifest=parse(file.read(0,file.size,4096),4096);
  if(manifest.is_object()&&manifest.contains("rollbackSha256")){
   need(IsPublicDigest(str(manifest.at("rollbackSha256"),64)),"public W2C rollback identifier");
   manifest.erase("rollbackSha256");
  }
  if(manifest.is_object()&&manifest.contains("language")){
   str(manifest.at("language"),64);
   manifest.erase("language");
  }
  if(manifest.is_object()&&manifest.contains("targetCore")){
   const auto& core=manifest.at("targetCore");
   keys(core,{"TRACKS/L2RA.BUN","TRACKS/STREAML2RA.BUN","GLOBAL/InGameA.bun"});
   for(const auto* name:{"TRACKS/L2RA.BUN","TRACKS/STREAML2RA.BUN","GLOBAL/InGameA.bun"})
    need(IsPublicDigest(str(core.at(name),64)),"public W2C target resource fingerprint");
   manifest.erase("targetCore");
  }
  if(manifest.is_object()&&manifest.contains("streamSha256")){
   need(IsPublicDigest(str(manifest.at("streamSha256"),64)),"public W2C installed stream fingerprint");
   manifest.erase("streamSha256");
  }
  keys(manifest,{"schema","packageId","generation","quality","indexSha256","runtimeSha256"});
  need(uintv(manifest.at("schema"))==1&&str(manifest.at("packageId"))=="W2CMWPublic",
       "public W2C installation manifest");
  sourcePublicBuild=str(manifest.at("generation"),64);
  sourcePublicIndexSha=str(manifest.at("indexSha256"),64);
  sourcePublicQuality=str(manifest.at("quality"),8);
  need(IsPublicDigest(sourcePublicBuild)&&IsPublicDigest(sourcePublicIndexSha),"public W2C generation identifiers");
  need(sourcePublicQuality=="NORMAL"||sourcePublicQuality=="WORLD"||sourcePublicQuality=="X360","public W2C quality");
  sourcePublicGeneration="W2CMWPublic\\generations\\"+sourcePublicBuild+"\\";
  return true;
 } catch(const std::exception& error) {
  ++v2Failures;
  W2V2Status.error=10;
  Log("PUBLIC_INSTALLATION_REFUSED reason=%s",error.what());
  return false;
 }
}
