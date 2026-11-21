#pragma once

// A separate native vault publishes only W2C collection exports. The game's
// original banks, startup path and save validation are left untouched.
namespace public_pursuit_overlay {
using namespace w2v2disk;
constexpr auto originalDbSlot=0x9258A4;
constexpr auto vaultLoader=0x664430;
constexpr auto attributeManagerSlot=0x90DCBC;
constexpr auto propClassSlot=0x926180;
constexpr uint32_t Collection=0x8E112EB7;
constexpr char bankName[]="w2";
struct View {
 size_t bin=0,binSize=0,vlt=0,vltSize=0,dependency=0,exports=0,exportsSize=0;
};
struct Row {uint32_t key,cls; size_t exportAt;};
static B overlay;
static std::vector<Row> additions;
static bool attempted=false,ready=false;
static void* retainedNativeBin=nullptr;

static void Write32(B& b,size_t at,uint32_t value){
 need(at<=b.size()&&b.size()-at>=4,"public vault write extent");
 memcpy(b.data()+at,&value,4);
}
static View Inspect(const B& b){
 need(b.size()>=128&&r32(b,0)==0x4B415056&&r32(b,4)==1&&r32(b,8)==64&&
      r32(b,16)==0&&!memcmp(b.data()+64,"db",3),"public vault db pack envelope");
 View v;
 v.binSize=r32(b,20);v.vltSize=r32(b,24);v.bin=r32(b,28);v.vlt=r32(b,32);
 need(v.bin>=128&&v.bin<=b.size()&&v.binSize<=b.size()-v.bin&&v.vlt>=v.bin+v.binSize&&
      v.vlt<=b.size()&&v.vltSize<=b.size()-v.vlt,"public vault db pack extents");
 size_t end=v.vlt+v.vltSize;
 for(size_t at=v.vlt;at<end;){
  need(end-at>=8,"public vault db chunk header");
  auto tag=r32(b,at),len=r32(b,at+4);
  need(len>=8&&len<=end-at,"public vault db chunk extent");
  if(tag==0x4465704E){need(!v.dependency&&len==48,"public vault db dependency shape");v.dependency=at;}
  if(tag==0x4578704E){need(!v.exports&&len>=12,"public vault db export shape");v.exports=at;v.exportsSize=len;}
  at+=len;
 }
 need(v.dependency&&v.exports&&r32(b,v.dependency+8)==2&&
      r32(b,v.dependency+12)==0x18228ADE&&r32(b,v.dependency+16)==0x610CE10B&&
      r32(b,v.dependency+20)==0&&r32(b,v.dependency+24)==7&&
      !memcmp(b.data()+v.dependency+28,"db.vlt\0db.bin",14),"public vault db dependency identity");
 return v;
}
static std::vector<Row> Rows(const B& bytes,const View& v){
 auto count=r32(bytes,v.exports+8);
 need(count<32768&&count<=(v.exportsSize-12)/20,"public vault db export count");
 std::vector<Row> rows;
 std::set<uint64_t> seen;
 for(unsigned i=0;i<count;++i){
  size_t entry=v.exports+12+i*20;
  if(r32(bytes,entry+4)!=Collection)continue;
  auto length=r32(bytes,entry+12),off=r32(bytes,entry+16);
  need(r32(bytes,entry+8)==0&&off<=v.vltSize&&length<=v.vltSize-off&&length>=32,
       "public vault collection extent");
  auto key=r32(bytes,v.vlt+off),cls=r32(bytes,v.vlt+off+4);
  need(key&&cls&&seen.insert((uint64_t(cls)<<32)|key).second,"public vault unique collection");
  rows.push_back({key,cls,entry});
 }
 return rows;
}
static void Prepare(B original,B merged){
 auto old=Inspect(original),next=Inspect(merged);
 std::set<uint64_t> stock;
 for(const auto& row:Rows(original,old))stock.insert((uint64_t(row.cls)<<32)|row.key);
 auto remaining=stock;
 auto rows=Rows(merged,next);
 std::vector<Row> extra;
 for(const auto& row:rows){
  auto id=(uint64_t(row.cls)<<32)|row.key;
  if(!remaining.erase(id))extra.push_back(row);
 }
 need(remaining.empty()&&!extra.empty()&&extra.size()<=4096,"public vault additive collection closure");
 // Preserve offsets and fixups within the binary bank. Compact the export
 // descriptors in their existing chunk so only new collections are registered.
 size_t write=next.exports+12;
 for(const auto& row:extra){
  memmove(merged.data()+write,merged.data()+row.exportAt,20);
  write+=20;
 }
 std::fill(merged.begin()+write,merged.begin()+next.exports+next.exportsSize,0);
 Write32(merged,next.exports+8,uint32_t(extra.size()));
 memcpy(merged.data()+64,bankName,sizeof(bankName));
 auto dep=next.dependency;
 Write32(merged,dep+12,0x59350D10); // VLT32("w2.vlt")
 Write32(merged,dep+16,0x1ECB5599); // VLT32("w2.bin")
 memcpy(merged.data()+dep+28,"w2.vlt\0w2.bin",14);
 overlay=std::move(merged);
 additions=std::move(extra);
}
static bool Publish(){
 if(overlay.empty()||ready)return true;
 if(attempted)return false;
 if(*reinterpret_cast<unsigned*>(0x925E90)<5||!*reinterpret_cast<void**>(originalDbSlot)||
    !*reinterpret_cast<void**>(propClassSlot))return false;
 attempted=true;
 auto db=*reinterpret_cast<void**>(attributeManagerSlot);
 const BYTE guard[]={0x83,0xEC,0x44,0x53,0x55,0x56,0x57,0xE8,0x04,0x8A,0xDF,0xFF};
 auto load=reinterpret_cast<void*(__cdecl*)(void*,const char*,void**,unsigned)>(vaultLoader);
 auto findClass=reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(0x455BC0);
 auto findRow=reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(0x455960);
 if(!db||memcmp(reinterpret_cast<void*>(vaultLoader),guard,sizeof(guard))){
  Log("PUBLIC_PURSUIT_OVERLAY_REFUSED native bank entry");W2V2Status.error=1;return false;
 }
 for(const auto& row:additions){
  auto cls=findClass(db,row.cls);
  if(!cls||findRow(cls,row.key)){
   Log("PUBLIC_PURSUIT_OVERLAY_REFUSED class=%08X key=%08X",row.cls,row.key);
   W2V2Status.error=1;return false;
  }
 }
 auto bank=load(overlay.data(),bankName,&retainedNativeBin,0);
 unsigned found=0;
 for(const auto& row:additions){
  auto cls=findClass(db,row.cls);
  if(cls&&findRow(cls,row.key))++found;
 }
 ready=bank&&retainedNativeBin&&found==additions.size();
 Log("PUBLIC_PURSUIT_OVERLAY_%s entries=%u resolved=%u bank=%u retained=%u original_disk=1",
     ready?"REGISTERED":"REFUSED",unsigned(additions.size()),found,unsigned(bank!=nullptr),
     unsigned(retainedNativeBin!=nullptr));
 if(!ready)W2V2Status.error=1;
 return ready;
}
}

static bool EnsureSourcePublicPursuitVault(){
 return public_pursuit_overlay::Publish();
}

static bool InstallSourcePublicPursuitVault(){
 try {
  using namespace w2v2disk;
  const auto& bundle=v2Store.bundle();
  const auto contract=bundle.find(61,"native/pursuit-vault-base");
  if(!contract)return true;
  for(const char* entry:{"prop-placements","prop-hierarchies","prop-parameters","prop-convex",
                         "prop-models","prop-textures","sequences","sequence-routes","cameras","camera-markers"})
   need(bundle.find(61,std::string("native/")+entry)!=nullptr,"public pursuit asset closure");
  const auto baseline=parse(bundle.read(*contract),4096);
  need(baseline.is_object()&&baseline.size()==4,"public pursuit vault baseline");
  // Only attributes.bin is loaded by the additive overlay. The other
  // fingerprints record the conversion inputs and do not gate world startup.
  const auto expected=str(baseline.at("attributes.bin"),64);
  File original(std::filesystem::path(root)/"GLOBAL"/"attributes.bin");
  need(IsPublicDigest(expected)&&original.fullHash()==expected,"MW attributes.bin changed after pursuit conversion");
  const auto entry=bundle.find(62,"native-vault/attributes.bin");
  need(entry&&entry->bytes>0&&entry->bytes<=16*MiB,"public pursuit vault entry");
  auto merged=bundle.read(*entry);
  need(merged.size()==entry->bytes&&sha(merged)==entry->sha,"public pursuit vault integrity");
  public_pursuit_overlay::Prepare(original.read(0,original.size,16*MiB),std::move(merged));
  sourceCarbonPursuit=true;
  Log("PUBLIC_PURSUIT_OVERLAY_PREPARED entries=%u base_already_loaded=%u",
      unsigned(public_pursuit_overlay::additions.size()),
      unsigned(*reinterpret_cast<void**>(public_pursuit_overlay::originalDbSlot)!=nullptr));
  return true;
 }catch(const std::exception& error){
  ++v2Failures;
  W2V2Status.error=1;
  Log("PUBLIC_PURSUIT_VAULT_REFUSED reason=%s",error.what());
  return false;
 }
}

