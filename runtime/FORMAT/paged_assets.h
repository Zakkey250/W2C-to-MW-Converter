#pragma once
static J readEffectiveMaterials(const B& b){
 need(b.size()>=128&&!memcmp(b.data(),"W2MATB1",8)&&r32(b,8)==1&&r32(b,20)==88&&zeros(b,104,24),"effective material bank identity");unsigned count=r32(b,12),animations=r32(b,16);need(count<100000&&animations<10000,"effective material counts");U end=add(128,mul(count,88));need(end<=b.size(),"material bank record range");J materials=J::object();
 for(unsigned at:{76u,80u,84u,88u})rf(b,at);
 unsigned prior=0;for(unsigned i=0;i<count;++i){size_t at=128+size_t(i)*88;unsigned hash=r32(b,at);need((!i||hash>prior)&&r32(b,at+84)<=1,"material bank order/night flag");prior=hash;for(unsigned p:{56u,60u,64u,68u})rf(b,at+p);
  materials[std::to_string(hash)]={{"usage",r32(b,at+4)},{"blend",r32(b,at+8)},{"flags",r32(b,at+12)},{"tile",r32(b,at+16)},{"mask",r32(b,at+52)},{"night",r32(b,at+84)}};
 }
 std::set<unsigned> frames;for(unsigned i=0;i<animations;++i){need(end<=b.size()&&b.size()-end>=12,"animation bank header");unsigned fps=r32(b,size_t(end)),base=r32(b,size_t(end+4)),n=r32(b,size_t(end+8));end+=12;need(fps>0&&fps<=255&&base<=1&&n>0&&n<=255&&mul(n,4)<=b.size()-end,"animation bank range");for(unsigned j=0;j<n;++j)frames.insert(r32(b,size_t(end+U(j)*4)));end+=U(n)*4;}
 need(end==b.size(),"material bank trailing bytes");return {{"recordCount",count},{"animationCount",animations},{"materials",materials},{"frames",frames},
  {"default",{{"usage",r32(b,24)},{"blend",r32(b,28)},{"flags",r32(b,32)},{"tile",r32(b,36)},{"mask",r32(b,72)},{"night",0}}}};
}
static J validateDdsSource(const B& b){
 need(b.size()>=128&&!memcmp(b.data(),"DDS ",4)&&r32(b,4)==124&&r32(b,76)==32,"DDS source header");U height=r32(b,12),width=r32(b,16),mips=std::max(1u,r32(b,28));need(width>0&&height>0&&width<=16384&&height<=16384&&mips<=15&&r32(b,24)<=1&&(r32(b,112)&0xFE00)==0,"DDS source dimensions/type");
 unsigned fourcc=r32(b,84),bits=r32(b,88),flags=r32(b,80);bool compressed=(flags&4)!=0;unsigned block=0;
 if(compressed){if(fourcc==0x31545844)block=8;else if(fourcc==0x33545844||fourcc==0x35545844)block=16;else throw std::runtime_error("unsupported source DDS compression");}
 else need((flags&0x40)&&bits==32&&fourcc==0,"unsupported source DDS pixel format");
 U bytes=128,w=width,h=height,lastW=0,lastH=0;bool done=false;for(U i=0;i<mips;++i){need(!done,"excess source DDS mips");lastW=w;lastH=h;bytes=add(bytes,compressed?mul(mul((w+3)/4,(h+3)/4),block):mul(mul(w,h),4));done=w==1&&h==1;w=std::max<U>(1,w/2);h=std::max<U>(1,h/2);}
 // The frozen exporter wrote one extra compressed block after some chains
 // ending at 4x4. Retain it byte-for-byte; it is not a declared/sampleable mip.
 need(bytes==b.size()||(fourcc==0x31545844&&compressed&&lastW==4&&lastH==4&&add(bytes,block)==b.size()),"source DDS exact mip bytes/known legacy suffix");
 return {{"width",width},{"height",height},{"mipCount",mips},{"pixelFormat",hex(b.data()+76,32)},{"preservedTrailingBytes",b.size()-bytes}};
}
static J validateSourceArchive(const B& b,const std::string& type){
 need(oneof(type,{"visibility","overlap-groups","weather-zones","lights","flares","emitters","world-animations","pursuit-markers","prop-catalog","render-policy"}),"unknown preserved source class");J result={{"sourceClass",type}};
 if(type=="overlap-groups"){
  need(b.size()>=12&&r32(b,0)==0x31505247&&r32(b,4)==1&&r32(b,8)<100000,"overlap source identity");unsigned count=r32(b,8);U at=12;J groups=J::object();for(unsigned i=0;i<count;++i){need(at<=b.size()&&b.size()-at>=8,"overlap group header");unsigned id=r32(b,size_t(at)),n=r32(b,size_t(at+4));at+=8;need(id>0&&id<=count&&n>=2&&n<2171&&U(n)*4<=b.size()-at&&!groups.contains(std::to_string(id)),"overlap group counts");J members=J::array();unsigned prior=0;for(unsigned j=0;j<n;++j){unsigned section=r32(b,size_t(at+U(j)*4));need(!j||section>prior,"overlap membership order");members.push_back(section);prior=section;}groups[std::to_string(id)]=members;at+=U(n)*4;}need(at==b.size(),"overlap source trailing bytes");result["groups"]=groups;
 }else if(type=="lights"){
  need(b.size()>=8&&r32(b,0)==0x324c4d57&&r32(b,4)<100000&&8+U(r32(b,4))*60==b.size(),"light source structure");auto count=r32(b,4);for(unsigned i=0;i<count;++i){size_t at=8+size_t(i)*60;for(unsigned offset:{0u,4u,8u,12u,16u,20u,24u,28u,32u,36u,40u,48u,52u})rf(b,at+offset);need(rf(b,at+28)>0&&rf(b,at+28)<=500,"light source range");}result["count"]=count;
 }else if(type=="flares"){
  need(b.size()>=8&&r32(b,0)==0x32464d57&&r32(b,4)<100000&&8+U(r32(b,4))*36==b.size(),"flare source structure");for(unsigned i=0;i<r32(b,4);++i){size_t at=8+size_t(i)*36;need(r32(b,at+16)<=20,"source flare type");for(unsigned offset:{0u,4u,8u,24u,28u,32u})rf(b,at+offset);}
 }else if(type=="prop-catalog")need(b.size()>=8&&r32(b,0)==0x31504d57&&r32(b,4)<100000&&8+U(r32(b,4))*64==b.size(),"preserved prop catalog structure");
 else if(type=="weather-zones"){
  need(b.size()>=12&&r32(b,0)==0x315a4d57&&r32(b,4)==1&&r32(b,8)<10000,"weather source identity");U at=12;for(unsigned i=0;i<r32(b,8);++i){need(at<=b.size()&&b.size()-at>=12,"weather source entry");unsigned tag=r32(b,size_t(at)),n=r32(b,size_t(at+4));rf(b,size_t(at+8));at+=12;need(tag>=3&&tag<=5&&n>=3&&n<=128&&U(n)*8<=b.size()-at,"weather source polygon");for(U f=0;f<U(n)*2;++f)rf(b,size_t(at+f*4));at+=U(n)*8;}need(at==b.size(),"weather source trailing bytes");
 }
 return result;
}
