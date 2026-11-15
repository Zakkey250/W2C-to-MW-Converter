#pragma once
#include "Bundle.h"
#include <cmath>
namespace w2source {
inline unsigned cameraNameHash(const std::string& name){unsigned h=~0u;for(unsigned char c:name)h=h*33+c;return h;}
inline std::map<unsigned,std::string> validateNativeSequences(const d::B& b){
 d::need(b.size()>=8&&b.size()<=4*d::MiB&&d::r32(b,0)==0x8003b810&&d::r32(b,4)==b.size()-8,"source sequence envelope");
 std::map<unsigned,std::string> names;d::U at=8;unsigned groups=0;
 while(at<b.size()){
  d::need(at+8<=b.size()&&d::r32(b,at)==0x3b811,"source CARP chunk");auto n=d::r32(b,at+4);d::U base=at+8,end=base+n;d::need(n>=32&&end<=b.size()&&d::r32(b,base+16)==0x43415250&&d::r32(b,base+20)==26,"source CARP header");auto count=d::r32(b,base+24);d::need(count>0&&count<=4096&&32ull+16ull*count<=n,"source CARP count");std::set<unsigned> tags;
  for(unsigned i=0;i<count;++i){auto e=base+32+16ull*i;unsigned tag=d::r32(b,e),size=d::r32(b,e+4)>>8,key=d::r32(b,e+8);auto start=e+d::r32(b,e+12);d::need(tags.insert(tag).second&&start>=base+32+16ull*count&&start+size<=end,"source CARP entry extent");
   if(tag>>16==0x694e){d::need(size>10&&size<=128&&b[start+size-1]==0,"source private engine name extent");std::string name(reinterpret_cast<const char*>(b.data()+start),size-1);d::need(name.find('\0')==std::string::npos&&name.rfind("w2v2_seq_",0)==0&&names.emplace(key,name).second,"source private engine identity");}
   if(tag>>16==0x656c){d::need(size>=16,"source command-list header");auto commands=d::r32(b,start);d::need(commands<=4096&&16ull+16ull*commands<=size,"source command count");for(unsigned j=0;j<commands;++j){auto e2=start+16+16ull*j;auto kind=d::r32(b,e2),bytes=d::r32(b,e2+4);auto p=e2+d::r32(b,e2+8);d::need(bytes>=16&&bytes<=48&&bytes%16==0&&p>=start+16+16ull*commands&&p+bytes<=start+size&&d::r32(b,p)==kind,"source native event extent");if(kind==0x2554c48a){d::need(bytes==48&&d::r32(b,p+12)!=0x0a8471ec,"source absent fragment was not quarantined");for(unsigned f=0;f<5;++f)d::need(d::r32(b,p+28+f*4)<=1,"source MW fragment flags");}}
   }
  }at=end;++groups;d::need(groups<=4096,"source sequence group cap");
 }d::need(at==b.size()&&!names.empty(),"source sequence complete extent");return names;
}
inline std::set<unsigned> validateNativeCameras(const d::B& b,bool allowEmpty=false){
 d::need(b.size()>=8&&b.size()<=8*d::MiB&&d::r32(b,0)==0x8003b200&&d::r32(b,4)==b.size()-8,"source camera envelope");std::set<unsigned> scenes;d::U at=8;
 while(at<b.size()){d::need(at+16<=b.size()&&d::r32(b,at)==0x3b210,"source camera chunk");auto size=d::r32(b,at+4);d::U base=at+8,end=base+size;d::need(end<=b.size()&&size>=48&&d::r32(b,base+4)==1&&scenes.insert(d::r32(b,base)).second,"source camera identity/track count");auto track=base+8;d::need(d::zeros(b,track,12),"source camera transient pointers");auto count=d::r16(b,track+20);d::need(count>0&&count<=1024&&48ull+132ull*count==size,"source native camera key extent");float times[2];memcpy(times,b.data()+track+12,8);d::need(std::isfinite(times[0])&&std::isfinite(times[1])&&times[1]>times[0]&&times[1]<=10,"source camera time bounds");at=end;d::need(scenes.size()<=128,"source camera scene cap");}d::need(at==b.size()&&(allowEmpty||!scenes.empty()),"source camera complete extent");return scenes;
}
inline unsigned validateNativeMarkers(const d::B& b,const std::set<unsigned>& scenes){
 d::need(b.size()>=16&&d::r32(b,0)==0x314d4243&&d::r32(b,4)==1&&d::r32(b,12)==52,"source camera marker header");auto count=d::r32(b,8);d::need(count<=16384&&16ull+52ull*count==b.size(),"source camera marker extent");for(unsigned i=0;i<count;++i){d::U at=16+52ull*i;d::need(scenes.count(d::r32(b,at))&&d::zeros(b,at+4,8),"source camera marker scene/pointers");for(unsigned j=0;j<3;++j){float p;memcpy(&p,b.data()+at+20+4*j,4);d::need(std::isfinite(p),"source camera marker finite position");}}return count;
}
}
