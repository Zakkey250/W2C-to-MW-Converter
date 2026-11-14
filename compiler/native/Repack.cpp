// V2 offline compiler. Exact 40-byte keys, ordered index streams, no game code.
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <lz4/lz4.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#pragma comment(lib,"advapi32.lib")
using B=std::vector<unsigned char>;using U=std::uint64_t;using J=nlohmann::json;namespace fs=std::filesystem;
static void need(bool x,const char* s){if(!x)throw std::runtime_error(s);}
static unsigned r32(const B& b,size_t p){need(p<=b.size()&&b.size()-p>=4,"input u32");unsigned v;memcpy(&v,b.data()+p,4);return v;}
static void w32(B& b,size_t p,unsigned v){need(p+4<=b.size(),"output u32");memcpy(b.data()+p,&v,4);}
static void w64(B& b,size_t p,U v){need(p+8<=b.size(),"output u64");memcpy(b.data()+p,&v,8);}
static unsigned crc(const B& b){static auto table=[](){std::array<unsigned,256> t{};for(unsigned i=0;i<256;++i){auto n=i;for(unsigned j=0;j<8;++j)n=(n>>1)^((n&1)?0xedb88320:0);t[i]=n;}return t;}();unsigned n=~0u;for(auto x:b)n=table[(n^x)&255]^(n>>8);return ~n;}
static std::array<unsigned char,32> sha(const B& b){HCRYPTPROV c=0;HCRYPTHASH h=0;need(CryptAcquireContextW(&c,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(c,CALG_SHA_256,0,0,&h),"SHA256 init");need(CryptHashData(h,b.data(),DWORD(b.size()),0),"SHA256 data");std::array<unsigned char,32> out{};DWORD n=32;need(CryptGetHashParam(h,HP_HASHVAL,out.data(),&n,0),"SHA256 finish");CryptDestroyHash(h);CryptReleaseContext(c,0);return out;}
static B load(const fs::path& p,U max=64*1048576){auto n=fs::file_size(p);need(n<=max,"bounded offline source");B b(static_cast<size_t>(n),0);std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(n));need(U(f.gcount())==n,"source truncation");return b;}
struct Mesh{unsigned texture,firstVertex,vertexCount,firstIndex,indexCount;float bounds[6];};static_assert(sizeof(Mesh)==44);
struct Vertex{std::array<unsigned char,40> bytes;bool operator==(const Vertex&)const=default;};
struct VertexHash{size_t operator()(const Vertex& v)const{U h=14695981039346656037ull;for(auto c:v.bytes){h^=c;h*=1099511628211ull;}return size_t(h);}};
struct Material{unsigned usage,blend,flags,tile;};
struct Span{unsigned source,chunk,first,count,local,sequence;};
struct Chunk{std::vector<Vertex> vertices;std::vector<unsigned short>indices,shadow;unsigned draws=0,shadowSpans=0;};
static bool shadowEligible(const Material& m,unsigned hash){return !m.blend&&m.usage!=1&&hash!=0x57FC91B7&&hash!=0xD2E09151&&hash!=0x29D9478C;}
static void append32(B& b,unsigned v){auto old=b.size();b.resize(old+4);w32(b,old,v);}
static void appendSpan(B& b,const Span& s){auto at=b.size();b.resize(at+32);unsigned x[]={s.source,s.chunk,s.first,s.count,s.local,s.sequence};memcpy(b.data()+at,x,24);}
static J repack(const fs::path& source,const fs::path& bankPath,const fs::path& destination,unsigned budget,bool rawOnly){
 need(budget>=4096&&budget<=1048576,"cluster budget");auto old=load(source),bank=load(bankPath);need(old.size()>=128&&!memcmp(old.data(),"W2ORCL2",8)&&r32(old,8)==1&&r32(old,12)==128,"oracle envelope");need(bank.size()>=128&&!memcmp(bank.data(),"W2MATB1",8)&&r32(bank,8)==1&&r32(bank,20)==88,"material bank envelope");
 unsigned section=r32(old,16),vc=r32(old,20),ic=r32(old,24),mc=r32(old,28),lc=r32(old,32),oc=r32(old,36),sc=r32(old,40),ss=r32(old,44),dc0=r32(old,48),dc1=r32(old,52);
 need(vc<5000000&&ic<15000000&&ic%3==0&&mc<1000000&&lc==mc&&oc==mc&&dc0+dc1==mc,"oracle counts");
 U vertexAt=128,indexAt=vertexAt+U(vc)*40,meshAt=indexAt+U(ic)*4,lightAt=meshAt+U(mc)*44,ownerAt=lightAt+U(lc)*64,shadowAt=ownerAt+U(oc)*4,shadowSpanAt=shadowAt+U(sc)*4,draw0At=shadowSpanAt+U(ss)*12,draw1At=draw0At+U(dc0)*4;
 need(draw1At+U(dc1)*4==old.size(),"oracle exact length");
 std::vector<Mesh> meshes(mc);if(mc)memcpy(meshes.data(),old.data()+meshAt,size_t(mc)*44);
 std::map<unsigned,Material> materials;Material fallback;memcpy(&fallback,bank.data()+24,16);unsigned materialCount=r32(bank,12);need(128+U(materialCount)*88<=bank.size(),"material bank count");for(unsigned i=0;i<materialCount;++i){size_t at=128+size_t(i)*88;Material m;memcpy(&m,bank.data()+at+4,16);materials[r32(bank,at)]=m;}
 auto mat=[&](unsigned hash){auto it=materials.find(hash);return it==materials.end()?fallback:it->second;};
 std::vector<unsigned> pass(mc,~0u);for(unsigned p=0;p<2;++p){auto at=p?draw1At:draw0At;auto n=p?dc1:dc0;for(unsigned i=0;i<n;++i){auto draw=r32(old,size_t(at+i*4));need(draw<mc&&pass[draw]==~0u,"oracle draw identity");pass[draw]=p;}}
 std::vector<Chunk> chunks(1);std::vector<Span> spans,shadowSpans;std::vector<unsigned> triangleChunk(ic/3,~0u),triangleLocal(ic/3,~0u);
 std::unordered_map<Vertex,unsigned short,VertexHash> weld;weld.reserve(32768);unsigned activeDraw=~0u;
 auto beginChunk=[&](){need(!chunks.back().indices.empty(),"empty cluster split");chunks.emplace_back();weld.clear();activeDraw=~0u;};
 for(unsigned draw=0;draw<mc;++draw){const auto& m=meshes[draw];need(U(m.firstVertex)+m.vertexCount<=vc&&U(m.firstIndex)+m.indexCount<=ic&&m.indexCount%3==0&&pass[draw]!=~0u,"source mesh bounds");
  for(unsigned i=0;i<m.indexCount;i+=3){std::array<Vertex,3> triangle;unsigned added=0;for(unsigned k=0;k<3;++k){auto index=r32(old,size_t(indexAt+U(m.firstIndex+i+k)*4));need(index<m.vertexCount,"source mesh index");memcpy(triangle[k].bytes.data(),old.data()+vertexAt+U(m.firstVertex+index)*40,40);for(unsigned at:{0u,4u,8u,12u,16u,20u,28u,32u,36u}){float x;memcpy(&x,triangle[k].bytes.data()+at,4);need(std::isfinite(x),"source non-finite vertex");}if(!weld.count(triangle[k])&&std::find(triangle.begin(),triangle.begin()+k,triangle[k])==triangle.begin()+k)++added;}
   auto& prior=chunks.back();unsigned drawCount=prior.draws+(activeDraw!=draw);U worst=128+U(drawCount)*192+U(prior.vertices.size()+added)*40+U(prior.indices.size()+3)*4;
   if(prior.vertices.size()+added>65535||worst>budget){beginChunk();added=0;for(unsigned k=0;k<3;++k)if(std::find(triangle.begin(),triangle.begin()+k,triangle[k])==triangle.begin()+k)++added;need(128+192+U(added)*40+12<=budget,"triangle exceeds cluster");}
   auto& c=chunks.back();if(activeDraw!=draw){spans.push_back({draw,unsigned(chunks.size()-1),i,0,unsigned(c.indices.size()),unsigned(spans.size())});++c.draws;activeDraw=draw;}
   unsigned local=unsigned(c.indices.size());for(auto& v:triangle){auto [it,inserted]=weld.emplace(v,static_cast<unsigned short>(c.vertices.size()));if(inserted)c.vertices.push_back(v);c.indices.push_back(it->second);}spans.back().count+=3;triangleChunk[(m.firstIndex+i)/3]=unsigned(chunks.size()-1);triangleLocal[(m.firstIndex+i)/3]=local;
  }
 }
 if(chunks.size()==1&&chunks[0].indices.empty())chunks.clear();
 // Reconstruct the old owner-sorted shadow stream and check it before using
 // its source-index locations. Never infer a shadow from color or position.
 std::map<unsigned,std::vector<unsigned>> shadowSources;
 for(unsigned draw=0;draw<mc;++draw){auto& m=meshes[draw];unsigned owner=r32(old,size_t(ownerAt+U(draw)*4));if(owner==~0u||!shadowEligible(mat(m.texture),m.texture))continue;for(unsigned i=0;i<m.indexCount;i+=3)shadowSources[owner].push_back(m.firstIndex+i);}
 unsigned shadowCursor=0,oldSpan=0;
 for(auto& [owner,locations]:shadowSources){if(locations.empty())continue;need(oldSpan<ss&&r32(old,size_t(shadowSpanAt+U(oldSpan)*12))==owner&&r32(old,size_t(shadowSpanAt+U(oldSpan)*12+4))==shadowCursor&&r32(old,size_t(shadowSpanAt+U(oldSpan)*12+8))==locations.size()*3,"frozen shadow owner span mismatch");++oldSpan;
  for(auto first:locations){unsigned chunk=triangleChunk[first/3],local=triangleLocal[first/3];need(chunk<chunks.size(),"shadow triangle missing from main stream");auto& c=chunks[chunk];
   if(shadowSpans.empty()||shadowSpans.back().source!=owner||shadowSpans.back().chunk!=chunk){shadowSpans.push_back({owner,chunk,shadowCursor,0,unsigned(c.shadow.size()),unsigned(shadowSpans.size())});++c.shadowSpans;}
   auto draw=std::upper_bound(meshes.begin(),meshes.end(),first,[](unsigned v,const Mesh&m){return v<m.firstIndex;});need(draw!=meshes.begin(),"shadow source mesh");--draw;
   for(unsigned k=0;k<3;++k){unsigned global=draw->firstVertex+r32(old,size_t(indexAt+U(first+k)*4));need(shadowCursor<sc&&r32(old,size_t(shadowAt+U(shadowCursor)*4))==global,"frozen shadow vertex mismatch");c.shadow.push_back(c.indices[local+k]);++shadowCursor;}shadowSpans.back().count+=3;
  }
 }
 need(shadowCursor==sc&&oldSpan==ss,"frozen shadow coverage");
 U chunkTable=256,drawTable=chunkTable+U(chunks.size())*128,normalTable=drawTable+U(mc)*128,shadowTable=normalTable+U(spans.size())*32,order0=shadowTable+U(shadowSpans.size())*32,order1=order0+U(dc0)*4,payloadStart=(order1+U(dc1)*4+63)/64*64;
 need(payloadStart<=16*1048576,"page index budget");B output(size_t(payloadStart),0);memcpy(output.data(),"W2GPAGE2",8);unsigned h[]={1,256,section,unsigned(chunks.size()),vc,ic,mc,sc,unsigned(spans.size()),unsigned(shadowSpans.size()),dc0,dc1};memcpy(output.data()+8,h,sizeof(h));
 for(auto [at,value]:std::initializer_list<std::pair<size_t,U>>{{56,chunkTable},{64,drawTable},{72,normalTable},{80,shadowTable},{88,order0},{96,order1},{208,payloadStart}})w64(output,at,value);
 memcpy(output.data()+112,old.data()+56,24);auto oracleHash=sha(old),bankHash=sha(bank);memcpy(output.data()+136,oracleHash.data(),32);memcpy(output.data()+168,bankHash.data(),32);w32(output,216,rawOnly?1:3);
 for(unsigned draw=0;draw<mc;++draw){size_t at=size_t(drawTable+U(draw)*128);auto& m=meshes[draw];w32(output,at,draw);w32(output,at+4,m.texture);w32(output,at+8,m.firstVertex);w32(output,at+12,m.vertexCount);w32(output,at+16,m.firstIndex);w32(output,at+20,m.indexCount);memcpy(output.data()+at+24,m.bounds,24);memcpy(output.data()+at+48,old.data()+lightAt+U(draw)*64,64);w32(output,at+112,r32(old,size_t(ownerAt+U(draw)*4)));w32(output,at+116,pass[draw]);}
 auto encodeSpans=[&](U at,const auto& values){for(auto& span:values){unsigned x[]={span.source,span.chunk,span.first,span.count,span.local,span.sequence};memcpy(output.data()+at,x,24);at+=32;}};encodeSpans(normalTable,spans);encodeSpans(shadowTable,shadowSpans);
 if(dc0)memcpy(output.data()+order0,old.data()+draw0At,size_t(dc0)*4);if(dc1)memcpy(output.data()+order1,old.data()+draw1At,size_t(dc1)*4);
 U unique=0,decoded=0,maximum=0,compressedCount=0;
 for(unsigned i=0;i<chunks.size();++i){auto& c=chunks[i];unsigned accounting=128+c.draws*128+c.draws*32+c.shadowSpans*32+unsigned(c.vertices.size()*40+(c.indices.size()+c.shadow.size())*2);need(accounting<=budget&&c.vertices.size()<=65535,"final cluster total-byte bound");maximum=std::max<U>(maximum,accounting);unique+=c.vertices.size();
  B raw(c.vertices.size()*40+(c.indices.size()+c.shadow.size())*2);auto at=raw.data();memcpy(at,c.vertices.data(),c.vertices.size()*40);at+=c.vertices.size()*40;memcpy(at,c.indices.data(),c.indices.size()*2);at+=c.indices.size()*2;if(!c.shadow.empty())memcpy(at,c.shadow.data(),c.shadow.size()*2);decoded+=raw.size();
  B encoded;unsigned codec=0;if(!rawOnly){encoded.resize(LZ4_compressBound(int(raw.size())));int n=LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),reinterpret_cast<char*>(encoded.data()),int(raw.size()),int(encoded.size()));need(n>0,"LZ4 encode");encoded.resize(n);if(encoded.size()<raw.size())codec=1;}
  if(!codec)encoded=raw;else++compressedCount;size_t offset=(output.size()+63)/64*64;output.resize(offset,0);size_t entry=size_t(chunkTable+U(i)*128);w64(output,entry,offset);w64(output,entry+8,encoded.size());w64(output,entry+16,raw.size());w32(output,entry+24,unsigned(c.vertices.size()));w32(output,entry+28,unsigned(c.indices.size()));w32(output,entry+32,unsigned(c.shadow.size()));w32(output,entry+36,codec);w32(output,entry+40,crc(encoded));w32(output,entry+44,crc(raw));w32(output,entry+48,accounting);w32(output,entry+52,c.draws);w32(output,entry+56,c.shadowSpans);w32(output,entry+60,c.draws);
  float lo[3],hi[3];for(unsigned a=0;a<3;++a){memcpy(&lo[a],c.vertices[0].bytes.data()+a*4,4);hi[a]=lo[a];}for(auto& v:c.vertices)for(unsigned a=0;a<3;++a){float x;memcpy(&x,v.bytes.data()+a*4,4);lo[a]=std::min(lo[a],x);hi[a]=std::max(hi[a],x);}memcpy(output.data()+entry+64,lo,12);memcpy(output.data()+entry+76,hi,12);output.insert(output.end(),encoded.begin(),encoded.end());
 }
 need(output.size()<=64*1048576,"page payload bound");w64(output,104,output.size());B tables(output.begin()+256,output.begin()+size_t(payloadStart));w32(output,204,crc(tables));B header(output.begin(),output.begin()+256);w32(output,200,crc(header));
 need(!fs::exists(destination),"immutable page output already exists");std::ofstream f(destination,std::ios::binary);f.write(reinterpret_cast<const char*>(output.data()),output.size());f.flush();need(bool(f),"page write");
 return {{"section",section},{"sourceVertices",vc},{"sourceIndices",ic},{"sourceMeshes",mc},{"sourceShadowIndices",sc},{"clusters",chunks.size()},{"uniqueClusterVertices",unique},{"decodedBytes",decoded},{"storedBytes",output.size()},{"maximumClusterTotalBytes",maximum},{"lz4Clusters",compressedCount},{"budget",budget},{"quantization",false}};
}
int wmain(int argc,wchar_t** argv){try{need(argc>=4&&argc<=6,"Repack ORACLE MATERIAL_BANK OUTPUT [--raw] [BUDGET]");need(argc<5||std::wstring(argv[4])==L"--raw"||std::wstring(argv[4])==L"--lz4","unknown codec option");bool raw=argc>=5&&std::wstring(argv[4])==L"--raw";unsigned budget=argc==6?unsigned(std::stoul(argv[5])):1048576;std::cout<<repack(argv[1],argv[2],argv[3],budget,raw).dump()<<'\n';return 0;}catch(const std::exception&e){std::cerr<<J({{"error",e.what()}}).dump()<<'\n';return 1;}}
