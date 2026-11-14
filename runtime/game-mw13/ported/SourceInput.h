#pragma once
#include "../../source-format/Cursor.h"
#include "../../source-format/Model.h"
static w2v2disk::B SourceBytes(unsigned kind,const std::string& key){return v2Store.read(v2Store.find(kind,key));}
static w2source::Cursor SourceInput(unsigned kind,const std::string& key){return w2source::Cursor(SourceBytes(kind,key));}
static unsigned SourceKey(const char* name){unsigned h=0xffffffffu;for(;*name;++name)h=h*33+static_cast<unsigned char>(*name);return h;}
