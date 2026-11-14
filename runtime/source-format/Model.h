#pragma once
#include "../format/BoundedDisk.h"
#include <cfloat>
namespace w2source {
namespace d=w2v2disk;
struct ModelSummary {unsigned hash=0,materials=0,clusters=0,vertices=0,indices=0,rejected=0;};
static ModelSummary validateModel(const d::B& b){
 d::need(b.size()>=256&&b.size()<=64*d::MiB&&!memcmp(b.data(),"W2SGPU1",8),"GPU model envelope");
 d::need(d::r32(b,8)==1&&d::r32(b,12)==256&&d::r32(b,28)==40&&d::r32(b,48)==96&&d::r32(b,52)==96,"GPU model layout");
 ModelSummary s{d::r32(b,16),d::r32(b,20),d::r32(b,24),d::r32(b,32),d::r32(b,36),d::r32(b,44)};
 d::need(s.materials<=4096&&s.clusters<=262144&&s.vertices<=10000000&&d::r64(b,56)==b.size(),"GPU model counts/size");
 auto ma=d::r64(b,64),ca=d::r64(b,72),pa=d::r64(b,80),aa=d::r64(b,88),an=d::r64(b,96);
 d::need(ma==256&&ca==ma+d::U(s.materials)*96&&pa==(ca+d::U(s.clusters)*96+63)/64*64&&aa>=pa&&aa%8==0&&aa<=b.size()&&an==b.size()-aa&&d::r64(b,104)==0&&d::zeros(b,240,16),"GPU model tables/auxiliary range");
 auto header=d::B(b.begin(),b.begin()+256);unsigned hc=d::r32(b,236);std::fill(header.begin()+236,header.begin()+240,static_cast<unsigned char>(0));d::need(d::crc(header)==hc&&d::crc(d::B(b.begin()+256,b.end()))==d::r32(b,232),"GPU model CRC");
 for(unsigned at=144;at<232;at+=4)d::rf(b,at);
 d::need(d::zeros(b,size_t(ca+d::U(s.clusters)*96),size_t(pa-ca-d::U(s.clusters)*96)),"GPU table padding");
 std::vector<unsigned char> owners(s.clusters);std::vector<std::pair<d::U,d::U>> ranges;d::U rendered=0;
 for(unsigned i=0;i<s.materials;++i){size_t at=size_t(ma+d::U(i)*96);unsigned first=d::r32(b,at+40),count=d::r32(b,at+44);d::need(d::r32(b,at)==i&&d::r32(b,at+4)<128&&d::U(first)+count<=s.clusters&&d::r32(b,at+32)%3==0&&d::r32(b,at+36)%3==0&&d::zeros(b,at+48,48),"GPU material dimensions");d::U indices=0;
  for(unsigned n=first;n<first+count;++n){d::need(owners[n]++==0&&d::r32(b,size_t(ca+d::U(n)*96+4))==i,"GPU material ownership");indices+=d::r32(b,size_t(ca+d::U(n)*96+12));}d::need(indices==d::r32(b,at+36),"GPU material index sum");}
 for(unsigned i=0;i<s.clusters;++i){size_t at=size_t(ca+d::U(i)*96);unsigned mi=d::r32(b,at+4),vc=d::r32(b,at+8),ic=d::r32(b,at+12),nt=d::r32(b,at+52);auto vo=d::r64(b,at+16),io=d::r64(b,at+24),ro=d::r64(b,at+32),to=d::r64(b,at+40);
  d::need(owners[i]==1&&d::r32(b,at)==i&&mi<s.materials&&vc>0&&vc<=65535&&ic>0&&ic<=18000&&ic%3==0&&nt==ic/3&&d::r32(b,at+56)==40&&d::r32(b,at+60)==2&&vo%64==0,"GPU cluster dimensions");
  for(auto [offset,n]:{std::pair{vo,d::U(vc)*40},std::pair{io,d::U(ic)*2},std::pair{ro,d::U(vc)*4},std::pair{to,d::U(nt)*4}}){d::need(offset%4==0&&offset>=pa&&offset<=aa&&n<=aa-offset,"GPU cluster payload range");ranges.emplace_back(offset,offset+n);}
  d::need(320ull+d::U(vc)*44+d::U(ic)*4+d::U(nt)*4==d::r32(b,at+88)&&d::r32(b,at+88)<=1048576,"GPU cluster total-byte budget");
  float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
  for(unsigned v=0;v<vc;++v){auto p=size_t(vo+d::U(v)*40);for(unsigned f:{0u,4u,8u,12u,16u,20u,28u,32u,36u})d::rf(b,p+f);for(unsigned a=0;a<3;++a){float value=d::rf(b,p+a*4);lo[a]=std::min(lo[a],value);hi[a]=std::max(hi[a],value);}}
  for(unsigned a=0;a<3;++a)d::need(lo[a]==d::rf(b,at+64+a*4)&&hi[a]==d::rf(b,at+76+a*4),"GPU exact cluster bounds");
  for(unsigned n=0;n<ic;++n)d::need(d::r16(b,size_t(io+d::U(n)*2))<vc,"GPU cluster index bounds");
  std::set<unsigned> remaps;for(unsigned n=0;n<vc;++n)d::need(remaps.insert(d::r32(b,size_t(ro+d::U(n)*4))).second,"GPU source vertex correspondence uniqueness");
  unsigned prior=0;for(unsigned n=0;n<nt;++n){unsigned id=d::r32(b,size_t(to+d::U(n)*4));d::need((n?id>prior:id==d::r32(b,at+48)),"GPU source triangle order");prior=id;}
  d::B bytes(b.begin()+size_t(vo),b.begin()+size_t(vo+d::U(vc)*40));bytes.insert(bytes.end(),b.begin()+size_t(io),b.begin()+size_t(io+d::U(ic)*2));d::need(d::crc(bytes)==d::r32(b,at+92),"GPU cluster CRC");rendered+=ic;
 }
 d::need(rendered==d::r32(b,40)&&rendered+d::U(s.rejected)*3==s.indices,"GPU original triangle accounting");
 std::sort(ranges.begin(),ranges.end());d::U end=pa;for(auto [lo,hi]:ranges){d::need(lo>=end&&lo-end<=63&&d::zeros(b,size_t(end),size_t(lo-end)),"GPU overlap/padding");end=hi;}d::need(aa>=end&&aa-end<=7&&d::zeros(b,size_t(end),size_t(aa-end)),"GPU auxiliary padding");
 d::need(an>=24&&!memcmp(b.data()+size_t(aa),"W2SAUX1",8),"GPU auxiliary signature");d::U p=aa+8;
 auto u32=[&](){d::need(p<=b.size()&&b.size()-p>=4,"GPU auxiliary u32");auto x=d::r32(b,size_t(p));p+=4;return x;};
 auto skip=[&](d::U n){d::need(p<=b.size()&&n<=b.size()-p,"GPU auxiliary range");p+=n;};
 auto align=[&](){auto n=(4-p%4)%4;d::need(d::zeros(b,size_t(p),size_t(n)),"GPU auxiliary zero padding");skip(n);};
 unsigned sets=u32(),raw=u32(),rejected=u32(),extras=u32();d::need(sets>0&&sets<=128&&(raw==0||raw==s.materials)&&rejected==s.rejected&&extras<=128,"GPU auxiliary counts");d::U vertices=0;
 for(unsigned i=0;i<sets;++i){unsigned n=u32(),mask=u32(),uniform=u32(),reserved=u32();d::need(n<=10000000&&(mask&~63u)==0&&!reserved&&(uniform==~0u||(uniform&~63u)==0),"GPU source presence flags");vertices+=n;if(uniform==~0u){skip(n);align();}for(auto [bit,width]:{std::pair{2u,12u},std::pair{4u,12u},std::pair{8u,12u},std::pair{32u,4u}})if(mask&bit)skip(d::U(n)*width);}
 d::need(vertices==s.vertices,"GPU auxiliary vertex accounting");skip(d::U(raw)*144);skip(d::U(rejected)*8);
 for(unsigned i=0;i<extras;++i){u32();unsigned n=u32(),width=u32();u32();d::need(n<=10000000&&width<=256,"GPU raw auxiliary dimensions");skip(d::U(n)*width);align();}
 unsigned other=u32();d::need(other<=65536,"GPU light metadata count");for(unsigned i=0;i<other;++i){u32();unsigned n=u32();skip(n);align();}
 unsigned markers=u32();d::need(markers<=65536,"GPU marker count");for(unsigned i=0;i<markers;++i){unsigned n=u32();d::need(n<=1048576,"GPU marker range");skip(n);align();}
 unsigned morphs=u32();d::need(morphs<=65536,"GPU morph count");skip(d::U(morphs)*4);unsigned invalid=u32();d::need(invalid<=s.vertices,"GPU malformed originals count");skip(d::U(invalid)*88);unsigned originalHeader=u32();d::need(originalHeader==0||originalHeader>=160,"GPU original header bytes");skip(originalHeader);align();d::need(p==b.size(),"GPU auxiliary trailing bytes");return s;
}
}
