#pragma once
#include "Bundle.h"
namespace w2source {
// Validate the original context scene used by the actual runtime dispatch.
// Profile/sky/background templates do not become unconditional world draws.
inline unsigned validateGlobalContext(const d::J& meta,const d::B& b,const char* streamRole="x360"){
 d::need(d::str(meta["source"]["origin"]["role"])==streamRole&&d::str(meta["source"]["origin"]["chunk"])=="80034100","original world context role");
 d::need(b.size()>=32&&!memcmp(b.data(),"W2SSCNE1",8)&&d::r32(b,8)==1&&d::r32(b,12)==32&&d::r32(b,16)==26000,"global context scene identity");auto infos=d::r32(b,20),instances=d::r32(b,24);
 d::need(infos<=100000&&instances<=1000000&&32ull+d::U(infos)*72+d::U(instances)*96==b.size(),"global context original extent");
 for(unsigned i=0;i<instances;++i){auto at=32+d::U(infos)*72+d::U(i)*96;d::need(d::r16(b,size_t(at+84))<infos,"global context info reference");}
 return instances;
}
}
