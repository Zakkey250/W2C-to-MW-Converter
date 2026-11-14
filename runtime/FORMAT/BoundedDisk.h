#pragma once
// Frozen copy of the accepted V2 independent disk validator; no game dependencies.
// Independent bounded disk reader. Does not include the Python writer, old
// World2MW headers, game addresses, GPU code, save state or event adapters.

#include <windows.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <lz4/lz4.h>
#pragma comment(lib,"advapi32.lib")
namespace w2v2disk {
using J=nlohmann::json;using B=std::vector<unsigned char>;using U=std::uint64_t;
namespace fs=std::filesystem;
constexpr U MiB=1048576,MaxBlock=64*MiB,MaxIndex=16*MiB,MaxMeta=65536,MaxGeneration=80*1024*MiB;
static_assert(sizeof(std::streamoff)>=8,"64-bit seek required even on Win32");
static void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void need(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
static U add(U a,U b){need(a<=UINT64_MAX-b,"offset addition overflow");return a+b;}
static U mul(U a,U b){need(!b||a<=UINT64_MAX/b,"length multiplication overflow");return a*b;}
static unsigned r16(const B& b,size_t p){need(p<=b.size()&&b.size()-p>=2,"u16 range");return unsigned(b[p])|(unsigned(b[p+1])<<8);}
static unsigned r32(const B& b,size_t p){need(p<=b.size()&&b.size()-p>=4,"u32 range");return unsigned(b[p])|(unsigned(b[p+1])<<8)|(unsigned(b[p+2])<<16)|(unsigned(b[p+3])<<24);}
static U r64(const B& b,size_t p){return U(r32(b,p))|(U(r32(b,p+4))<<32);}
static float rf(const B& b,size_t p){auto bits=r32(b,p);float value;memcpy(&value,&bits,4);need(std::isfinite(value),"non-finite binary float");return value;}
static std::string hex(const unsigned char* p,size_t n){static const char digits[]="0123456789abcdef";std::string out;out.reserve(n*2);for(size_t i=0;i<n;++i){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;}
static B unhex(const std::string& s,U maximum){need(s.size()%2==0&&s.size()/2<=maximum,"hex length budget");B out;out.reserve(s.size()/2);for(size_t i=0;i<s.size();i+=2){auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("non-canonical hex");};out.push_back(static_cast<unsigned char>(digit(s[i])*16+digit(s[i+1])));}return out;}
struct Hash {
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;
 Hash(){need(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash),"SHA256 initialization");}
 ~Hash(){if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);}
 void append(const void* p,size_t size){need(size<=UINT32_MAX&&CryptHashData(hash,static_cast<const BYTE*>(p),DWORD(size),0),"SHA256 update");}
 std::string finish(){unsigned char out[32];DWORD n=32;need(CryptGetHashParam(hash,HP_HASHVAL,out,&n,0)&&n==32,"SHA256 result");return hex(out,32);}
};
static std::string sha(const B& bytes,const char* domain=nullptr){Hash h;if(domain)h.append(domain,strlen(domain)+1);if(!bytes.empty())h.append(bytes.data(),bytes.size());return h.finish();}
static unsigned crc(const B& bytes){static const auto table=[](){std::array<unsigned,256> t{};for(unsigned i=0;i<256;++i){unsigned v=i;for(unsigned n=0;n<8;++n)v=(v>>1)^((v&1)?0xedb88320:0);t[i]=v;}return t;}();unsigned v=~0u;for(auto b:bytes)v=table[(v^b)&255]^(v>>8);return ~v;}
static bool zeros(const B& b,size_t start,size_t count){need(start<=b.size()&&count<=b.size()-start,"reserved range");return std::all_of(b.begin()+start,b.begin()+start+count,[](auto c){return c==0;});}
struct File {
 std::ifstream f;U size=0;
 explicit File(const fs::path& path):f(path,std::ios::binary){auto attr=GetFileAttributesW(path.c_str());need(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT),"pack absent/reparse point");need(f.good(),"open pack");size=fs::file_size(path);need(size<=INT64_MAX,"unsupported file extent");}
 B read(U offset,U length,U budget){need(length<=budget&&offset<=size&&length<=size-offset,"bounded file region");B b(static_cast<size_t>(length));if(length){f.clear();f.seekg(static_cast<std::streamoff>(offset));need(f.good(),"64-bit seek");f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(length));need(U(f.gcount())==length,"truncated read");}return b;}
 std::string fullHash(){Hash h;std::array<char,65536> block{};f.clear();f.seekg(0);while(f){f.read(block.data(),block.size());auto n=f.gcount();if(n)h.append(block.data(),size_t(n));}need(f.eof(),"file hash read");return h.finish();}
};
static void keys(const J& j,std::initializer_list<const char*> names){need(j.is_object()&&j.size()==names.size(),"unknown/missing schema field");for(auto n:names)need(j.contains(n),std::string("missing field ")+n);}
static U uintv(const J& j,U max=UINT32_MAX){need(j.is_number_integer()&&!j.is_boolean(),"unsigned integer type");if(j.is_number_integer()&&!j.is_number_unsigned())need(j.get<std::int64_t>()>=0,"negative unsigned integer");auto v=j.get<U>();need(v<=max,"integer limit");return v;}
static double number(const J& j){need(j.is_number()&&!j.is_boolean(),"finite number type");auto d=j.get<double>();need(std::isfinite(d)&&std::abs(d)<=std::numeric_limits<float>::max(),"non-finite/unrepresentable float metadata");return d;}
static std::string str(const J& j,size_t maximum=240){need(j.is_string(),"string type");auto s=j.get<std::string>();need(s.size()<=maximum&&s.find('\0')==std::string::npos,"bounded string");return s;}
static void requireBool(const J& j){need(j.is_boolean(),"boolean type");}
static bool oneof(const std::string& s,std::initializer_list<const char*> values){for(auto v:values)if(s==v)return true;return false;}
static void vec(const J& j,size_t n){need(j.is_array()&&j.size()==n,"vector dimension");for(auto& v:j)number(v);}
static void strings(const J& j,size_t max=1024){need(j.is_array()&&j.size()<=max,"string array bound");for(auto& s:j)str(s);}
static void bounds(const J& j){vec(j,6);for(unsigned a=0;a<3;++a)need(number(j[a])<=number(j[a+3]),"inverted bounds");}
static double matrix(const J& j){vec(j,16);need(number(j[3])==0&&number(j[7])==0&&number(j[11])==0&&number(j[15])==1,"affine row-vector matrix");return number(j[0])*(number(j[5])*number(j[10])-number(j[6])*number(j[9]))-number(j[1])*(number(j[4])*number(j[10])-number(j[6])*number(j[8]))+number(j[2])*(number(j[4])*number(j[9])-number(j[5])*number(j[8]));}
static J parse(const B& bytes,U maximum){need(bytes.size()<=maximum,"JSON byte budget");std::vector<std::set<std::string>> names(27);U nodes=0;auto cb=[&](int depth,J::parse_event_t event,J& value){need(depth>=0&&depth<25,"JSON depth");need(++nodes<=2000000,"JSON node budget");if(event==J::parse_event_t::object_start)names[depth+1].clear();if(event==J::parse_event_t::key)need(names[depth].insert(value.get<std::string>()).second,"duplicate JSON key");return true;};return J::parse(bytes.begin(),bytes.end(),cb);}
#include "geometry_page.h"
#include "paged_assets.h"
static unsigned packFor(unsigned kind){switch(kind){case 1:case 2:case 3:case 10:case 11:return 1;case 20:case 21:return 2;case 30:case 31:case 32:case 34:return 3;case 40:case 41:return 4;case 12:case 33:case 50:case 51:case 52:case 53:case 54:return 5;case 60:return 6;default:throw std::runtime_error("unknown record kind");}}
struct Dep {unsigned pack,role;std::string id;};
struct Record {std::string id,source,payloadHash,metaHash,depsHash;unsigned kind=0,count=0,stride=0;U decoded=0,metaBytes=0;J meta,details;std::vector<Dep> deps;};
struct PackData {unsigned kind=0,minor=0,schema=0,compiler=0;U features=0;U bytes=0;std::string source,semantic,build,manifest;std::vector<Record> records;};
static unsigned countRole(const Record& r,unsigned role){return unsigned(std::count_if(r.deps.begin(),r.deps.end(),[&](const auto& d){return d.role==role;}));}
static void onlyRoles(const Record& r,std::initializer_list<unsigned> roles){for(auto& d:r.deps)need(std::find(roles.begin(),roles.end(),d.role)!=roles.end(),"unexpected dependency role");}
static void payloadEmpty(const Record& r,const B& b){need(b.empty()&&r.stride==0,"unexpected typed payload");}
static void positions(const B& payload,U count,U stride,const J& box){for(U v=0;v<count;++v)for(unsigned axis=0;axis<3;++axis){double x=rf(payload,size_t(v*stride+axis*4)),lo=number(box[axis]),hi=number(box[axis+3]);double tolerance=1e-6*std::max({1.,std::abs(lo),std::abs(hi)});need(x>=lo-tolerance&&x<=hi+tolerance,"vertex outside bounds");}}
static void wmc(const Record& r,const B& b){
 need(b.size()>=20&&r32(b,0)==0x31434d57&&r32(b,4)==1,"WMC identity");U triangles=r32(b,8),nodes=r32(b,12),surfaces=r32(b,16);
 need(triangles>0&&triangles<=4000000&&nodes>0&&nodes<1000000&&surfaces>0&&surfaces<1000,"WMC counts");
 need(triangles==uintv(r.meta["triangleCount"])&&nodes==uintv(r.meta["nodeCount"])&&surfaces==uintv(r.meta["surfaceCount"])&&r.count==triangles&&r.stride==44,"WMC metadata counts");
 U nodeStart=add(20,mul(surfaces,80)),triangleStart=add(nodeStart,mul(nodes,32));need(add(triangleStart,mul(triangles,44))==b.size(),"WMC exact length");
 for(U s=0;s<surfaces;++s){bool terminated=false;for(U k=0;k<80;++k)terminated|=b[size_t(20+s*80+k)]==0;need(terminated,"WMC surface name");}
 for(U t=0;t<triangles;++t){auto at=size_t(triangleStart+t*44);for(unsigned v=0;v<9;++v)rf(b,at+v*4);need(r32(b,at+36)<surfaces&&r32(b,at+40)<=1,"WMC surface/wall reference");}
 std::vector<unsigned char> seen(size_t(nodes),0),covered(size_t(triangles),0);std::vector<std::pair<unsigned,unsigned>> pending{{0,0}};U leaves=0;
 while(!pending.empty()){auto [index,depth]=pending.back();pending.pop_back();need(index<nodes&&depth<128&&!seen[index],"WMC cyclic/deep/shared BVH");seen[index]=1;size_t at=size_t(nodeStart+U(index)*32);float lo[3],hi[3];for(unsigned a=0;a<3;++a){lo[a]=rf(b,at+a*4);hi[a]=rf(b,at+12+a*4);need(lo[a]<=hi[a],"WMC inverted node");}
  auto left=r32(b,at+24),right=r32(b,at+28);
  if(right&0x80000000){U n=right&0x7fffffff;need(n>0&&left<triangles&&n<=triangles-left,"WMC leaf range");leaves+=n;
   for(U t=left;t<left+n;++t){need(!covered[size_t(t)],"WMC duplicate leaf triangle");covered[size_t(t)]=1;for(unsigned v=0;v<3;++v)for(unsigned a=0;a<3;++a){float x=rf(b,size_t(triangleStart+t*44+v*12+a*4));need(x>=lo[a]-.01f&&x<=hi[a]+.01f,"WMC leaf bounds");}}
  }else{need(left<nodes&&right<nodes,"WMC child range");for(auto child:{left,right}){size_t c=size_t(nodeStart+U(child)*32);for(unsigned a=0;a<3;++a)need(rf(b,c+a*4)>=lo[a]-.01f&&rf(b,c+12+a*4)<=hi[a]+.01f,"WMC parent bounds");pending.emplace_back(child,depth+1);}}
 }
 need(std::all_of(seen.begin(),seen.end(),[](auto v){return v==1;})&&leaves==triangles,"WMC coverage");
}
static void typed(Record& r,const B& b){
 const auto& m=r.meta;need(uintv(m.at("schema"))==1&&str(m.at("sourceKey"))==r.source&&!r.source.empty(),"record schema/source");
 switch(r.kind){
 case 1:{keys(m,{"schema","sourceKey","coordinateConvention","semanticEncoding"});need(str(m["coordinateConvention"])=="mw-engine-xyz-r1"&&str(m["semanticEncoding"])=="canonical-json-utf8","world coordinate contract");need(r.count==1&&r.stride==0&&!b.empty()&&b.size()<=MaxMeta,"world semantic bytes");auto semanticDocument=parse(b,MaxMeta);need(semanticDocument.is_object()&&uintv(semanticDocument.at("coordinateRevision"))==1,"semantic document coordinate revision");onlyRoles(r,{9});break;}
 case 2:{keys(m,{"schema","sourceKey","bounds","overlapGroup","draws"});bounds(m["bounds"]);str(m["overlapGroup"]);strings(m["draws"]);payloadEmpty(r,b);onlyRoles(r,{1});need(r.count==m["draws"].size()&&r.deps.size()==r.count,"section ordered draw references");break;}
 case 3:{keys(m,{"schema","sourceKey","sourceSection","bounds","page"});uintv(m["sourceSection"]);bounds(m["bounds"]);str(m["page"]);payloadEmpty(r,b);onlyRoles(r,{10});need(r.count==1&&r.deps.size()==1,"section page cardinality");break;}
 case 11:{keys(m,{"schema","sourceKey","encoding","recordCount","animationCount","sourceBinarySha256"});need(str(m["encoding"])=="frozen-effective-materials-v1"&&unhex(str(m["sourceBinarySha256"],64),32).size()==32&&r.stride==0&&r.deps.empty(),"legacy material encoding");r.details=readEffectiveMaterials(b);need(uintv(m["recordCount"])==uintv(r.details["recordCount"])&&uintv(m["animationCount"])==uintv(r.details["animationCount"])&&r.count==uintv(m["recordCount"]),"legacy material counts");break;}
 case 12:{keys(m,{"schema","sourceKey","sourceClass","sourceName","encoding","sourceSha256"});need(str(m["encoding"])=="original-bytes-v1"&&str(m["sourceSha256"],64)==r.payloadHash&&r.count==1&&r.stride==0&&r.deps.empty(),"source archive identity");str(m["sourceName"]);r.details=validateSourceArchive(b,str(m["sourceClass"]));break;}
 case 21:{keys(m,{"schema","sourceKey","sourceSection","pageVersion","vertexLayout","clusterCount","sourceVertexCount","sourceIndexCount","sourceMeshCount","shadowIndexCount","bounds","legacyOracleSha256","geometrySemanticSha256"});need(uintv(m["pageVersion"])==1&&str(m["vertexLayout"])=="P3N3C4UV2S1"&&r.stride==0&&unhex(str(m["geometrySemanticSha256"],64),32).size()==32,"geometry page metadata");auto p=validateGeometryPage(b);bounds(m["bounds"]);for(unsigned a=0;a<6;++a)need(number(m["bounds"][a])==rf(b,112+a*4),"page metadata bounds");need(uintv(m["sourceSection"])==p.section&&uintv(m["clusterCount"])==p.clusters&&r.count==p.clusters&&uintv(m["sourceVertexCount"])==p.sourceVertices&&uintv(m["sourceIndexCount"])==p.indices&&uintv(m["sourceMeshCount"])==p.draws&&uintv(m["shadowIndexCount"])==p.shadow&&str(m["legacyOracleSha256"],64)==p.oracleSha,"geometry page counts/oracle");onlyRoles(r,{11});need(r.deps.size()==1,"geometry page material bank");J passes=J::object();for(auto& [hash,mask]:p.texturePasses)passes[std::to_string(hash)]=mask;r.details={{"materialBankSha256",p.bankSha},{"textures",p.textureHashes},{"owners",p.owners},{"maximumLight",p.maximumLight},{"hasLight",p.hasLight},{"texturePasses",passes},{"maximumClusterTotalBytes",p.maximumBytes}};break;}
 case 10:{keys(m,{"schema","sourceKey","blend","alphaTest","alphaRef","alphaFunc","cull","depthWrite","depthTest","depthBias","samplers","uvAnimation","nightLayer","passes","shadowEligible","sourceFlags"});payloadEmpty(r,b);need(r.count==1,"material count");need(oneof(str(m["blend"]),{"opaque","additive","alpha_over"})&&oneof(str(m["alphaFunc"]),{"never","less","equal","less_equal","greater","not_equal","greater_equal","always"})&&oneof(str(m["cull"]),{"none","cw","ccw"}),"material state enum");for(auto k:{"alphaTest","depthWrite","depthTest","nightLayer","shadowEligible"})requireBool(m[k]);uintv(m["alphaRef"],255);uintv(m["sourceFlags"]);number(m["depthBias"]);
  need(m["samplers"].is_array()&&m["samplers"].size()<=8,"sampler count");std::set<U> slots;for(auto& s:m["samplers"]){keys(s,{"slot","min","mag","mip","addressU","addressV","maxAnisotropy","mipBias"});need(slots.insert(uintv(s["slot"],7)).second,"duplicate sampler slot");for(auto k:{"min","mag","mip"})need(oneof(str(s[k]),{"point","linear","anisotropic"}),"sampler filter");for(auto k:{"addressU","addressV"})need(oneof(str(s[k]),{"wrap","mirror","clamp","border"}),"sampler address");need(uintv(s["maxAnisotropy"],16)>=1,"anisotropy range");number(s["mipBias"]);}
  need(m["passes"].is_array()&&!m["passes"].empty()&&m["passes"].size()<=5,"render pass list");std::set<std::string> passes;for(auto& p:m["passes"]){auto v=str(p);need(oneof(v,{"main","mirror","cubemap","road_reflection","shadow"})&&passes.insert(v).second,"render pass enum/duplicate");}
  onlyRoles(r,{3,4,5});need(countRole(r,3)<=1&&countRole(r,4)<=1,"material texture role cardinality");if(!m["uvAnimation"].is_null()){auto& a=m["uvAnimation"];keys(a,{"fps","loop","frameCount"});need(number(a["fps"])>0&&uintv(a["frameCount"],1024)>0&&uintv(a["frameCount"])==countRole(r,5),"UV animation frames");requireBool(a["loop"]);}else need(countRole(r,5)==0,"undeclared animation frames");break;}
 case 20:{keys(m,{"schema","sourceKey","vertexCount","indexCount","shadowIndexCount","bounds","drawOrder","memberships","overlapGroup"});U v=uintv(m["vertexCount"],65535),i=uintv(m["indexCount"]),s=uintv(m["shadowIndexCount"]);need(v>0&&i>0&&i%3==0&&s%3==0&&r.count==v&&r.stride==40,"40B geometry dimensions");need(add(mul(v,40),mul(add(i,s),2))==b.size()&&add(add(b.size(),r.metaBytes),mul(r.deps.size(),40))<=MiB,"cluster byte budget");bounds(m["bounds"]);uintv(m["drawOrder"]);strings(m["memberships"]);str(m["overlapGroup"]);positions(b,v,40,m["bounds"]);for(U n=0;n<v;++n)for(unsigned offset:{0u,4u,8u,12u,16u,20u,28u,32u,36u})rf(b,size_t(n*40+offset));for(U n=0;n<i+s;++n)need(r16(b,size_t(v*40+n*2))<v,"16bit index out of range");onlyRoles(r,{2});need(r.deps.size()==1&&countRole(r,2)==1,"geometry material reference");break;}
 case 30:{keys(m,{"schema","sourceKey","format","width","height","mips","quality","sourceTextureHash","sourceFlags"});auto format=str(m["format"]);need(oneof(format,{"DXT1","DXT3","DXT5","A8R8G8B8","R8G8B8","A8","L8"}),"texture format");U w=uintv(m["width"],16384),h=uintv(m["height"],16384);need(w&&h&&r.stride==0&&oneof(str(m["quality"]),{"full","coarse"}),"texture dimensions/quality");need(unhex(str(m["sourceTextureHash"]),4).size()==4,"source texture hash");uintv(m["sourceFlags"]);need(m["mips"].is_array()&&!m["mips"].empty()&&m["mips"].size()<=15&&r.count==m["mips"].size(),"mip count");U offset=0;bool smallest=false;for(auto& mip:m["mips"]){keys(mip,{"width","height","offset","bytes"});need(!smallest&&uintv(mip["width"])==w&&uintv(mip["height"])==h&&uintv(mip["offset"],UINT64_MAX)==offset,"mip dimensions/offset");U size=format[0]=='D'?mul(mul((w+3)/4,(h+3)/4),format=="DXT1"?8:16):mul(mul(w,h),format=="A8R8G8B8"?4:format=="R8G8B8"?3:1);need(uintv(mip["bytes"],UINT64_MAX)==size,"mip block byte count");offset=add(offset,size);smallest=w==1&&h==1;w=std::max<U>(1,w/2);h=std::max<U>(1,h/2);}need(offset==b.size()&&r.deps.empty(),"texture exact bytes/deps");break;}
 case 31:{keys(m,{"schema","sourceKey","quality","sourceTextureHash"});need(oneof(str(m["quality"]),{"full","coarse"})&&unhex(str(m["sourceTextureHash"]),4).size()==4&&r.count==1,"texture alias metadata");payloadEmpty(r,b);onlyRoles(r,{7});need(r.deps.size()==1,"alias reference");break;}
 case 32:{keys(m,{"schema","sourceKey","quality","sourceTextureHash","sourceSha256","width","height","mipCount","pixelFormat"});need(str(m["quality"])=="full"&&unhex(str(m["sourceTextureHash"]),4).size()==4&&str(m["sourceSha256"],64)==r.payloadHash&&r.stride==0&&r.deps.empty(),"DDS source identity");r.details=validateDdsSource(b);for(auto key:{"width","height","mipCount","pixelFormat"})need(m[key]==r.details[key],"DDS metadata mismatch");need(r.count==uintv(m["mipCount"]),"DDS mip count");break;}
 case 34:{keys(m,{"schema","sourceKey","quality","sourceTextureHash","sourceSha256"});need(str(m["quality"])=="full"&&unhex(str(m["sourceTextureHash"]),4).size()==4&&unhex(str(m["sourceSha256"],64),32).size()==32&&r.count==1,"DDS alias source metadata");payloadEmpty(r,b);onlyRoles(r,{7});need(r.deps.size()==1,"DDS alias reference");break;}
 case 33:{keys(m,{"schema","sourceKey","encoding","sourceSymbol","sourceSha256"});need(str(m["encoding"])=="d3d9-bytecode-v1"&&str(m["sourceSha256"],64)==r.payloadHash&&!str(m["sourceSymbol"]).empty()&&r.stride==4&&r.count*U(4)==b.size()&&b.size()>=8&&b.size()<=MiB&&r.deps.empty(),"shader source identity/size");unsigned token=r32(b,0);need(((token>>16)==0xfffe||(token>>16)==0xffff)&&((token>>8)&255)>=2&&((token>>8)&255)<=3&&r32(b,b.size()-4)==0xffff,"D3D9 shader framing");break;}
 case 40:{keys(m,{"schema","sourceKey","bounds","pieces","sourceModelHash"});bounds(m["bounds"]);need(unhex(str(m["sourceModelHash"]),4).size()==4,"model source hash");payloadEmpty(r,b);need(m["pieces"].is_array()&&!m["pieces"].empty()&&m["pieces"].size()<=1024&&r.count==m["pieces"].size()&&r.deps.size()==r.count,"model piece count");onlyRoles(r,{1});for(size_t i=0;i<m["pieces"].size();++i){auto& p=m["pieces"][i];keys(p,{"geometry","parent","matrix","sourceLod","nativeConnection"});str(p["geometry"]);need(p["parent"].is_number_integer(),"piece parent type");if(p["parent"].is_number_unsigned())need(p["parent"].get<U>()<=INT64_MAX,"piece parent integer overflow");auto parent=p["parent"].get<std::int64_t>();need(parent>=-1&&parent<std::int64_t(i),"piece hierarchy cycle/order");need(matrix(p["matrix"])!=0,"singular piece transform");uintv(p["sourceLod"]);str(p["nativeConnection"]);}break;}
 case 41:{keys(m,{"schema","sourceKey","matrix","negativeDeterminant","memberships","overlapGroup"});double det=matrix(m["matrix"]);requireBool(m["negativeDeterminant"]);need(det!=0&&(det<0)==m["negativeDeterminant"].get<bool>(),"placement determinant");strings(m["memberships"]);str(m["overlapGroup"]);payloadEmpty(r,b);need(r.count==1&&r.deps.size()==1,"placement count/model");onlyRoles(r,{6});break;}
 case 50:case 51:{if(r.kind==50)keys(m,{"schema","sourceKey","position","color","radius","intensity","sourceFlags"});else keys(m,{"schema","sourceKey","position","color","size","sourceFlags"});vec(m["position"],3);vec(m["color"],4);uintv(m["sourceFlags"]);payloadEmpty(r,b);need(r.count==1,"light/flare count");if(r.kind==50){need(number(m["radius"])>0&&number(m["intensity"])>=0&&r.deps.empty(),"light semantics");}else{need(number(m["size"])>0&&r.deps.size()==1,"flare texture");onlyRoles(r,{3});}break;}
 case 52:{keys(m,{"schema","sourceKey","position","bounds","rate","lifetime","sourceFlags","recordVersion"});vec(m["position"],3);bounds(m["bounds"]);need(number(m["rate"])>=0&&number(m["lifetime"])>0&&uintv(m["recordVersion"])==1,"emitter schema");uintv(m["sourceFlags"]);need(r.count==1&&r.stride==0&&b.size()<=MaxMeta,"bounded retained emitter data");onlyRoles(r,{3,4});break;}
 case 53:{keys(m,{"schema","sourceKey","frameCount","fps","loop","encoding"});U n=uintv(m["frameCount"],4096);need(n>0&&number(m["fps"])>0&&str(m["encoding"])=="matrix4x4-f32"&&mul(n,64)==b.size()&&r.count==n&&r.stride==64,"animation dimensions");requireBool(m["loop"]);for(U i=0;i<n*16;++i)rf(b,size_t(i*4));onlyRoles(r,{8});need(r.deps.size()==1,"animation target");break;}
 case 54:{keys(m,{"schema","sourceKey","vertexCount","indexCount","bounds","sourceFlags"});U v=uintv(m["vertexCount"],65535),i=uintv(m["indexCount"]);need(v>=3&&i>0&&i%3==0&&r.count==v&&r.stride==12&&add(mul(v,12),mul(i,4))==b.size(),"barrier geometry shape");bounds(m["bounds"]);positions(b,v,12,m["bounds"]);uintv(m["sourceFlags"]);for(U n=0;n<i;++n)need(r32(b,size_t(v*12+n*4))<v,"barrier index");need(r.deps.empty(),"barrier dependencies");break;}
 case 60:{keys(m,{"schema","sourceKey","encoding","triangleCount","nodeCount","surfaceCount"});need(str(m["encoding"])=="wmc1"&&r.deps.empty(),"collision encoding/deps");wmc(r,b);break;}
 default:throw std::runtime_error("unsupported typed record");
 }
}
static std::string assetId(const std::string& source,unsigned kind,const std::string& key){
 Hash h;constexpr char domain[]="W2CMWv2.Asset";h.append(domain,sizeof(domain));auto s=unhex(source,32);h.append(s.data(),s.size());unsigned char k[4]={static_cast<unsigned char>(kind),static_cast<unsigned char>(kind>>8),static_cast<unsigned char>(kind>>16),static_cast<unsigned char>(kind>>24)};h.append(k,4);h.append(key.data(),key.size());return h.finish();
}
static PackData loadPack(const fs::path& path,U& indexUsed){
 File file(path);need(file.size>=256,"truncated pack header");need(file.size<=MaxGeneration,"pack file byte budget");auto h=file.read(0,256,256);
 need(!memcmp(h.data(),"W2MWPK2\0",8)&&r16(h,8)==2&&r16(h,10)<=1,"format magic/version");
 need(r32(h,12)==256&&r32(h,16)>=1&&r32(h,16)<=6&&((r16(h,10)==0&&r32(h,20)==1&&r32(h,184)==1&&r64(h,24)==0)||(r16(h,10)==1&&r32(h,20)==2&&r32(h,184)==2&&r64(h,24)==3)),"header schema/required feature");
 need(r64(h,32)==file.size&&r32(h,52)==128&&r32(h,188)==1&&zeros(h,200,56),"header size/stride/revision/reserved");
 auto expectedCrc=r32(h,192);std::fill(h.begin()+192,h.begin()+196,static_cast<unsigned char>(0));need(crc(h)==expectedCrc,"header CRC");
 PackData out;out.kind=r32(h,16);out.minor=r16(h,10);out.schema=r32(h,20);out.compiler=r32(h,184);out.features=r64(h,24);out.bytes=file.size;out.source=hex(h.data()+56,32);out.semantic=hex(h.data()+88,32);out.build=hex(h.data()+120,32);out.manifest=hex(h.data()+152,32);
 for(size_t offset:{56u,88u,120u,152u})need(!zeros(h,offset,32),"zero generation identity");
 U count=r32(h,48),tableAt=r64(h,40),tableBytes=mul(count,128);need(count<=65536&&tableAt>=256&&tableAt%8==0,"record table count/alignment");
 indexUsed=add(indexUsed,tableBytes);need(indexUsed<=MaxIndex,"aggregate index budget");auto table=file.read(tableAt,tableBytes,MaxIndex);need(crc(table)==r32(h,196),"record table CRC");
 std::vector<std::pair<U,U>> regions{{0,256}};if(tableBytes)regions.emplace_back(tableAt,add(tableAt,tableBytes));
 auto region=[&](U offset,U length,U budget,U alignment){if(!length){need(offset==0,"empty region offset");return B{};}need(offset>=256&&offset%alignment==0,"record region alignment");auto end=add(offset,length);need(end<=file.size,"record region extent");regions.emplace_back(offset,end);return file.read(offset,length,budget);};
 std::set<std::string> ids;std::string prior;
 for(U i=0;i<count;++i){size_t at=size_t(i*128);Record r;r.id=hex(table.data()+at,32);r.kind=r32(table,at+32);need((out.schema==2||(r.kind!=3&&r.kind!=11&&r.kind!=12&&r.kind!=21&&r.kind!=32&&r.kind!=33&&r.kind!=34))&&packFor(r.kind)==out.kind&&r32(table,at+36)==0&&zeros(table,at+112,16),"record kind/flags/reserved");need(ids.insert(r.id).second&&(prior.empty()||prior<r.id),"duplicate/unsorted asset ID");prior=r.id;
  U payloadAt=r64(table,at+40),stored=r64(table,at+48);r.decoded=r64(table,at+56);U metaAt=r64(table,at+64);r.metaBytes=r32(table,at+72);U deps=r32(table,at+76),depsAt=r64(table,at+80);r.count=r32(table,at+88);r.stride=r32(table,at+92);
  need(r32(table,at+96)==0&&stored==r.decoded&&stored<=MaxBlock,"unknown codec/decompression budget");need(r.metaBytes>0&&r.metaBytes<=MaxMeta&&deps<=1024,"metadata/dependency limit");indexUsed=add(indexUsed,add(r.metaBytes,mul(deps,40)));need(indexUsed<=MaxIndex,"aggregate index budget");
  auto meta=region(metaAt,r.metaBytes,MaxMeta,8);need(crc(meta)==r32(table,at+104),"metadata CRC");r.metaHash=sha(meta);r.meta=parse(meta,MaxMeta);r.source=str(r.meta.at("sourceKey"));need(assetId(out.source,r.kind,r.source)==r.id,"stable asset ID/source key mismatch");
  auto links=region(depsAt,mul(deps,40),40960,8);need(crc(links)==r32(table,at+108),"dependency CRC");r.depsHash=sha(links);for(U n=0;n<deps;++n){size_t p=size_t(n*40);auto pack=r32(links,p),role=r32(links,p+4);need(pack>=1&&pack<=6&&role>=1&&role<=11,"dependency enum");r.deps.push_back({pack,role,hex(links.data()+p+8,32)});}
  {auto payload=region(payloadAt,stored,MaxBlock,64);need(crc(payload)==r32(table,at+100),"payload CRC");r.payloadHash=sha(payload);typed(r,payload);if(r.kind==1)need(sha(payload,"W2CMWv2.Semantic")==out.semantic,"WORLD semantic identity");}
  out.records.push_back(std::move(r));
 }
 std::sort(regions.begin(),regions.end());for(size_t i=1;i<regions.size();++i)need(regions[i-1].second<=regions[i].first,"overlapping pack regions");need(regions.back().second==file.size,"unowned trailing bytes");return out;
}
static bool targetKind(unsigned role,unsigned kind){switch(role){case 1:return kind==20;case 2:return kind==10;case 3:case 4:case 5:case 7:return kind==30||kind==31||kind==32||kind==34;case 6:return kind==40;case 8:return kind==20||kind==40||kind==41;case 9:return kind==60;case 10:return kind==21;case 11:return kind==11;}return false;}
static J pagedGeneration(const std::map<std::string,Record*>& records){
 Record* bank=nullptr;Record* overlap=nullptr;Record* lights=nullptr;std::map<unsigned,Record*> pages,sections,textures;U maximum=0;
 for(auto& [id,r]:records){
  if(r->kind==11){need(!bank,"duplicate effective material bank");bank=r;}
  if(r->kind==21){need(pages.emplace(unsigned(uintv(r->meta["sourceSection"])),r).second,"duplicate page section");maximum=std::max(maximum,uintv(r->details["maximumClusterTotalBytes"]));}
  if(r->kind==3)need(sections.emplace(unsigned(uintv(r->meta["sourceSection"])),r).second,"duplicate stream section");
  if(r->kind==32||r->kind==34){auto hash=unsigned(std::stoul(str(r->meta["sourceTextureHash"]),nullptr,16));need(textures.emplace(hash,r).second,"duplicate source texture hash");if(r->kind==34){auto target=records.at(r->deps[0].id);need((target->kind==32||target->kind==34)&&r->meta["sourceSha256"]==target->meta["sourceSha256"],"DDS alias content mismatch");}}
  if(r->kind==12&&r->meta["sourceClass"]=="overlap-groups"){need(!overlap,"duplicate overlap source");overlap=r;}
  if(r->kind==12&&r->meta["sourceClass"]=="lights"){need(!lights,"duplicate light source");lights=r;}
 }
 need(bank&&pages.size()>0&&pages.size()==sections.size(),"paged world section/bank coverage");auto& materials=bank->details["materials"];
 auto requireTexture=[&](unsigned hash){need(!hash||hash==0xD2E09151||hash==0x29D9478C||textures.count(hash),"unresolved paged texture dependency: "+std::to_string(hash));};
 for(auto& frame:bank->details["frames"])requireTexture(unsigned(uintv(frame)));
 for(auto& [section,page]:pages){need(sections.count(section),"page missing stream section");auto s=sections.at(section);need(s->deps[0].id==page->id&&s->meta["page"]==page->source&&s->meta["bounds"]==page->meta["bounds"],"section page identity/bounds");need(page->deps[0].id==bank->id&&page->details["materialBankSha256"]==bank->payloadHash,"mixed effective material bank");
  for(auto& tex:page->details["textures"]){auto hash=unsigned(uintv(tex));requireTexture(hash);auto key=std::to_string(hash);auto& material=materials.contains(key)?materials[key]:bank->details["default"];requireTexture(unsigned(uintv(material["mask"])));need(uintv(page->details["texturePasses"][key])==(uintv(material["blend"])!=0?2u:1u),"material opacity/pass mismatch");}
  if(page->details["hasLight"].get<bool>())need(lights&&uintv(page->details["maximumLight"])<uintv(lights->details["count"]),"unresolved static light candidate");
  for(auto& value:page->details["owners"]){auto owner=uintv(value);if(!owner||owner==UINT32_MAX)continue;need(overlap&&overlap->details["groups"].contains(std::to_string(owner)),"unresolved overlap owner");auto& members=overlap->details["groups"][std::to_string(owner)];need(std::find(members.begin(),members.end(),J(section))!=members.end(),"overlap owner missing source section");}
 }
 if(overlap)for(auto& group:overlap->details["groups"].items())for(auto& member:group.value())need(pages.count(unsigned(uintv(member))),"overlap references missing section");
 return {{"sections",pages.size()},{"sourceTextures",textures.size()},{"maximumClusterTotalBytes",maximum}};
}
static J generation(const fs::path& root){
 File manifestFile(root/L"generation.json");auto rawManifest=manifestFile.read(0,manifestFile.size,64*MiB);auto m=parse(rawManifest,64*MiB);
 keys(m,{"schema","sourceWorldId","semanticWorldId","buildId","sourceManifestHex","semanticDocumentHex","buildRecipeHex","files"});need(uintv(m["schema"])==1,"generation manifest schema");
 auto sourceBytes=unhex(str(m["sourceManifestHex"],2*MaxMeta),MaxMeta),semanticBytes=unhex(str(m["semanticDocumentHex"],2*MaxMeta),MaxMeta),recipeBytes=unhex(str(m["buildRecipeHex"],32*MiB),16*MiB);
 auto source=sha(sourceBytes,"W2CMWv2.Source"),semantic=sha(semanticBytes,"W2CMWv2.Semantic"),build=sha(recipeBytes,"W2CMWv2.Build");need(str(m["sourceWorldId"],64)==source&&str(m["semanticWorldId"],64)==semantic&&str(m["buildId"],64)==build,"generation identity digest");
 auto origin=parse(sourceBytes,MaxMeta);keys(origin,{"variant","files"});need(!str(origin["variant"]).empty()&&origin["files"].is_array()&&!origin["files"].empty(),"source manifest schema");std::set<std::string> inputPaths;
 for(auto& f:origin["files"]){keys(f,{"path","bytes","sha256"});auto path=str(f["path"]);need(!path.empty()&&inputPaths.insert(path).second&&unhex(str(f["sha256"],64),32).size()==32,"source fingerprint/path");uintv(f["bytes"],UINT64_MAX);}
 auto semanticDocument=parse(semanticBytes,MaxMeta);need(semanticDocument.is_object()&&uintv(semanticDocument.at("coordinateRevision"))==1,"semantic document schema");
 
 const char* names8[]={"world.w2i","world.w2g","world-full.w2t","props.w2p","environment.w2e","collision.wmc"};
 need(m["files"].is_array()&&m["files"].size()==6,"six required pack kinds");std::set<unsigned> packKinds;std::vector<PackData> packs;U indexUsed=0;
 std::string textureFile;U generationBytes=0;
 for(auto& f:m["files"]){keys(f,{"kind","name","bytes","sha256"});unsigned kind=unsigned(uintv(f["kind"],6));need(kind>=1&&packKinds.insert(kind).second,"pack kind mapping");auto name=str(f["name"]);need(kind==3?oneof(name,{"world-full.w2t","world-coarse.w2t"}):name==names8[kind-1],"pack path allowlist");if(kind==3)textureFile=name;auto path=root/fs::path(name);File file(path);generationBytes=add(generationBytes,file.size);need(generationBytes<=MaxGeneration,"generation byte budget");auto p=loadPack(path,indexUsed);need(file.size==uintv(f["bytes"],UINT64_MAX)&&file.fullHash()==str(f["sha256"],64),"pack file SHA256/size");need(p.kind==kind&&p.source==source&&p.semantic==semantic&&p.build==build&&p.manifest==sha(sourceBytes),"mixed generation/manifest");if(!packs.empty())need(p.minor==packs[0].minor&&p.schema==packs[0].schema&&p.compiler==packs[0].compiler&&p.features==packs[0].features,"mixed format feature generation");packs.push_back(std::move(p));}
 std::map<std::string,Record*> records;unsigned worlds=0;for(auto& p:packs)for(auto& r:p.records){need(records.emplace(r.id,&r).second,"duplicate global asset identity");worlds+=r.kind==1;}
 need(worlds==1&&records.size()<=65536,"world/global asset count");
 std::map<std::string,Record*> sections;for(auto& [id,r]:records)if(r->kind==2)need(sections.emplace(r->source,r).second,"duplicate section source identity");
 for(auto& [id,r]:records)if(r->kind==20||r->kind==41){std::set<std::string> members;for(auto& value:r->meta["memberships"]){auto name=str(value);need(sections.count(name)&&members.insert(name).second,"unresolved/duplicate section membership");if(r->kind==20){auto section=sections.at(name);need(std::any_of(section->deps.begin(),section->deps.end(),[&](const auto& d){return d.id==id;}),"geometry membership missing section draw");}}
  if(members.size()>1)need(!str(r->meta["overlapGroup"]).empty(),"multiple memberships need overlap identity");
 }
 for(auto& [id,r]:records){for(auto& d:r->deps){auto target=records.find(d.id);need(target!=records.end()&&packFor(target->second->kind)==d.pack&&targetKind(d.role,target->second->kind),"missing/incorrect typed dependency");}
  if(r->kind==2)for(size_t i=0;i<r->deps.size();++i){auto geometry=records.at(r->deps[i].id);need(str(r->meta["draws"][i])==geometry->source,"section draw order identity");auto& membership=geometry->meta["memberships"];need(std::find(membership.begin(),membership.end(),J(r->source))!=membership.end(),"section draw missing geometry membership");if(membership.size()>1)need(r->meta["overlapGroup"]==geometry->meta["overlapGroup"],"overlap identity mismatch");}
  if(r->kind==40)for(size_t i=0;i<r->deps.size();++i)need(str(r->meta["pieces"][i]["geometry"])==records.at(r->deps[i].id)->source,"model piece identity/order");
 }
 std::map<std::string,unsigned> color;
 for(auto& [id,unused]:records){if(color[id]==2)continue;std::vector<std::pair<std::string,size_t>> stack{{id,0}};color[id]=1;
  while(!stack.empty()){need(stack.size()<=64,"dependency depth budget");auto& frame=stack.back();auto r=records.at(frame.first);if(frame.second==r->deps.size()){color[frame.first]=2;stack.pop_back();continue;}auto next=r->deps[frame.second++].id;need(color[next]!=1,"dependency cycle");if(color[next]!=2){color[next]=1;stack.emplace_back(next,0);}}
 }
 auto recipe=parse(recipeBytes,16*MiB);if(packs[0].schema==1)keys(recipe,{"formatMajor","schemaRevision","compilerRevision","coordinateRevision","shaderRevision","quality","sourceWorldId","semanticWorldId","assets"});else{keys(recipe,{"formatMajor","formatMinor","requiredFeatures","compilerFingerprint","schemaRevision","compilerRevision","coordinateRevision","shaderRevision","quality","sourceWorldId","semanticWorldId","assets"});need(uintv(recipe["formatMinor"])==1&&uintv(recipe["requiredFeatures"])==3&&unhex(str(recipe["compilerFingerprint"],64),32).size()==32,"paged compiler fingerprint/features");}need(uintv(recipe["schemaRevision"])==packs[0].schema&&uintv(recipe["compilerRevision"])==packs[0].compiler&&uintv(recipe["coordinateRevision"])==1&&uintv(recipe["shaderRevision"])==1,"build recipe revision");need(uintv(recipe["formatMajor"])==2&&oneof(str(recipe["quality"]),{"full","coarse"})&&str(recipe["sourceWorldId"],64)==source&&str(recipe["semanticWorldId"],64)==semantic,"build recipe world");need(recipe["assets"].is_array()&&recipe["assets"].size()==records.size(),"build recipe coverage");
 need(textureFile=="world-"+str(recipe["quality"])+".w2t","texture quality filename");for(auto& [id,r]:records)if(r->kind==30||r->kind==31||r->kind==32||r->kind==34)need(r->meta["quality"]==recipe["quality"],"mixed texture quality");
 std::string prior;std::set<std::string> listed;
 for(auto& a:recipe["assets"]){keys(a,{"id","kind","metadataSha256","payloadSha256","dependenciesSha256"});auto id=str(a["id"],64);need(records.count(id)&&listed.insert(id).second&&(prior.empty()||prior<id),"build recipe identity/order");prior=id;auto r=records.at(id);need(uintv(a["kind"])==r->kind&&str(a["metadataSha256"],64)==r->metaHash&&str(a["payloadSha256"],64)==r->payloadHash&&str(a["dependenciesSha256"],64)==r->depsHash,"build recipe content mismatch");}
 auto result=J({{"passed",true},{"scope","complete staged generation; no game/runtime/legacy equivalence acceptance"},{"packs",6},{"records",records.size()},{"indexBytes",indexUsed},{"sourceWorldId",source},{"semanticWorldId",semantic},{"buildId",build}});if(packs[0].schema==2)result["paged"]=pagedGeneration(records);return result;
}

}
