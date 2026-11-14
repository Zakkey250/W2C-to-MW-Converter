#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace w2v2 {
// DDS byte ranges remain in the original format; no resampling or recompression.
struct TextureMip {unsigned width=0,height=0,rows=0,rowBytes=0;std::size_t offset=0,bytes=0;};
struct TextureLayout {
 unsigned width=0,height=0,levels=0,format=0,block=0;std::size_t bytes=0;
 std::array<TextureMip,14> mip{};
 bool parse(const void* input,std::size_t size){
  *this={};if(size<128||!input||std::memcmp(input,"DDS ",4))return false;
  auto p=static_cast<const unsigned char*>(input);auto u=[&](unsigned at){std::uint32_t x;std::memcpy(&x,p+at,4);return x;};
  if(u(4)!=124||u(76)!=32)return false;
  width=u(16);height=u(12);levels=std::max(1u,u(28));format=u(84);
  if(!width||!height||width>8192||height>8192||levels>mip.size())return false;
  if(format==0x31545844)block=8;
  else if(format==0x33545844||format==0x35545844)block=16;
  else if(!format&&u(88)==32&&u(92)==0xff0000&&u(96)==0xff00&&u(100)==0xff)format=21;
  else return false;
  unsigned maxLevels=1;for(auto n=std::max(width,height);n>1;n/=2)++maxLevels;
  if(levels>maxLevels)return false;
  std::size_t at=128;unsigned w=width,h=height;
  for(unsigned i=0;i<levels;++i){auto& m=mip[i];m={w,h,block?std::max(1u,(h+3)/4):h,block?std::max(1u,(w+3)/4)*block:w*4,at,0};m.bytes=std::size_t(m.rows)*m.rowBytes;
   if(at>size||m.bytes>size-at)return false;at+=m.bytes;w=std::max(1u,w/2);h=std::max(1u,h/2);
  }bytes=at-128;return true;
 }
};
struct TextureSlice {unsigned level=0,row=0,rows=0,width=0,height=0,y=0;std::size_t offset=0,bytes=0;};
struct TextureCursor {
 unsigned level=0,row=0;
 bool done(const TextureLayout& l)const{return level>=l.levels;}
 TextureSlice next(const TextureLayout& l,std::size_t budget)const{
  if(done(l))return {};const auto& m=l.mip[level];unsigned rows=unsigned(std::min<std::size_t>(m.rows-row,budget/m.rowBytes));
  if(!rows)return {};unsigned y=row*(l.block?4:1);return {level,row,rows,m.width,std::min(m.height-y,rows*(l.block?4u:1u)),y,m.offset+std::size_t(row)*m.rowBytes,std::size_t(rows)*m.rowBytes};
 }
 void advance(const TextureLayout& l,const TextureSlice& s){row+=s.rows;if(row==l.mip[level].rows){row=0;++level;}}
};
}
