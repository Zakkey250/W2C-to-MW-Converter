#pragma once
#include "Bundle.h"
namespace w2source {
// Byte access stays bounded after the enclosing asset's SHA/block verification.
// Native loaders receive separate writable copies; indexed source bytes remain
// immutable and no stdio temporary file is created.
class Cursor {
 d::B bytes_;size_t at_=0;
public:
 explicit Cursor(d::B bytes):bytes_(std::move(bytes)){}
 size_t remaining()const{return bytes_.size()-at_;}
 size_t size()const{return bytes_.size();}
 size_t position()const{return at_;}
 void read(void* out,size_t n){d::need(n<=remaining(),"typed source record truncated");if(n)memcpy(out,bytes_.data()+at_,n);at_+=n;}
 template<class T>T get(){static_assert(std::is_trivially_copyable_v<T>);T value;read(&value,sizeof(value));return value;}
 d::B take(size_t n){d::need(n<=remaining(),"typed source range truncated");d::B result(bytes_.begin()+at_,bytes_.begin()+at_+n);at_+=n;return result;}
 void end()const{d::need(at_==bytes_.size(),"typed source trailing data");}
};
}
