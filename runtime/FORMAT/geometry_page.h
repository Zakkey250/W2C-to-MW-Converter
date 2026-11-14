#pragma once

// Independent V2 reader. No encoder/welding code is shared with Repack.cpp.
struct PageSummary {
 unsigned section=0,clusters=0,sourceVertices=0,indices=0,draws=0,shadow=0;
 U uniqueVertices=0,maximumBytes=0,decodedBytes=0,lz4Blocks=0;
 std::set<unsigned> textureHashes,owners;
 std::map<unsigned,unsigned> texturePasses;
 unsigned maximumLight=0;bool hasLight=false;
 std::string oracleSha,bankSha;
};
static B decodePageCluster(const B& page,unsigned id){
 size_t entry=size_t(r64(page,56)+U(id)*128);U at=r64(page,entry),stored=r64(page,entry+8),decoded=r64(page,entry+16);
 need(stored>0&&stored<=MiB&&decoded>0&&decoded<=MiB&&at<=page.size()&&stored<=page.size()-at,"page block range/budget");
 B packed(page.begin()+size_t(at),page.begin()+size_t(at+stored));need(crc(packed)==r32(page,entry+40),"cluster stored CRC");
 unsigned codec=r32(page,entry+36);B bytes;
 if(codec==0){need(stored==decoded,"RAW cluster lengths");bytes=std::move(packed);}else if(codec==1){need((r32(page,216)&2)!=0,"undeclared LZ4 feature");bytes.resize(size_t(decoded));int n=LZ4_decompress_safe(reinterpret_cast<const char*>(packed.data()),reinterpret_cast<char*>(bytes.data()),int(stored),int(decoded));need(n>=0&&U(n)==decoded,"LZ4 decode/exact length");}else throw std::runtime_error("unknown cluster codec");
 need(crc(bytes)==r32(page,entry+44),"cluster decoded CRC");return bytes;
}
static PageSummary validateGeometryPage(const B& b){
 need(b.size()>=256&&!memcmp(b.data(),"W2GPAGE2",8)&&r32(b,8)==1&&r32(b,12)==256,"geometry page identity");
 need(r64(b,104)==b.size()&&b.size()<=MaxBlock&&zeros(b,220,36)&&oneof(std::to_string(r32(b,216)),{"1","3"}),"geometry page header");
 B header(b.begin(),b.begin()+256);unsigned headerCrc=r32(header,200);std::fill(header.begin()+200,header.begin()+204,static_cast<unsigned char>(0));need(crc(header)==headerCrc,"geometry page header CRC");
 PageSummary result;result.section=r32(b,16);result.clusters=r32(b,20);result.sourceVertices=r32(b,24);result.indices=r32(b,28);result.draws=r32(b,32);result.shadow=r32(b,36);auto normalSpans=r32(b,40),shadowSpans=r32(b,44),opaque=r32(b,48),alpha=r32(b,52);
 need(result.clusters<=65536&&result.sourceVertices<5000000&&result.indices<15000000&&result.shadow<15000000&&result.indices%3==0&&result.shadow%3==0&&result.draws<1000000&&U(opaque)+alpha==result.draws,"geometry page counts");
 U ct=256,dt=add(ct,mul(result.clusters,128)),nt=add(dt,mul(result.draws,128)),st=add(nt,mul(normalSpans,32)),o0=add(st,mul(shadowSpans,32)),o1=add(o0,mul(opaque,4)),payloadStart=add(add(o1,mul(alpha,4)),63)/64*64;
 need(payloadStart<=MaxIndex&&payloadStart<=b.size()&&r64(b,56)==ct&&r64(b,64)==dt&&r64(b,72)==nt&&r64(b,80)==st&&r64(b,88)==o0&&r64(b,96)==o1&&r64(b,208)==payloadStart,"geometry page index offsets/budget");
 B table(b.begin()+256,b.begin()+size_t(payloadStart));need(crc(table)==r32(b,204),"geometry page table CRC");for(unsigned a=0;a<3;++a)need(rf(b,112+a*4)<=rf(b,124+a*4),"page bounds");
 result.oracleSha=hex(b.data()+136,32);result.bankSha=hex(b.data()+168,32);need(!zeros(b,136,32)&&!zeros(b,168,32),"page source identity");
 std::vector<unsigned> drawCursor(result.draws,0),normalCursor(result.clusters,0),shadowCursor(result.clusters,0),normalCount(result.clusters,0),shadowCount(result.clusters,0);std::vector<std::set<unsigned>> chunkDraws(result.clusters);U sourceIndex=0;
 for(unsigned i=0;i<result.draws;++i){size_t at=size_t(dt+U(i)*128);need(r32(b,at)==i&&r32(b,at+116)<=1&&zeros(b,at+120,8),"source draw identity/pass");need(U(r32(b,at+8))+r32(b,at+12)<=result.sourceVertices&&r32(b,at+16)==sourceIndex&&r32(b,at+20)%3==0,"source draw range/order");sourceIndex+=r32(b,at+20);for(unsigned a=0;a<3;++a)need(rf(b,at+24+a*4)<=rf(b,at+36+a*4),"source draw bounds");result.textureHashes.insert(r32(b,at+4));result.texturePasses[r32(b,at+4)]|=1u<<r32(b,at+116);result.owners.insert(r32(b,at+112));for(unsigned n=0;n<16;++n){unsigned light=r32(b,at+48+n*4);if(light!=~0u){result.maximumLight=std::max(result.maximumLight,light);result.hasLight=true;}}}
 need(sourceIndex==result.indices,"source draw index coverage");
 std::vector<bool> seen(result.draws,false);for(unsigned pass=0;pass<2;++pass){unsigned prior=0,count=pass?alpha:opaque;U at=pass?o1:o0;for(unsigned i=0;i<count;++i){auto draw=r32(b,size_t(at+U(i)*4));need(draw<result.draws&&!seen[draw]&&(!i||draw>prior)&&r32(b,size_t(dt+U(draw)*128+116))==pass,"opacity/alpha draw order");seen[draw]=true;prior=draw;}}
 unsigned priorDraw=0;bool first=true;
 for(unsigned i=0;i<normalSpans;++i){size_t at=size_t(nt+U(i)*32);unsigned draw=r32(b,at),chunk=r32(b,at+4),source=r32(b,at+8),count=r32(b,at+12),local=r32(b,at+16);need(draw<result.draws&&chunk<result.clusters&&count>0&&count%3==0&&r32(b,at+20)==i&&zeros(b,at+24,8),"normal span identity");need((first||draw>=priorDraw)&&source==drawCursor[draw]&&local==normalCursor[chunk]&&U(source)+count<=r32(b,size_t(dt+U(draw)*128+20)),"normal span source/order/coverage");first=false;priorDraw=draw;drawCursor[draw]+=count;normalCursor[chunk]+=count;++normalCount[chunk];need(chunkDraws[chunk].insert(draw).second,"duplicate draw within cluster");}
 for(unsigned draw=0;draw<result.draws;++draw)need(drawCursor[draw]==r32(b,size_t(dt+U(draw)*128+20)),"incomplete source draw");
 U sourceShadow=0;unsigned priorOwner=0;
 for(unsigned i=0;i<shadowSpans;++i){size_t at=size_t(st+U(i)*32);unsigned owner=r32(b,at),chunk=r32(b,at+4),source=r32(b,at+8),count=r32(b,at+12),local=r32(b,at+16);need(owner!=~0u&&chunk<result.clusters&&count>0&&count%3==0&&r32(b,at+20)==i&&zeros(b,at+24,8),"shadow span identity");need((!i||owner>=priorOwner)&&source==sourceShadow&&local==shadowCursor[chunk],"shadow span owner/order/coverage");priorOwner=owner;sourceShadow+=count;shadowCursor[chunk]+=count;++shadowCount[chunk];}
 need(sourceShadow==result.shadow,"shadow source coverage");
 U end=payloadStart;for(unsigned i=0;i<result.clusters;++i){size_t at=size_t(ct+U(i)*128);U offset=r64(b,at),stored=r64(b,at+8),decoded=r64(b,at+16);unsigned vc=r32(b,at+24),ic=r32(b,at+28),sc=r32(b,at+32),ns=r32(b,at+52),ss=r32(b,at+56),draws=r32(b,at+60);
  need(offset%64==0&&offset>=end&&stored>0&&stored<=MiB&&offset<=b.size()&&stored<=b.size()-offset,"cluster payload overlap/range");end=offset+stored;
  need(vc>0&&vc<=65535&&ic>0&&ic%3==0&&sc%3==0&&decoded==add(mul(vc,40),mul(add(ic,sc),2))&&decoded<=MiB,"cluster decoded shape/budget");
  need(ic==normalCursor[i]&&sc==shadowCursor[i]&&ns==normalCount[i]&&ss==shadowCount[i]&&draws==chunkDraws[i].size()&&zeros(b,at+88,40),"cluster span accounting");
  U accounting=add(add(decoded,128),add(mul(draws,128),mul(U(ns)+ss,32)));need(accounting<=MiB&&accounting==r32(b,at+48),"cluster total byte budget");result.maximumBytes=std::max(result.maximumBytes,accounting);result.uniqueVertices+=vc;result.decodedBytes+=decoded;result.lz4Blocks+=r32(b,at+36)==1;
  for(unsigned a=0;a<3;++a)need(rf(b,at+64+a*4)<=rf(b,at+76+a*4),"cluster bounds");auto bytes=decodePageCluster(b,i);
  for(U v=0;v<vc;++v){for(unsigned p:{0u,4u,8u,12u,16u,20u,28u,32u,36u})rf(bytes,size_t(v*40+p));for(unsigned a=0;a<3;++a){double x=rf(bytes,size_t(v*40+a*4)),lo=rf(b,at+64+a*4),hi=rf(b,at+76+a*4);need(x>=lo-1e-5&&x<=hi+1e-5,"cluster vertex bounds");}}
  for(U index=0;index<U(ic)+sc;++index)need(r16(bytes,size_t(U(vc)*40+index*2))<vc,"cluster uint16 index range");
 }
 need(end==b.size()&&(result.clusters!=0||result.indices==0),"page payload coverage");return result;
}
struct OracleView {unsigned vc,ic,mc,sc,ss,d0,d1;U va,ia,ma,la,oa,sa,spa,d0a,d1a;};
static OracleView readOracle(const B& b){need(b.size()>=128&&!memcmp(b.data(),"W2ORCL2",8)&&r32(b,8)==1&&r32(b,12)==128,"equivalence oracle header");OracleView o{};o.vc=r32(b,20);o.ic=r32(b,24);o.mc=r32(b,28);o.sc=r32(b,40);o.ss=r32(b,44);o.d0=r32(b,48);o.d1=r32(b,52);need(o.vc<5000000&&o.ic<15000000&&o.mc<1000000&&o.ic%3==0&&r32(b,32)==o.mc&&r32(b,36)==o.mc,"equivalence oracle counts");o.va=128;o.ia=add(o.va,mul(o.vc,40));o.ma=add(o.ia,mul(o.ic,4));o.la=add(o.ma,mul(o.mc,44));o.oa=add(o.la,mul(o.mc,64));o.sa=add(o.oa,mul(o.mc,4));o.spa=add(o.sa,mul(o.sc,4));o.d0a=add(o.spa,mul(o.ss,12));o.d1a=add(o.d0a,mul(o.d0,4));need(add(o.d1a,mul(o.d1,4))==b.size(),"equivalence oracle exact size");return o;}
struct StreamHash {Hash h;B pending;void append(const void* p,size_t n){auto first=static_cast<const unsigned char*>(p);pending.insert(pending.end(),first,first+n);if(pending.size()>=65536){h.append(pending.data(),pending.size());pending.clear();}}std::string finish(){if(!pending.empty())h.append(pending.data(),pending.size());return h.finish();}};
static J equivalence(const fs::path& oraclePath,const fs::path& pagePath,const fs::path& bankPath){
 File of(oraclePath),pf(pagePath),mf(bankPath);auto old=of.read(0,of.size,MaxBlock),page=pf.read(0,pf.size,MaxBlock),bank=mf.read(0,mf.size,MaxBlock);auto p=validateGeometryPage(page);auto o=readOracle(old);need(p.oracleSha==sha(old)&&p.bankSha==sha(bank),"equivalence frozen input digest");need(p.section==r32(old,16)&&p.sourceVertices==o.vc&&p.indices==o.ic&&p.draws==o.mc&&p.shadow==o.sc&&r32(page,48)==o.d0&&r32(page,52)==o.d1&&!memcmp(page.data()+112,old.data()+56,24),"equivalence section/header/bounds");
 U dt=r64(page,64),nt=r64(page,72),st=r64(page,80);StreamHash expected,actual;expected.append(old.data()+16,16);actual.append(old.data()+16,16);expected.append(old.data()+56,24);actual.append(page.data()+112,24);
 for(unsigned draw=0;draw<o.mc;++draw){size_t at=size_t(dt+U(draw)*128),m=size_t(o.ma+U(draw)*44);need(!memcmp(page.data()+at+4,old.data()+m,20)&&!memcmp(page.data()+at+24,old.data()+m+20,24)&&!memcmp(page.data()+at+48,old.data()+o.la+U(draw)*64,64)&&r32(page,at+112)==r32(old,size_t(o.oa+U(draw)*4)),"equivalence material/bounds/light/ownership state");expected.append(old.data()+m,44);actual.append(page.data()+at+4,20);actual.append(page.data()+at+24,24);expected.append(old.data()+o.la+U(draw)*64,64);actual.append(page.data()+at+48,64);expected.append(old.data()+o.oa+U(draw)*4,4);actual.append(page.data()+at+112,4);}
 need(!memcmp(page.data()+r64(page,88),old.data()+o.d0a,size_t(o.d0)*4)&&!memcmp(page.data()+r64(page,96),old.data()+o.d1a,size_t(o.d1)*4),"equivalence opacity/alpha source order");expected.append(old.data()+o.d0a,size_t(o.d0+o.d1)*4);actual.append(page.data()+r64(page,88),size_t(o.d0+o.d1)*4);
 unsigned cached=~0u;B bytes;auto cluster=[&](unsigned id)->const B&{if(cached!=id){bytes=decodePageCluster(page,id);cached=id;}return bytes;};U compared=0;
 for(unsigned n=0;n<r32(page,40);++n){size_t span=size_t(nt+U(n)*32);unsigned draw=r32(page,span),id=r32(page,span+4),source=r32(page,span+8),count=r32(page,span+12),local=r32(page,span+16);size_t mesh=size_t(o.ma+U(draw)*44);const auto& decoded=cluster(id);U vertexBytes=U(r32(page,size_t(r64(page,56)+U(id)*128+24)))*40;
  for(unsigned i=0;i<count;++i){auto originalIndex=r32(old,size_t(o.ia+U(r32(old,mesh+12)+source+i)*4));need(originalIndex<r32(old,mesh+8),"oracle index range");U global=U(r32(old,mesh+4))+originalIndex;need(global<o.vc,"oracle global vertex");auto index=r16(decoded,size_t(vertexBytes+U(local+i)*2));const auto* a=old.data()+o.va+global*40;const auto* b=decoded.data()+U(index)*40;need(!memcmp(a,b,40),"equivalence 40B normal stream/winding/color/UV/sun");expected.append(a,40);actual.append(b,40);++compared;}
 }
 U shadowCompared=0;unsigned oldSpan=0;
 for(unsigned n=0;n<r32(page,44);++n){size_t span=size_t(st+U(n)*32);unsigned owner=r32(page,span),id=r32(page,span+4),source=r32(page,span+8),count=r32(page,span+12),local=r32(page,span+16);while(oldSpan<o.ss&&source>=r32(old,size_t(o.spa+U(oldSpan)*12+4))+r32(old,size_t(o.spa+U(oldSpan)*12+8)))++oldSpan;need(oldSpan<o.ss&&owner==r32(old,size_t(o.spa+U(oldSpan)*12))&&U(source)+count<=U(r32(old,size_t(o.spa+U(oldSpan)*12+4)))+r32(old,size_t(o.spa+U(oldSpan)*12+8)),"equivalence shadow owner/order");const auto& decoded=cluster(id);size_t entry=size_t(r64(page,56)+U(id)*128);U indexAt=U(r32(page,entry+24))*40+U(r32(page,entry+28))*2;
  for(unsigned i=0;i<count;++i){auto global=r32(old,size_t(o.sa+U(source+i)*4));need(global<o.vc,"oracle shadow vertex");auto index=r16(decoded,size_t(indexAt+U(local+i)*2));const auto* a=old.data()+o.va+U(global)*40;const auto* b=decoded.data()+U(index)*40;need(!memcmp(a,b,40),"equivalence shadow attributes/order");expected.append(a,40);actual.append(b,40);++shadowCompared;}
 }
 need(compared==o.ic&&shadowCompared==o.sc,"equivalence complete indexed coverage");auto oldHash=expected.finish(),newHash=actual.finish();need(oldHash==newHash,"equivalence independent stream digest");return {{"passed",true},{"section",p.section},{"differences",0},{"normalIndexAttributesCompared",compared},{"shadowIndexAttributesCompared",shadowCompared},{"legacySemanticSha256",oldHash},{"reconstructedSemanticSha256",newHash},{"clusters",p.clusters},{"maximumClusterTotalBytes",p.maximumBytes},{"uniqueClusterVertices",p.uniqueVertices},{"storedBytes",page.size()},{"decodedBytes",p.decodedBytes},{"lz4Clusters",p.lz4Blocks},{"oracleSha256",p.oracleSha},{"materialBankSha256",p.bankSha},{"pageSha256",sha(page)}};
}
