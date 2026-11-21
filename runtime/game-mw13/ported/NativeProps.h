#pragma once
// The simulation remains in MW's own smackable spawner and collision engine.
// This bridge owns only converted model buffers and the rendering transform.
struct PropTriangle {unsigned texture;int xyz[9];bool operator<(const PropTriangle& b)const{if(texture!=b.texture)return texture<b.texture;return std::lexicographical_compare(xyz,xyz+9,b.xyz,b.xyz+9);}};
static PropTriangle TriangleKey(unsigned texture,Vec3 a,Vec3 b,Vec3 c){std::array<std::array<int,3>,3> points;Vec3 v[]={a,b,c};for(int n=0;n<3;n++)points[n]={int(std::nearbyint(v[n].x*100)),int(std::nearbyint(v[n].y*100)),int(std::nearbyint(v[n].z*100))};std::sort(points.begin(),points.end());PropTriangle result{};result.texture=texture;memcpy(result.xyz,points.data(),36);return result;}
static std::set<PropTriangle> propGhostTriangles;
static bool propCatalog=false;
static void RemovePropGhosts(CpuSection& data){std::set<PropTriangle> keys;const auto* filter=&propGhostTriangles;if(propCatalog){char path[100];sprintf_s(path,"NativeProps/Ghosts/%u.bin",data.header.section);FILE* f=nullptr;fopen_s(&f,Path(path).c_str(),"rb");if(!f)return;unsigned count=0;if(fread(&count,4,1,f)!=1||count>1000000){fclose(f);return;}for(unsigned i=0;i<count;i++){PropTriangle key;if(fread(&key,40,1,f)!=1){fclose(f);return;}keys.insert(key);}fclose(f);filter=&keys;}if(filter->empty())return;std::vector<unsigned> indices,overlap;unsigned removed=0;for(auto& m:data.meshes){unsigned start=unsigned(indices.size());for(unsigned n=0;n<m.indexCount;n+=3){auto ix=data.indices.data()+m.firstIndex+n;auto v=data.vertices.data()+m.firstVertex;if(filter->count(TriangleKey(m.texture,v[ix[0]].position,v[ix[1]].position,v[ix[2]].position))){++removed;continue;}indices.insert(indices.end(),ix,ix+3);if(!data.triangleOverlap.empty())overlap.push_back(data.triangleOverlap[(m.firstIndex+n)/3]);}m.firstIndex=start;m.indexCount=unsigned(indices.size())-start;}data.indices=std::move(indices);data.triangleOverlap=std::move(overlap);data.header.indices=unsigned(data.indices.size());if(removed)Log("PROP_GHOST_REMOVED section=%u triangles=%u",data.header.section,removed);}
// The native spawner's +0x34 interface supplies bounds, attributes, model ID,
// transform and COM ownership to the native Smackable factory.
struct PropParams {unsigned type=0,name=0xA6B47FAC;void* data=nullptr;unsigned pad=0;D3DMATRIX matrix{};bool virgin=true;BYTE padding[3]{};void* model=nullptr;bool simple=false;BYTE padding2[3]{};};
static_assert(sizeof(PropParams)==92);
struct NativeProp {w2v2::Epoch queuedEpoch;unsigned id=0;BYTE record[64]{};CpuSection cpu;Section gpu;void* spawner=nullptr;void* sim=nullptr;void* renderVtable[7]{};D3DMATRIX last{};D3DMATRIX initial{};bool readQueued=false,readComplete=false;bool initialized=false;bool refused=false;bool nativeRender=false;bool nativeTouched=false;bool nativeFractured=false;bool rockportCushion=false;bool cushionMotionLogged=false;bool authored=false;};
static std::vector<std::unique_ptr<NativeProp>> nativeProps;
static std::set<NativeProp*> activeProps;
static std::map<void*,NativeProp*> propModelOwners;
static std::map<void*,NativeProp*> propChildSims;
static void PropObstacleOwnerGone(unsigned);
static void PropObstacleModelGone(void*);

static std::map<void*,NativeProp*> propPayloadPieces;
static std::map<void*,D3DMATRIX> propPayloadDrawMatrices;
#ifdef MW_PROP_SHADER_AUDIT
#include "MwPropShaderAudit.h"
#endif
static std::deque<NativeProp*> propSpawnQueue;
static void* propCollisionBundle=nullptr;
static bool propCollisionReady=false;
// gFastMem backs native contact and smackable arrays. The retail initializer
// disables expansion after initializing its 2.2 MB arena. Keep native allocation
// and linked-block destruction, allowing this arena to expand on actual demand.
static bool (__thiscall* propOriginalFastMemGrow)(void*,unsigned);
static bool __fastcall PropFastMemGrow(void* self,void*,unsigned requested){
 if(self==reinterpret_cast<void*>(0x925B30)&&requested>0&&requested<=1024){
  auto fields=reinterpret_cast<unsigned*>(static_cast<BYTE*>(self)+0x104);
  unsigned disabled=fields[1];fields[0]=std::max(fields[0],2097152u);fields[1]=0;
  bool result=propOriginalFastMemGrow(self,requested);fields[1]=disabled;
  Log("PROP_FASTMEM_GROW request=%u disabled=%u chunk=%u success=%u block=%08X",requested,disabled,fields[0],unsigned(result),fields[3]);
  return result;
 }
 return propOriginalFastMemGrow(self,requested);
}
// Read-only diagnostics: preserve the native hull selection and response.
static int (__thiscall* propOriginalHullSelection)(void*,const Vec3*,const BYTE*,int,unsigned);
static int __fastcall PropHullSelection(void* self,void*,const Vec3* point,const BYTE* edges,int count,unsigned flags){
 auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());int result=propOriginalHullSelection(self,point,edges,count,flags);
 static DWORD previous=0;DWORD now=GetTickCount();
 if(point&&point->x>-2720&&point->x<-2640&&point->z>210&&point->z<280&&caller>=0x775BE0&&caller<0x776600&&now-previous>250){previous=now;
  Log("PROP_CONTACT_HULL caller=%08X count=%d selected=%d point=%.4f,%.4f,%.4f",unsigned(caller),count,result,point->x,point->y,point->z);
  if(edges&&count>0&&count<=64)for(int n=0;n<count;n++){auto e=reinterpret_cast<const float*>(edges+64*n);Log("PROP_CONTACT_EDGE index=%d axis=%.4f,%.4f range=%.4f,%.4f center=%.4f,%.4f,%.4f",n,e[4],e[5],e[8],e[9],e[12],e[13],e[14]);}
 }
 return result;
}
static void (__thiscall* propOriginalBoundVelocity)(void*,Vec3*);
static void __fastcall PropBoundVelocity(void* self,void*,Vec3* out){
 auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());propOriginalBoundVelocity(self,out);
 if(caller!=0x77606B)return;auto it=propModelOwners.find(static_cast<BYTE*>(self)-0x60);if(it==propModelOwners.end())return;
 static DWORD previous=0;DWORD now=GetTickCount();if(now-previous>250){previous=now;Log("PROP_CONTACT_PLANAR_PASS id=%u velocity=%.4f,%.4f,%.4f",it->second->id,out->x,out->y,out->z);}
}
static int (__thiscall* propOriginalContactCandidates)(void*,void**,unsigned);
static int __fastcall PropContactCandidates(void* self,void*,void** output,unsigned capacity){
 int count=propOriginalContactCandidates(self,output,capacity);static DWORD previous=0;DWORD now=GetTickCount();
 if(now-previous>1000&&count>=0&&unsigned(count)<=capacity){previous=now;unsigned owned=0;
  for(int n=0;n<count;n++){auto bound=static_cast<BYTE*>(output[n]);auto it=propModelOwners.find(bound-0x60);if(it==propModelOwners.end())continue;++owned;auto p=it->second;
   D3DMATRIX m{};Vec3 half{};auto vt=*reinterpret_cast<void***>(bound);reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[1])(bound,&m);reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[4])(bound,&half);
   if(p->id>=10007&&p->id<=10010)Log("PROP_CONTACT_CANDIDATE id=%u matrix=%.4f,%.4f,%.4f half=%.4f,%.4f,%.4f axisX=%.4f,%.4f,%.4f axisZ=%.4f,%.4f,%.4f",p->id,m._41,m._42,m._43,half.x,half.y,half.z,m._11,m._12,m._13,m._31,m._32,m._33);
  }
  Log("PROP_CONTACT_CANDIDATES total=%d owned=%u",count,owned);
 }
 return count;
}
static bool propEffectsDiagnostics=false;
static bool propPursuitDiagnostics=false;
static bool propPursuitPayload=false;
static std::set<unsigned> propPursuitAssets;
static bool SourceNativePackets(const NativeProp* p){
 if(!p)return false;
 const unsigned asset=*reinterpret_cast<const unsigned*>(p->record+32);
 // Native MW tolls have byte-preserved models and destruction trees. Submit
 // them through the same native packet path, without a competing static draw.
 switch(asset){case 0x2528FE86:case 0x7BC807F9:case 0xAA27F4C6:case 0xB6D45FE1:case 0xBA0A9F07:return true;}
 return propPursuitPayload&&propPursuitAssets.count(asset)!=0;
}
// MW's utility grid is built for its original streamed map. Retain its normal
// query, and supplement missed W2C breaker entries with the very same native
// narrow phase and dispatch. Never invent a hit or replace the source bounds.
static bool propUtilityBridge=false;
static std::set<void*>* propUtilityVisited=nullptr;
static bool (__thiscall* propOriginalUtilityTest)(void*,void*,void*,float);
static void (__thiscall* propOriginalUtilityQuery)(void*,void*,float);
static bool __fastcall PropUtilityTest(void* manager,void*,void* vehicle,void* polygon,float dt){
 if(propUtilityVisited)propUtilityVisited->insert(polygon);
 bool result=propOriginalUtilityTest(manager,vehicle,polygon,dt);
 if(worldLogEnabled&&propPursuitDiagnostics){for(auto p:activeProps){
  if(p->id!=7743||!p->spawner)continue;auto wrapper=*reinterpret_cast<void***>(static_cast<BYTE*>(p->spawner)+0xD0);
  if(wrapper&&*wrapper==polygon){static DWORD last=0;DWORD now=GetTickCount();if(now-last>200){last=now;auto flags=*reinterpret_cast<unsigned*>(static_cast<BYTE*>(polygon)+0x10);Log("CARBON_PB_CONTACT_QUERY_ACTIVE id=%u result=%u dt=%.5f flags=%08X",p->id,unsigned(result),dt,flags);}break;}
 }}
 return result;
}
static void __fastcall PropUtilityQuery(void* manager,void*,void* vehicle,float dt){
 std::set<void*> visited;auto previous=propUtilityVisited;propUtilityVisited=&visited;
 propOriginalUtilityQuery(manager,vehicle,dt);propUtilityVisited=previous;
 if(!propUtilityBridge||!vehicle||!std::isfinite(dt)||dt<0||dt>.25f)return;
 auto vt=*reinterpret_cast<void***>(vehicle);
 unsigned mask=reinterpret_cast<unsigned(__thiscall*)(void*)>(vt[20])(vehicle);
 auto position=reinterpret_cast<const Vec3*(__thiscall*)(void*)>(vt[8])(vehicle);
 auto velocity=reinterpret_cast<const Vec3*(__thiscall*)(void*)>(vt[9])(vehicle);
 float radius=reinterpret_cast<float(__thiscall*)(void*)>(vt[5])(vehicle);
 if(!mask||!position||!velocity||!std::isfinite(radius)||radius<0||radius>100)return;
 unsigned examined=0,missed=0,hits=0;
 for(auto p:activeProps){
  if(!p->spawner||!propPursuitAssets.count(*reinterpret_cast<unsigned*>(p->record+32)))continue;
  auto wrapper=*reinterpret_cast<BYTE**>(static_cast<BYTE*>(p->spawner)+0xD0);if(!wrapper)continue;
  auto polygon=*reinterpret_cast<BYTE**>(wrapper);if(!polygon)continue;
  unsigned flags=*reinterpret_cast<unsigned*>(polygon+0x10),groups=flags>>8;
  if(!(groups&1)||!(groups&mask)||((groups&0x400)&&!*(static_cast<BYTE*>(manager))))continue;
  auto center=reinterpret_cast<const Vec3*>(polygon+0x30);
  float reach=radius+*reinterpret_cast<float*>(polygon+0x3c)+std::sqrt(Dot(*velocity,*velocity))*dt;
  if(!std::isfinite(reach)||reach>150||std::abs(position->x-center->x)>reach||std::abs(position->z-center->z)>reach)continue;
  ++examined;if(visited.count(polygon))continue;++missed;
  if(!propOriginalUtilityTest(manager,vehicle,polygon,dt))continue;
  auto keyOwner=reinterpret_cast<BYTE*(__thiscall*)(void*)>(vt[1])(vehicle);if(!keyOwner)continue;
  unsigned key=*reinterpret_cast<unsigned*>(keyOwner+8);
  reinterpret_cast<void(__thiscall*)(void*,void*,unsigned)>(0x7817B0)(manager,polygon,key);++hits;
  Log("PURSUIT_NATIVE_UTILITY_HIT id=%u key=%08X dt=%.6f source_bounds=1",p->id,key,dt);
 }
 static DWORD last=0;if(propPursuitDiagnostics&&examined&&GetTickCount()-last>1000){last=GetTickCount();Log("PURSUIT_NATIVE_UTILITY_QUERY seen=%u nearby=%u missed=%u hits=%u",unsigned(visited.size()),examined,missed,hits);}
}
static std::map<unsigned,std::string> propPursuitParameters;
static bool propNativeReference=false;
static void* (__cdecl* propOriginalSpawner)(void*,void*,unsigned);
static void* __cdecl PropReferenceSpawner(void* record,void* resource,unsigned flags){
 auto result=propOriginalSpawner(record,resource,flags);if(!result||!record)return result;
 auto xyz=reinterpret_cast<float*>(static_cast<BYTE*>(record)+16);
 const unsigned referenceSolid=*reinterpret_cast<unsigned*>(static_cast<BYTE*>(result)+0xcc);Log("PROP_REFERENCE_SCENERY solid=%08X at=%.3f,%.3f,%.3f flags=%08X",referenceSolid,xyz[0],xyz[1],xyz[2],flags);if(referenceSolid!=0x92D18C7F)return result;
 auto p=std::make_unique<NativeProp>();p->id=100000+unsigned(nativeProps.size());memcpy(p->record,record,64);p->spawner=result;p->nativeRender=true;p->initialized=true;
 const unsigned solid=*reinterpret_cast<unsigned*>(static_cast<BYTE*>(result)+0xcc);if(solid==0x92D18C7F&&std::abs(xyz[0]+1394.349365f)<.1f&&std::abs(xyz[2]-2164.277344f)<.1f)p->id=12905;
 propModelOwners[static_cast<BYTE*>(result)+0x34]=p.get();activeProps.insert(p.get());
 Log("PROP_NATIVE id=%u spawner=%p asset=%08X matrix=1 reference=1 at=%.3f,%.3f,%.3f flags=%08X",p->id,result,solid,xyz[0],xyz[1],xyz[2],flags);nativeProps.push_back(std::move(p));return result;
}
static bool (__thiscall* propOriginalWorldContact)(void*,const void*,const Vec3*,void*);
static bool __fastcall PropWorldContactTrace(void* self,void*,const void* info,const Vec3* velocity,void* primitive){
 auto sim=*reinterpret_cast<void**>(static_cast<BYTE*>(self)-0x30);auto it=propChildSims.find(sim);
 static DWORD previous=0;static unsigned initial=0;DWORD now=GetTickCount();
 auto v=static_cast<const float*>(info);
 if(initial++<20||((it!=propChildSims.end()|| (v[0]>-1405&&v[0]<-1385&&v[2]>2040&&v[2]<2060))&&now-previous>=100)){previous=now;
 Log("PROP_WORLD_CONTACT id=%u sim=%p point=%.4f,%.4f,%.4f normal=%.5f,%.5f,%.5f penetration=%.5f primitive=%p",it==propChildSims.end()?0xffffffff:it->second->id,sim,v[0],v[1],v[2],v[4],v[5],v[6],v[7],primitive);}
 return propOriginalWorldContact(self,info,velocity,primitive);
}
static bool propHierarchyEnabled=false;
static std::map<unsigned,std::vector<BYTE>> propHierarchies;
static std::map<unsigned,std::unique_ptr<std::array<BYTE,24>>> propHierarchyModels;
static bool propHierarchyResolved=false;
static NativeProp* propConstructing=nullptr;
static void* (__thiscall* propOriginalConstruct)(void*,unsigned,void*,unsigned,void*,void*,void*,unsigned,bool);
static void* __fastcall PropConstructHook(void* self,void*,unsigned solid,void* placement,unsigned name,void* parent,void* resource,void* tree,unsigned node,bool graphics){
 // The stock constructor derives solid/tree from the original scenery handle.
 // Our placement records borrow that ABI handle, not its model identity. Supply
 // the converted identity before native collision, graphics and sequencer init.
 auto p=propConstructing;
 if(p&&!parent&&node==0&&name==0x9756DF79){auto it=propHierarchies.find(*reinterpret_cast<unsigned*>(p->record+32));if(it!=propHierarchies.end()){auto replacement=it->second.data();unsigned corrected=*reinterpret_cast<unsigned*>(replacement);if(propPursuitDiagnostics)Log("PROP_MODEL_IDENTITY id=%u old=%08X new=%08X old_tree=%p new_tree=%p",p->id,solid,corrected,tree,replacement);solid=corrected;tree=replacement;}}
 return propOriginalConstruct(self,solid,placement,name,parent,resource,tree,node,graphics);
}
static std::vector<void*> propNativeSolidBundles;
static bool propNativeSolidsReady=false;
static bool propNativeSolidsValid=false;
static NativeProp* propFactorySelected=nullptr;
static bool (__thiscall* propOriginalEvent)(void*,unsigned,float,void*,unsigned);
static bool PropHasSequence(void* outer,void* sequence){if(!outer)return false;if(outer==sequence)return true;auto b=static_cast<BYTE*>(outer);unsigned count=*reinterpret_cast<unsigned*>(b+0x28);auto engines=*reinterpret_cast<BYTE**>(b+0x24);return count<32&&engines&&static_cast<BYTE*>(sequence)>=engines&&static_cast<BYTE*>(sequence)<engines+count*64;}
#include "CarbonBreakerBackground.h"
static bool __fastcall PropEventHook(void* sequence,void*,unsigned event,float time,void* data,unsigned mode){
 NativeProp* owner=propFactorySelected;
 if(!worldLogEnabled||(!propEffectsDiagnostics&&!propPursuitDiagnostics))return CarbonProcessEvent(sequence,event,time,data,mode);
 if(!owner)for(auto p:activeProps){if(PropHasSequence(*reinterpret_cast<void**>(static_cast<BYTE*>(p->spawner)+0x6C),sequence)){owner=p;break;}if(p->sim){auto vt=*reinterpret_cast<void***>(p->sim);if(PropHasSequence(reinterpret_cast<void*(__thiscall*)(void*)>(vt[29])(p->sim),sequence)){owner=p;break;}}}
 bool result=CarbonProcessEvent(sequence,event,time,data,mode);
 if(owner)Log("PROP_EVENT id=%u event=%08X mode=%u accepted=%u sequence=%p",owner->id,event,mode,result,sequence);
 return result;
}
#include "CarbonBreakerPrototype.h"
#include "SourceProps.h"
static void LoadPropHierarchies(){
 if(sourceRuntime){LoadSourcePropHierarchies();return;}
 propHierarchyEnabled=GetPrivateProfileIntA("Probe","NativePropHierarchy",0,Path("W2CMWv2Bootstrap.ini").c_str())!=0;if(!propHierarchyEnabled)return;
 FILE* f=nullptr;fopen_s(&f,Path("NativeProps/hierarchies.bin").c_str(),"rb");if(!f)return;unsigned magic,count;bool valid=fread(&magic,4,1,f)==1&&fread(&count,4,1,f)==1&&magic==0x31485057&&count<10000;
 for(unsigned i=0;valid&&i<count;i++){unsigned asset,size;valid=fread(&asset,4,1,f)==1&&fread(&size,4,1,f)==1&&size>=24&&size<=520&&(size-8)%16==0;if(!valid)break;std::vector<BYTE> tree(size);valid=fread(tree.data(),1,size,f)==size&&*reinterpret_cast<unsigned*>(tree.data()+4)==(size-8)/16;
 for(unsigned n=0;valid&&n<(size-8)/16;n++)valid=*reinterpret_cast<unsigned*>(tree.data()+16+n*16)==0&&tree[22+n*16]+tree[23+n*16]<=(size-8)/16;
 if(valid)propHierarchies[asset]=std::move(tree);}
 valid=valid&&fgetc(f)==EOF;fclose(f);if(!valid){propHierarchies.clear();Log("PROP_HIERARCHY_REFUSED file");}else Log("PROP_HIERARCHY_PACK assets=%u",unsigned(propHierarchies.size()));
 if(GetPrivateProfileIntA("Probe","PursuitPayload",0,Path("W2CMWv2Bootstrap.ini").c_str())){
  f=nullptr;fopen_s(&f,Path("PursuitPayload/hierarchies.bin").c_str(),"rb");if(!f){Log("PURSUIT_PAYLOAD_REFUSED missing hierarchy");return;}
  unsigned magic=0,count=0;bool ok=fread(&magic,4,1,f)==1&&fread(&count,4,1,f)==1&&magic==0x31485057&&count>0&&count<=4096;std::map<unsigned,std::vector<BYTE>> staged;
  for(unsigned i=0;ok&&i<count;i++){unsigned asset=0,size=0;ok=fread(&asset,4,1,f)==1&&fread(&size,4,1,f)==1&&size>=24&&size<=520&&(size-8)%16==0&&!staged.count(asset);if(!ok)break;std::vector<BYTE> tree(size);ok=fread(tree.data(),1,size,f)==size&&*reinterpret_cast<unsigned*>(tree.data()+4)==(size-8)/16;for(unsigned j=0;ok&&j<(size-8)/16;j++)ok=*reinterpret_cast<unsigned*>(tree.data()+16+j*16)==0&&tree[22+j*16]+tree[23+j*16]<=(size-8)/16&&(tree[20+j*16]<(size-8)/16||tree[20+j*16]==255);if(ok)staged[asset]=std::move(tree);}
  ok=ok&&fgetc(f)==EOF;fclose(f);if(!ok||staged.size()!=count){Log("PURSUIT_PAYLOAD_REFUSED hierarchy closure");return;}
  std::map<unsigned,std::string> parameters;f=nullptr;fopen_s(&f,Path("PursuitPayload/parameters.bin").c_str(),"rb");
  if(f){unsigned pm=0,pc=0;ok=fread(&pm,4,1,f)==1&&fread(&pc,4,1,f)==1&&pm==0x32505057&&pc>=count&&pc<=count+64;
   for(unsigned i=0;ok&&i<pc;i++){unsigned asset=0;char name[64]{};ok=fread(&asset,4,1,f)==1&&fread(name,1,64,f)==64&&(staged.count(asset)||propHierarchies.count(asset))&&!parameters.count(asset);unsigned length=0;while(length<64&&name[length])++length;ok=ok&&length>9&&length<64&&!memcmp(name,"w2mw_pb2_",9);for(unsigned j=0;ok&&j<length;j++)ok=(name[j]>='a'&&name[j]<='z')||(name[j]>='0'&&name[j]<='9')||name[j]=='_';for(unsigned j=length;ok&&j<64;j++)ok=name[j]==0;if(ok)parameters[asset]=name;}
   ok=ok&&parameters.size()==pc&&fgetc(f)==EOF;for(auto& pair:staged)ok=ok&&parameters.count(pair.first);fclose(f);
  }else{
   ok=count==3&&staged.count(0x600B6636)&&staged.count(0x7D6FF632)&&staged.count(0x17CB6E16);
   if(ok){parameters[0x600B6636]="w2mw_pb_woodroofsupport";parameters[0x7D6FF632]="w2mw_pb_woodroof";parameters[0x17CB6E16]="w2mw_pb_bigrollingtorus";}
  }
  if(!ok){Log("PURSUIT_PAYLOAD_REFUSED parameter closure");return;}
  propPursuitParameters=std::move(parameters);for(auto& pair:staged){propPursuitAssets.insert(pair.first);propHierarchies[pair.first]=std::move(pair.second);}propPursuitPayload=true;Log("PURSUIT_PAYLOAD_HIERARCHIES assets=%u original_nodes=1 parameters=%u",count,unsigned(propPursuitParameters.size()));
 }
}
static void TracePropPhysics(NativeProp& p){
 if(!propEffectsDiagnostics||!p.sim)return;
 __try{
  auto vt=*reinterpret_cast<void***>(p.sim);Vec3 linear{},angular{};
  reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[26])(p.sim,&linear);
  reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[27])(p.sim,&angular);
  auto sequence=reinterpret_cast<void*(__thiscall*)(void*)>(vt[29])(p.sim);
  Log("PROP_MOTION id=%u velocity=%.3f,%.3f,%.3f angular=%.3f,%.3f,%.3f sequencer=%p parts=%08X",p.id,linear.x,linear.y,linear.z,angular.x,angular.y,angular.z,sequence,*reinterpret_cast<unsigned*>(static_cast<BYTE*>(p.spawner)+0xDC));
 }__except(EXCEPTION_EXECUTE_HANDLER){Log("PROP_DIAGNOSTIC_FAULT id=%u code=%08X",p.id,GetExceptionCode());}
}
static void TracePropProxy(NativeProp& p){
 if(!propEffectsDiagnostics)return;
 __try{
  auto proxy=*reinterpret_cast<void**>(static_cast<BYTE*>(p.spawner)+0x80);
  Log("PROP_PROXY id=%u proxy=%p",p.id,proxy);
  if(proxy){auto vt=*reinterpret_cast<void***>(proxy);Log("PROP_PROXY_VTABLE id=%u table=%p slots=%p,%p,%p",p.id,vt,vt[0],vt[1],vt[2]);}
 }__except(EXCEPTION_EXECUTE_HANDLER){Log("PROP_PROXY_FAULT id=%u",p.id);}
}
static void RegisterPropCollision(){
 if(propCollisionReady)return;auto manager=*reinterpret_cast<void**>(0x926180);if(!manager)return;
 if(sourceRuntime){propCollisionReady=RegisterSourcePropConvex();return;}
 propCollisionReady=true;FILE* f=nullptr;fopen_s(&f,Path("NativeProps/collision.bun").c_str(),"rb");if(!f)return;
 fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);if(bytes<8||bytes>4000000){fclose(f);Log("PROP_COLLISION_REFUSED size");return;}
 auto data=VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);bool ok=data&&fread(data,1,bytes,f)==size_t(bytes);fclose(f);
 if(!ok||*static_cast<unsigned*>(data)!=0x8003B900||static_cast<unsigned*>(data)[1]!=unsigned(bytes-8)){if(data)VirtualFree(data,0,MEM_RELEASE);Log("PROP_COLLISION_REFUSED header");return;}
 const BYTE guard[]={0x64,0xA1,0,0,0,0,0x6A,0xFF};if(memcmp(reinterpret_cast<void*>(0x687240),guard,sizeof(guard))){VirtualFree(data,0,MEM_RELEASE);Log("PROP_COLLISION_REFUSED executable");return;}
 // Native table and convex-cloud pointers reference this stable, aligned block.
 // Registration occurs after the game's native resource pools are initialized.
 if(reinterpret_cast<int(__cdecl*)(void*)>(0x687240)(data)){propCollisionBundle=data;Log("PROP_COLLISION_NATIVE registered bytes=%ld",bytes);}else{VirtualFree(data,0,MEM_RELEASE);Log("PROP_COLLISION_REFUSED loader");}
}
static void ResolvePropHierarchies(){
 if(!propHierarchyEnabled||propHierarchyResolved||!propCollisionReady)return;
 if(sourceRuntime&&!propNativeSolidsReady&&!RegisterSourcePropModels())return;
#ifdef W2C_PUBLIC_BUILD
 if(sourceCarbonPursuit){
#endif
 LoadCarbonBreakerPrototype();
 if(sourceRuntime&&!sourceSequencesReady)return;
#ifdef W2C_PUBLIC_BUILD
 }
#endif
 if(!propNativeSolidsReady){
  propNativeSolidsReady=true;FILE* f=nullptr;fopen_s(&f,Path("NativeProps/solids.bin").c_str(),"rb");
  if(f){unsigned magic=0,count=0;const BYTE solidGuard[]={0x8B,0x44,0x24,0x04,0x81,0x38,0,0x40,0x13,0x80};bool valid=!memcmp(reinterpret_cast<void*>(0x5075A0),solidGuard,sizeof(solidGuard))&&fread(&magic,4,1,f)==1&&fread(&count,4,1,f)==1&&magic==0x31535057&&count<512;
   for(unsigned i=0;valid&&i<count;i++){unsigned size=0;valid=fread(&size,4,1,f)==1&&size>=16&&size<8000000;if(!valid)break;auto bundle=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);valid=bundle&&fread(bundle,1,size,f)==size&&*static_cast<unsigned*>(bundle)==0x80134000&&static_cast<unsigned*>(bundle)[1]==size-8;if(!valid){if(bundle)VirtualFree(bundle,0,MEM_RELEASE);break;}if(!reinterpret_cast<int(__cdecl*)(void*)>(0x5075A0)(bundle)){valid=false;break;}propNativeSolidBundles.push_back(bundle);}
   valid=valid&&fgetc(f)==EOF;fclose(f);propNativeSolidsValid=valid&&count>0;Log("PROP_NATIVE_SOLIDS lists=%u valid=%u",unsigned(propNativeSolidBundles.size()),valid);
  }
  if(propPursuitPayload){FILE* f=nullptr;fopen_s(&f,Path("PursuitPayload/models.bun").c_str(),"rb");bool ok=f!=nullptr;if(f){fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);ok=size>=16&&size<64000000;if(ok){void* data=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);ok=data&&fread(data,1,size,f)==size&&*static_cast<unsigned*>(data)==0x80134000&&static_cast<unsigned*>(data)[1]==size-8;if(ok)ok=reinterpret_cast<int(__cdecl*)(void*)>(0x5075A0)(data)!=0;if(ok)propNativeSolidBundles.push_back(data);else if(data)VirtualFree(data,0,MEM_RELEASE);}fclose(f);}if(!ok){propNativeSolidsValid=false;Log("PURSUIT_PAYLOAD_REFUSED native model registration");return;}Log("PURSUIT_PAYLOAD_MODELS loaded=1 native_loader=005075A0");}
 }
 const BYTE modelGuard[]={0x8B,0x44,0x24,0x04,0x56,0x8B,0xF1,0x33,0xC9};
 if(memcmp(reinterpret_cast<void*>(0x506AF0),modelGuard,sizeof(modelGuard))){propHierarchies.clear();propHierarchyResolved=true;Log("PROP_HIERARCHY_REFUSED model constructor");return;}
 for(auto& pair:propHierarchies){auto tree=pair.second.data();unsigned count=*reinterpret_cast<unsigned*>(tree+4);for(unsigned n=0;n<count;n++){auto node=tree+8+n*16;unsigned solid=*reinterpret_cast<unsigned*>(node+4);auto& model=propHierarchyModels[solid];if(!model){model=std::make_unique<std::array<BYTE,24>>();model->fill(0);reinterpret_cast<void(__thiscall*)(void*,unsigned)>(0x506AF0)(model->data(),solid);}*reinterpret_cast<void**>(node+8)=model->data();}}
 propHierarchyResolved=true;Log("PROP_HIERARCHY_RESOLVED models=%u native_emodel=1",unsigned(propHierarchyModels.size()));
}
static void ReleasePropGeometry(NativeProp& p){if(p.gpu.vb)p.gpu.vb->Release();if(p.gpu.ib)p.gpu.ib->Release();resident-=p.gpu.bytes;p.gpu=Section{};p.cpu=CpuSection{};p.readComplete=false;}
static std::set<NativeProp*> propRetiredGeometry;
static void (__thiscall* propOriginalDestroy)(void*);
// MW destroys scenery on garage/safehouse transitions independently of our
// distance eviction. Observe the real destructor, including interface thunks.
// Keep ownership through the native destructor so its child/sim callbacks work.
static void __fastcall PropDestroyHook(void* spawner,void*){
 auto it=propModelOwners.find(static_cast<BYTE*>(spawner)+0x34);
 auto owner=it==propModelOwners.end()?nullptr:it->second;
 if(owner)PropObstacleOwnerGone(owner->id);
 propOriginalDestroy(spawner);
 if(!owner||owner->spawner!=spawner)return;
 propPayloadDrawMatrices.erase(static_cast<BYTE*>(spawner)+0x34);
 propModelOwners.erase(static_cast<BYTE*>(spawner)+0x34);
 for(auto piece=propPayloadPieces.begin();piece!=propPayloadPieces.end();){if(piece->second==owner){propPayloadDrawMatrices.erase(piece->first);piece=propPayloadPieces.erase(piece);}else ++piece;}
 for(auto child=propChildSims.begin();child!=propChildSims.end();){if(child->second==owner)child=propChildSims.erase(child);else ++child;}
 activeProps.erase(owner);owner->spawner=nullptr;owner->sim=nullptr;
 owner->initialized=false;owner->nativeTouched=false;
 propRetiredGeometry.insert(owner);
 Log("PROP_DESTROYED id=%u spawner=%p flow=%d remaining=%u respawn=1",owner->id,spawner,*reinterpret_cast<int*>(0x925E90),unsigned(activeProps.size()));
}
static bool ReadPropPack(const char* tail,CpuSection& out){
 FILE* f=nullptr;fopen_s(&f,Path(tail).c_str(),"rb");if(!f)return false;auto& h=out.header;
 bool ok=fread(&h,48,1,f)==1&&h.magic==Magic&&h.version==1&&h.meshes<1000&&h.vertices<1000000&&h.indices<3000000&&h.indices%3==0;
 if(ok){out.meshes.resize(h.meshes);out.vertices.resize(h.vertices);out.indices.resize(h.indices);ok=fread(out.meshes.data(),44,h.meshes,f)==h.meshes&&fread(out.vertices.data(),24,h.vertices,f)==h.vertices&&fread(out.indices.data(),4,h.indices,f)==h.indices&&fgetc(f)==EOF;}fclose(f);if(!ok)return false;
 for(auto& m:out.meshes){if(uint64_t(m.firstVertex)+m.vertexCount>h.vertices||uint64_t(m.firstIndex)+m.indexCount>h.indices||m.indexCount%3)return false;for(unsigned n=0;n<m.indexCount;n++)if(out.indices[m.firstIndex+n]>=m.vertexCount)return false;}PrepareMeshDrawLists(out);return true;
}
static bool PropMatrix(void* spawner,D3DMATRIX& matrix){__try{reinterpret_cast<void(__thiscall*)(void*,void*)>(0x66B4A0)(static_cast<BYTE*>(spawner)+0x34,&matrix);for(unsigned n=0;n<16;n++)if(!std::isfinite((&matrix._11)[n]))return false;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool PayloadPieceMatrix(void* model,D3DMATRIX& matrix){__try{auto vt=*reinterpret_cast<void***>(model);if(vt!=reinterpret_cast<void**>(0x8A9950))return PropMatrix(static_cast<BYTE*>(model)-0x34,matrix);reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[15])(model,&matrix);for(unsigned n=0;n<16;n++)if(!std::isfinite((&matrix._11)[n]))return false;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
struct PropHeader {unsigned type,name;void* data;unsigned pad;};
static void* (__cdecl* propOriginalFactory)(PropHeader);
static void (__thiscall* propOriginalKill)(void*);
static void* (__cdecl* propOriginalGraphicsFactory)(unsigned,void*,void*);
static void* __cdecl PropGraphicsFactoryHook(unsigned type,void* owner,void* params){
 if(propEffectsDiagnostics){
  auto result=propOriginalGraphicsFactory(type,owner,params);
  if(type==0x804C146E&&owner&&result){auto parent=*reinterpret_cast<void**>(static_cast<BYTE*>(owner)+0x4C);auto a=propModelOwners.find(parent),b=propModelOwners.find(static_cast<BYTE*>(owner)+0x34);if(a!=propModelOwners.end()||b!=propModelOwners.end()){auto p=a!=propModelOwners.end()?a->second:b->second;auto vt=*reinterpret_cast<void***>(result);Log("PROP_GRAPHICS_CREATED id=%u owner=%p result=%p vtable=%p slots=%p,%p,%p",p->id,owner,result,vt,vt[0],vt[1],vt[2]);}}
  return result;
 }
 if(type==0x804C146E&&owner){
  if(SourceNativePackets(propConstructing)){
   auto vt=*reinterpret_cast<unsigned*>(owner);
   if(vt==0x8A99F4||vt==0x8A9E7C){Log("PROP_RENDER_INITIAL_OWNED id=%u",propConstructing->id);return nullptr;}
  }
  auto parent=*reinterpret_cast<void**>(static_cast<BYTE*>(owner)+0x4C);
  auto it=propModelOwners.find(parent);
  if(it!=propModelOwners.end()){if(it->second->nativeRender)return propOriginalGraphicsFactory(type,owner,params);Log("PROP_RENDER_SLEEP_OWNED id=%u",it->second->id);return nullptr;}
  auto spawner=propModelOwners.find(static_cast<BYTE*>(owner)+0x34);
  if(spawner!=propModelOwners.end()){
   auto p=spawner->second;bool source=SourceNativePackets(p);
   // Intact source scenery already has a converted draw. A second native
   // static proxy races it at the same depth (and may bind stale textures).
   // Preserve native child proxies for animation and the root physics model.
   if(p->nativeRender&&!source)return propOriginalGraphicsFactory(type,owner,params);
   Log("PROP_RENDER_SPAWNER_OWNED id=%u source=%u",p->id,source);return nullptr;
  }
 }
 return propOriginalGraphicsFactory(type,owner,params);
}
#include "NeoPropObstacles.h"
static bool SimMatrix(void* sim,D3DMATRIX& matrix){__try{auto vt=*reinterpret_cast<void***>(sim);reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[25])(sim,&matrix);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool PursuitRollingRoot(const NativeProp* p){if(!p)return false;if(sourceRuntime){auto it=propPursuitParameters.find(*reinterpret_cast<const unsigned*>(p->record+32));return it!=propPursuitParameters.end()&&it->second.ends_with("_bigrollingtorus");}return *reinterpret_cast<const unsigned*>(p->record+40)==0xB5E1E552;}
static bool PursuitPayloadOwner(const NativeProp* p){return p&&p->nativeRender&&SourceNativePackets(p);}
static bool propPayloadSubmitting=false;
static unsigned propPayloadSubmitted=0;
static void (__cdecl* propOriginalDrawPacket)(void*,unsigned,unsigned,void*,void*,D3DMATRIX*,void*,void*,void*);
static void __cdecl PropPayloadDrawPacket(void* a,unsigned b,unsigned c,void* d,void* e,D3DMATRIX* f,void* g,void* h,void* i){if(propPayloadSubmitting)++propPayloadSubmitted;propOriginalDrawPacket(a,b,c,d,e,f,g,h,i);}
static void* (__thiscall* propOriginalPieceDestroy)(void*,unsigned);
static void* __fastcall PropPayloadPieceDestroy(void* self,void*,unsigned flags){
 auto model=static_cast<BYTE*>(self)+0x34;
 PropObstacleModelGone(model);propPayloadPieces.erase(model);propPayloadDrawMatrices.erase(model);
 return propOriginalPieceDestroy(self,flags);
}
static void SubmitPursuitPieces(void* view,unsigned flags){
 if(*reinterpret_cast<int*>(0x925E90)!=6)return;
 auto submit=[&](void* model,NativeProp* p){
  auto owner=static_cast<BYTE*>(model)-0x34;
  unsigned solid=*reinterpret_cast<unsigned*>(owner+0xCC);
  auto found=propHierarchyModels.find(solid);if(found==propHierarchyModels.end()){static std::set<unsigned> missing;if(missing.insert(solid).second)Log("PURSUIT_PAYLOAD_DRAW_MISSING solid=%08X",solid);return;}
  auto geo=*reinterpret_cast<BYTE**>(found->second->data()+12);if(geo){auto table=*reinterpret_cast<unsigned**>(geo+0x2C);static std::set<void*> initializedGeometry;bool ready=true,changed=!initializedGeometry.count(geo);for(unsigned i=0;i<geo[0x19];i++){auto info=reinterpret_cast<void*(__cdecl*)(unsigned,int,int)>(0x503400)(table[i*2],0,0);if(!info)ready=false;else if(unsigned(info)!=table[i*2+1])changed=true;}if(ready&&changed){reinterpret_cast<void(__thiscall*)(void*)>(0x5051D0)(geo);initializedGeometry.insert(geo);static std::set<unsigned> refreshed;if(refreshed.insert(solid).second)Log("PURSUIT_PAYLOAD_TEXTURE_READY solid=%08X",solid);}}
  static std::set<unsigned> textureLogged;if(textureLogged.insert(solid).second){auto geo=*reinterpret_cast<BYTE**>(found->second->data()+12);if(geo){unsigned count=geo[0x19];auto table=*reinterpret_cast<unsigned**>(geo+0x2C);for(unsigned i=0;i<count;i++){auto info=reinterpret_cast<BYTE*(__cdecl*)(unsigned,int,int)>(0x503400)(table[i*2],0,0);auto bound=reinterpret_cast<BYTE*>(table[i*2+1]);Log("PURSUIT_PAYLOAD_TEXTURE solid=%08X hash=%08X lookup=%p bound=%p bound_hash=%08X",solid,table[i*2],info,bound,bound?*reinterpret_cast<unsigned*>(bound+0x24):0);}}}
  // The native graphics proxy owns the final render transform, including
  // local model-axis conversion and the transform retained after sleep.
  auto& matrix=propPayloadDrawMatrices[model];
  if(model==static_cast<BYTE*>(p->spawner)+0x34){
   // Root smackables retain their static scenery proxy when the original
   // world update is suppressed. Follow the native rigid body instead,
   // including its final transform captured by the native sleep callback.
   if(p->sim&&!SimMatrix(p->sim,p->last))return;
   const unsigned axes[]={2,0,1};const float signs[]={1,-1,1};matrix={};
   for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)matrix.m[i][j]=signs[i]*signs[j]*p->last.m[axes[i]][axes[j]];
   // A native rigid body's origin is the compressed bound's centre. Source
   // vertices remain relative to the scenery pivot, not that centre.
   Vec3 pivot{p->last._41,p->last._42,p->last._43};
   auto bound=*reinterpret_cast<const short**>(static_cast<BYTE*>(p->spawner)+0x58);
   // MW GetMatrix uses the bound centre even before the rigid body is created.
   // Convert both intact and moving bodies back to the source scenery pivot.
   if(bound)for(unsigned j=0;j<3;j++)for(unsigned i=0;i<3;i++)(&pivot.x)[j]-=float(bound[12+i])*.001f*p->last.m[i][j];
   matrix._41=pivot.z;matrix._42=-pivot.x;matrix._43=pivot.y;matrix._44=1;
  }else{
   auto proxy=*reinterpret_cast<BYTE**>(owner+0x80);
   if(!proxy||*reinterpret_cast<unsigned*>(proxy)!=0x8B4B7C)return;
   memcpy(&matrix,proxy+0x50,sizeof(matrix));
  }
  for(unsigned i=0;i<16;i++)if(!std::isfinite((&matrix._11)[i]))return;
  unsigned nativeFlags=0;if(flags&1)nativeFlags|=0x1000;if(flags&0x80)nativeFlags|=0x100;
#ifdef MW_PROP_SHADER_AUDIT
  ObserveMwPropMatrix(p->id,&matrix);
#endif
  propPayloadSubmitting=true;propPayloadSubmitted=0;
  reinterpret_cast<void(__thiscall*)(void*,void*,D3DMATRIX*,unsigned,unsigned,unsigned)>(0x6DA9B0)(view,found->second->data(),&matrix,0,nativeFlags,0);
  propPayloadSubmitting=false;
  static DWORD tireLog=0;if(propPursuitDiagnostics&&p->id==82&&GetTickCount()-tireLog>100){tireLog=GetTickCount();Log("PURSUIT_PAYLOAD_TIRE_DRAW sim=%p at=%.3f,%.3f,%.3f packets=%u up=%.3f,%.3f,%.3f",p->sim,matrix._41,matrix._42,matrix._43,propPayloadSubmitted,matrix._31,matrix._32,matrix._33);}
  static std::set<void*> logged;if(propPursuitDiagnostics&&logged.insert(model).second)Log("PURSUIT_PAYLOAD_DRAW id=%u solid=%08X model=%p emodel=%p geometry=%p packets=%u native_submit=006DA9B0 at=%.3f,%.3f,%.3f",p->id,solid,model,found->second->data(),*reinterpret_cast<void**>(found->second->data()+12),propPayloadSubmitted,matrix._41,matrix._42,matrix._43);
 };
 for(auto& entry:propPayloadPieces)submit(entry.first,entry.second);
 static DWORD last=0;if(propPursuitDiagnostics&&GetTickCount()-last>1000){last=GetTickCount();Log("PURSUIT_PAYLOAD_DRAW_PIECES count=%u",unsigned(propPayloadPieces.size()));}
 for(auto p:activeProps)if(PursuitPayloadOwner(p)&&(!p->nativeFractured||PursuitRollingRoot(p))&&p->initialized&&p->spawner)submit(static_cast<BYTE*>(p->spawner)+0x34,p);
}
static bool __fastcall PropRenderHidden(void*,void*){return false;}
static void* __fastcall PropRenderHandle(void*,void*){return nullptr;}
static void HideNativePropRendering(NativeProp& p,void* sim){
 if(propEffectsDiagnostics||(p.nativeRender&&!PursuitPayloadOwner(&p)))return;
 // Verified MW Smackable: ISimable at +0x2c, IRenderable at +0xb4.
 // Keep GetModel and all collision/physics interfaces intact. Only this owned
 // instance uses our converted renderer; native missing-solid boxes must not draw.
 auto render=reinterpret_cast<void***>(static_cast<BYTE*>(sim)+0x88);
 if(*render!=reinterpret_cast<void**>(0x8AA5D8)){Log("PROP_RENDER_REFUSED id=%u vtable=%p",p.id,*render);return;}
 memcpy(p.renderVtable,*render,sizeof(p.renderVtable));
 p.renderVtable[1]=reinterpret_cast<void*>(PropRenderHidden);
 p.renderVtable[2]=reinterpret_cast<void*>(PropRenderHidden);
 p.renderVtable[3]=reinterpret_cast<void*>(PropRenderHandle);
 *render=p.renderVtable;Log("PROP_RENDER_OWNED id=%u",p.id);
}
static void* __cdecl PropFactoryHook(PropHeader h){auto params=static_cast<PropParams*>(h.data);NativeProp* selected=nullptr;NativeProp* childOwner=nullptr;if(h.name==0xA6B47FAC&&params){auto it=propModelOwners.find(params->model);if(it!=propModelOwners.end())selected=it->second;else if(params->model){auto parent=*reinterpret_cast<void**>(static_cast<BYTE*>(params->model)+0x18);it=propModelOwners.find(parent);if(it!=propModelOwners.end())childOwner=it->second;}}auto previous=propFactorySelected;propFactorySelected=selected?selected:childOwner;auto result=propOriginalFactory(h);propFactorySelected=previous;if(childOwner&&result){childOwner->nativeFractured=true;auto modelVt=*reinterpret_cast<void***>(params->model);auto modelOwner=static_cast<BYTE*>(params->model)-0x34;unsigned modelHash=0;if(modelVt==reinterpret_cast<void**>(0x8A9950)&&(*reinterpret_cast<unsigned*>(modelOwner)==0x8A99F4||*reinterpret_cast<unsigned*>(modelOwner)==0x8A9E7C))modelHash=*reinterpret_cast<unsigned*>(modelOwner+0xCC);Log("PROP_CHILD_MODEL id=%u solid=%08X model=%p vt=%p pos=%.3f,%.3f,%.3f",childOwner->id,modelHash,params->model,modelVt,params->matrix._41,params->matrix._42,params->matrix._43);propChildSims[result]=childOwner;PropObstacleAwake(result,childOwner,params->model);if(PursuitPayloadOwner(childOwner)){propPayloadPieces[params->model]=childOwner;HideNativePropRendering(*childOwner,result);}if(childOwner->nativeRender&&!childOwner->nativeTouched){childOwner->nativeTouched=true;Log("PROP_NATIVE_BREAKUP_RENDER id=%u",childOwner->id);}Log("PROP_CHILD_CONTACT id=%u sim=%p model=%p",childOwner->id,result,params->model);}if(selected){selected->sim=result;if(result){selected->nativeTouched=true;HideNativePropRendering(*selected,result);SimMatrix(result,selected->last);if(params->virgin){selected->initial=selected->last;Log("PROP_BASE id=%u pos=%.4f,%.4f,%.4f rotation=%.3f,%.3f,%.3f",selected->id,selected->initial._41,selected->initial._42,selected->initial._43,selected->initial._11,selected->initial._22,selected->initial._33);}}Log("PROP_FACTORY contact id=%u sim=%p model=%p virgin=%u",selected->id,result,params->model,unsigned(params->virgin));}return result;}
static void __fastcall PropKillHook(void* sim,void*){auto child=propChildSims.find(sim);if(child!=propChildSims.end()){PropObstacleSleeping(sim,child->second);D3DMATRIX m{};SimMatrix(sim,m);Log("PROP_CHILD_SLEEP id=%u sim=%p at=%.3f,%.3f,%.3f",child->second->id,sim,m._41,m._42,m._43);propChildSims.erase(child);}for(auto p:activeProps)if(p->sim==sim){SimMatrix(sim,p->last);p->sim=nullptr;Log("PROP_FACTORY sleeping id=%u at=%.3f,%.3f,%.3f up=%.3f,%.3f,%.3f",p->id,p->last._41,p->last._42,p->last._43,p->last._21,p->last._22,p->last._23);}propOriginalKill(sim);}
static void InitNativeProps(){
#ifdef MW_PROP_SHADER_AUDIT
 InstallMwPropShaderAudit();
#endif
 const BYTE growGuard[]={0x56,0x8B,0xF1,0x8B,0x86,0x04,0x01,0,0};auto grow=reinterpret_cast<void*>(0x5CA4F0);
 if(!memcmp(grow,growGuard,sizeof(growGuard))&&MH_CreateHook(grow,PropFastMemGrow,reinterpret_cast<void**>(&propOriginalFastMemGrow))==MH_OK&&MH_EnableHook(grow)==MH_OK)Log("PROP_FASTMEM_GROW_ACTIVE native_arena=00925B30 native_growth=005CA4F0");
 if(!sourceRuntime&&!GetPrivateProfileIntA("Probe","NativePropPilot",0,Path("W2CMWv2Bootstrap.ini").c_str())&&!GetPrivateProfileIntA("Probe","NativeProps",0,Path("W2CMWv2Bootstrap.ini").c_str()))return;
#ifdef W2C_PUBLIC_BUILD
 propPursuitDiagnostics=worldLogEnabled&&WorldDiagnosticFlag(Path("W2CMWv2.ini").c_str(),"Debug","PursuitDiagnostics");
#else
 propEffectsDiagnostics=WorldDiagnosticFlag(Path("W2CMWv2Bootstrap.ini").c_str(),"Probe","PropEffectsDiagnostics");
 propPursuitDiagnostics=WorldDiagnosticFlag(Path("W2CMWv2Bootstrap.ini").c_str(),"Probe","PursuitDiagnostics");
#endif
 LoadPropHierarchies();
 if(sourceRuntime&&!propHierarchyEnabled){W2V2Status.error=1;return;}
 propUtilityBridge=propPursuitPayload&&(sourceRuntime||GetPrivateProfileIntA("Probe","PursuitUtilityBridge",0,Path("W2CMWv2Bootstrap.ini").c_str())!=0);
 if(propUtilityBridge){
  const BYTE queryGuard[]={0x6A,0xFF,0x68,0x98,0x1F,0x88,0,0x64,0xA1};
  const BYTE testGuard[]={0x81,0xEC,0x28,0x02,0,0,0x53,0x56};
  auto query=reinterpret_cast<void*>(0x781840),test=reinterpret_cast<void*>(0x7669F0);
  if(memcmp(query,queryGuard,sizeof(queryGuard))||memcmp(test,testGuard,sizeof(testGuard))||MH_CreateHook(query,PropUtilityQuery,reinterpret_cast<void**>(&propOriginalUtilityQuery))!=MH_OK||MH_CreateHook(test,PropUtilityTest,reinterpret_cast<void**>(&propOriginalUtilityTest))!=MH_OK||MH_EnableHook(test)!=MH_OK||MH_EnableHook(query)!=MH_OK){MH_DisableHook(query);MH_RemoveHook(query);MH_DisableHook(test);MH_RemoveHook(test);propUtilityBridge=false;Log("PURSUIT_NATIVE_UTILITY_REFUSED hooks");}
  else Log("PURSUIT_NATIVE_UTILITY_ACTIVE query=00781840 narrow_phase=007669F0 dispatch=007817B0 source_bounds=1");
 }
 if(propPursuitDiagnostics){
  const BYTE hullGuard[]={0x83,0xEC,0x1C,0x56,0x8B,0x74,0x24,0x2C};auto hull=reinterpret_cast<void*>(0x76EED0);
  if(!memcmp(hull,hullGuard,sizeof(hullGuard))&&MH_CreateHook(hull,PropHullSelection,reinterpret_cast<void**>(&propOriginalHullSelection))==MH_OK&&MH_EnableHook(hull)==MH_OK)Log("PROP_CONTACT_HULL_TRACE_ACTIVE");
  const BYTE velocityGuard[]={0x83,0xE9,0x60,0xE9};auto velocity=reinterpret_cast<void*>(0x6726B0);
  if(!memcmp(velocity,velocityGuard,sizeof(velocityGuard))&&MH_CreateHook(velocity,PropBoundVelocity,reinterpret_cast<void**>(&propOriginalBoundVelocity))==MH_OK&&MH_EnableHook(velocity)==MH_OK)Log("PROP_CONTACT_PLANAR_TRACE_ACTIVE");
 }
 if(propPursuitDiagnostics){const BYTE guard[]={0x8B,0x81,0x84,0,0,0,0x83,0xEC,0x18};auto target=reinterpret_cast<void*>(0x775A20);if(!memcmp(target,guard,sizeof(guard))&&MH_CreateHook(target,PropContactCandidates,reinterpret_cast<void**>(&propOriginalContactCandidates))==MH_OK&&MH_EnableHook(target)==MH_OK)Log("PROP_CONTACT_CANDIDATE_TRACE_ACTIVE");}
 if(propPursuitDiagnostics){auto target=reinterpret_cast<void*>(0x6E0AB0);const BYTE guard[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0};if(!memcmp(target,guard,sizeof(guard))&&MH_CreateHook(target,PropPayloadDrawPacket,reinterpret_cast<void**>(&propOriginalDrawPacket))==MH_OK)MH_EnableHook(target);}
 {const BYTE guard[]={0x56,0x8B,0xF1,0xE8};auto target=reinterpret_cast<void*>(0x67F700);if(memcmp(target,guard,sizeof(guard))||MH_CreateHook(target,PropPayloadPieceDestroy,reinterpret_cast<void**>(&propOriginalPieceDestroy))!=MH_OK||MH_EnableHook(target)!=MH_OK){Log("PURSUIT_PAYLOAD_REFUSED piece lifetime hook");return;}Log("PURSUIT_PAYLOAD_DRAW_ACTIVE native_submit=006DA9B0 native_transform=1");}
 if(propHierarchyEnabled){const BYTE guard[]={0x6A,0xFF,0x68,0x02,0xA9,0x87,0,0x64,0xA1};auto target=reinterpret_cast<void*>(0x6722C0);if(memcmp(target,guard,sizeof(guard))||MH_CreateHook(target,PropConstructHook,reinterpret_cast<void**>(&propOriginalConstruct))!=MH_OK||MH_EnableHook(target)!=MH_OK){Log("PROP_REFUSED model identity constructor");return;}Log("PROP_MODEL_IDENTITY_ACTIVE constructor=006722C0");}
 if(sourceRuntime||propEffectsDiagnostics||propPursuitDiagnostics||GetPrivateProfileIntA("Probe","CarbonBreakerAllFamilies",0,Path("W2CMWv2Bootstrap.ini").c_str())==1||GetPrivateProfileIntA("World","AllFamilies",0,Path("World2MWCarbonBreakers.ini").c_str())==1){const BYTE eventGuard[]={0x51,0x56,0x8B,0xF1,0x8B,0x46,0x04,0x85,0xC0};auto event=reinterpret_cast<void*>(0x645A40);if(!memcmp(event,eventGuard,sizeof(eventGuard))&&MH_CreateHook(event,PropEventHook,reinterpret_cast<void**>(&propOriginalEvent))==MH_OK)MH_EnableHook(event);}
 if(propEffectsDiagnostics){
 const BYTE worldGuard[]={0x6A,0xFF,0x68,0x40,0xC2,0x87,0,0x64,0xA1};auto world=reinterpret_cast<void*>(0x699B50);if(!memcmp(world,worldGuard,sizeof(worldGuard))&&MH_CreateHook(world,PropWorldContactTrace,reinterpret_cast<void**>(&propOriginalWorldContact))==MH_OK)MH_EnableHook(world);}
 if(!sourceRuntime){FILE* matFile=nullptr;fopen_s(&matFile,Path("NativeProps/materials.txt").c_str(),"r");if(matFile){unsigned hash;Material mat;while(fscanf_s(matFile,"%x %u %u %u %u",&hash,&mat.usage,&mat.blend,&mat.flags,&mat.tile)==5)materials[hash]=mat;fclose(matFile);}}
 const BYTE factoryGuard[]={0x64,0xA1,0,0,0,0,0x6A,0xFF},killGuard[]={0x56,0x8B,0xF1,0x8B,0x86,0xE0,0,0,0};if(memcmp(reinterpret_cast<void*>(0x6895A0),factoryGuard,sizeof(factoryGuard))||memcmp(reinterpret_cast<void*>(0x67B650),killGuard,sizeof(killGuard))){Log("PROP_REFUSED byte guards");return;}
 auto factory=reinterpret_cast<void*>(0x6895A0),kill=reinterpret_cast<void*>(0x67B650),graphics=reinterpret_cast<void*>(0x6F2540);auto destroy=reinterpret_cast<void*>(0x67BE90);auto rollback=[&](){for(auto target:{factory,kill,graphics,destroy,reinterpret_cast<void*>(0x6722C0)}){MH_DisableHook(target);MH_RemoveHook(target);}};if(MH_CreateHook(factory,PropFactoryHook,reinterpret_cast<void**>(&propOriginalFactory))!=MH_OK||MH_CreateHook(kill,PropKillHook,reinterpret_cast<void**>(&propOriginalKill))!=MH_OK||MH_EnableHook(factory)!=MH_OK||MH_EnableHook(kill)!=MH_OK){rollback();Log("PROP_REFUSED factory hooks");return;}
 const BYTE destroyGuard[]={0x6A,0xFF,0x68,0xF1,0xAD,0x87,0,0x64,0xA1};
 if(memcmp(destroy,destroyGuard,sizeof(destroyGuard))||MH_CreateHook(destroy,PropDestroyHook,reinterpret_cast<void**>(&propOriginalDestroy))!=MH_OK||MH_EnableHook(destroy)!=MH_OK){rollback();Log("PROP_REFUSED lifecycle hook");return;}
 Log("PROP_LIFECYCLE_ACTIVE destructor=0067BE90");
 const BYTE graphicsGuard[]={0x83,0xEC,0x0C,0x8B,0x54,0x24,0x10};
 if(propNativeReference){const BYTE spawnerGuard[]={0x64,0xA1,0,0,0,0};auto spawner=reinterpret_cast<void*>(0x681540);if(memcmp(spawner,spawnerGuard,sizeof(spawnerGuard))||MH_CreateHook(spawner,PropReferenceSpawner,reinterpret_cast<void**>(&propOriginalSpawner))!=MH_OK||MH_EnableHook(spawner)!=MH_OK){Log("PROP_REFERENCE_REFUSED spawner");return;}Log("PROP_REFERENCE_ACTIVE native_world=1");return;}
 if(memcmp(graphics,graphicsGuard,sizeof(graphicsGuard))||MH_CreateHook(graphics,PropGraphicsFactoryHook,reinterpret_cast<void**>(&propOriginalGraphicsFactory))!=MH_OK||MH_EnableHook(graphics)!=MH_OK){rollback();Log("PROP_REFUSED graphics ownership hook");return;}
 if(sourceRuntime){if(!LoadSourcePropCatalog()){rollback();W2V2Status.error=1;}return;}
 FILE* catalog=nullptr;fopen_s(&catalog,Path("NativeProps/catalog.bin").c_str(),"rb");if(catalog){unsigned magic=0,count=0;bool valid=fread(&magic,4,1,catalog)==1&&fread(&count,4,1,catalog)==1&&magic==0x31504D57&&count>0&&count<100000;for(unsigned i=0;valid&&i<count;i++){auto p=std::make_unique<NativeProp>();p->id=i;valid=fread(p->record,64,1,catalog)==1;for(unsigned n=0;n<3;n++)if(!std::isfinite(*reinterpret_cast<float*>(p->record+16+4*n)))valid=false;if(valid)nativeProps.push_back(std::move(p));}valid=valid&&fgetc(catalog)==EOF;fclose(catalog);if(!valid){nativeProps.clear();rollback();Log("PROP_REFUSED catalog");return;}propCatalog=true;w2mw::propPolygonBridge=true;Log("PROP_CATALOG loaded=%u native_physics=1",unsigned(nativeProps.size()));return;}
 auto p=std::make_unique<NativeProp>();FILE* f=nullptr;fopen_s(&f,Path("NativeProps/0.record").c_str(),"rb");if(!f){rollback();return;}bool ok=fread(p->record,64,1,f)==1&&fgetc(f)==EOF;fclose(f);
 CpuSection ghost;if(!ok||!ReadPropPack("NativeProps/0.wmw",p->cpu)||!ReadPropPack("NativeProps/0.ghost.wmw",ghost)){rollback();Log("PROP_REFUSED pilot pack");return;}
 for(auto& m:ghost.meshes){auto v=ghost.vertices.data()+m.firstVertex;auto ix=ghost.indices.data()+m.firstIndex;for(unsigned n=0;n<m.indexCount;n+=3)propGhostTriangles.insert(TriangleKey(m.texture,v[ix[n]].position,v[ix[n+1]].position,v[ix[n+2]].position));}
 w2mw::propPolygonBridge=true;nativeProps.push_back(std::move(p));Log("PROP_PACK loaded=%u ghost_triangles=%u",unsigned(nativeProps.size()),unsigned(propGhostTriangles.size()));
}
static float PropDistance(const NativeProp& p){float dx=cameraPosition.x-*reinterpret_cast<const float*>(p.record+24),dy=cameraPosition.y+*reinterpret_cast<const float*>(p.record+16);return dx*dx+dy*dy;}
static void TickNativeProps(){propObstacleThread=GetCurrentThreadId();
#ifdef W2C_PUBLIC_BUILD
 if(!EnsureSourcePublicPursuitVault())return;
#endif
 // Worker owns disk reads/validation. D3D buffers and native spawners remain
 // on the scene thread; completed reads are bounded to four CPU packs.
 {std::lock_guard lock(ioMutex);while(!propReadReady.empty()){auto item=std::move(propReadReady.front());propReadReady.pop_front();if(item.id>=nativeProps.size())continue;auto& p=*nativeProps[item.id];if(item.epoch!=V2Epoch()){if(p.queuedEpoch==item.epoch)p.readQueued=false;++v2StaleDrops;continue;}p.readQueued=false;if(item.data){p.cpu=std::move(*item.data);p.readComplete=true;}else{p.refused=true;Log("PROP_REFUSED model id=%u",p.id);}}}ioSignal.notify_all();
 for(auto p:propRetiredGeometry)ReleasePropGeometry(*p);propRetiredGeometry.clear();RegisterPropCollision();ResolvePropHierarchies();static DWORD lastScan=0;DWORD now=GetTickCount();if(now-lastScan>100){lastScan=now;propSpawnQueue.clear();for(auto& p:nativeProps){if(p->spawner||p->refused)continue;float distance=PropDistance(*p);
 if(distance>300.f*300.f&&p->readComplete&&!p->gpu.vb){p->cpu=CpuSection{};p->readComplete=false;}
 if(!sourceRuntime&&propCatalog&&!p->gpu.vb&&!p->readComplete&&!p->readQueued&&distance<220.f*220.f){std::lock_guard lock(ioMutex);if(propReadPending.size()<16&&V2RequestRoom()){p->readQueued=true;p->queuedEpoch=V2Epoch();propReadPending.push_back(p->id);}}
 if(distance<180.f*180.f)propSpawnQueue.push_back(p.get());}ioSignal.notify_all();}
 for(auto it=activeProps.begin();it!=activeProps.end();){auto& p=**it;if(PropDistance(p)<230.f*230.f){++it;continue;}if(p.sim)PropKillHook(p.sim,nullptr);p.sim=nullptr;auto vt=*reinterpret_cast<void***>(p.spawner);++it;reinterpret_cast<void(__thiscall*)(void*,unsigned)>(vt[0])(p.spawner,1);}
 unsigned added=0;while(!propSpawnQueue.empty()&&added<(initialWorldHold?16u:2u)&&sceneUploadBudget.Available(Clock())&&activeProps.size()<512){auto& p=*propSpawnQueue.front();propSpawnQueue.pop_front();if(p.spawner||p.refused||PropDistance(p)>180.f*180.f)continue;
 auto hierarchy=propHierarchies.find(*reinterpret_cast<unsigned*>(p.record+32));p.nativeRender=propNativeSolidsValid&&hierarchy!=propHierarchies.end()&&reinterpret_cast<void*(__cdecl*)(unsigned,void*)>(0x5018F0)(*reinterpret_cast<unsigned*>(hierarchy->second.data()),nullptr)!=nullptr;
 if(sourceRuntime){if(!p.nativeRender||!SourceNativePackets(&p))continue;}else if(!p.gpu.vb){if(propCatalog&&!p.readComplete)continue;if(!Upload(p.cpu,p.gpu))continue;p.cpu=CpuSection{};}
 auto manager=*reinterpret_cast<void**>(0x926180);if(!manager)continue;unsigned param=*reinterpret_cast<unsigned*>(p.record+40);
 auto adapted=propPursuitParameters.find(*reinterpret_cast<unsigned*>(p.record+32));if(propPursuitPayload&&adapted!=propPursuitParameters.end()){auto hash=reinterpret_cast<unsigned(__cdecl*)(const char*)>(0x5CC240);unsigned replacement=hash(adapted->second.c_str());if(!reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(0x455960)(manager,replacement)){Log("PURSUIT_PAYLOAD_REFUSED parameter id=%u",p.id);p.refused=true;continue;}Log("PURSUIT_PAYLOAD_PARAMETER id=%u original=%08X adapted=%08X",p.id,param,replacement);param=replacement;}
 auto resource=reinterpret_cast<void*(__thiscall*)(void*,unsigned)>(0x455960)(manager,param);if(!resource)continue;
 propConstructing=&p;p.spawner=reinterpret_cast<void*(__cdecl*)(void*,void*,unsigned)>(0x681540)(p.record,resource,0);propConstructing=nullptr;if(!p.spawner){p.refused=true;ReleasePropGeometry(p);Log("PROP_REFUSED native spawner id=%u asset=%08X",p.id,*reinterpret_cast<unsigned*>(p.record+32));continue;}
 if(hierarchy!=propHierarchies.end()){auto tree=hierarchy->second.data();auto owner=static_cast<BYTE*>(p.spawner);*reinterpret_cast<void**>(owner+0xC8)=tree;*reinterpret_cast<unsigned short*>(owner+0xD8)=0;*reinterpret_cast<unsigned*>(owner+0xCC)=*reinterpret_cast<unsigned*>(tree);auto proxy=*reinterpret_cast<BYTE**>(owner+0x80);if(proxy&&*reinterpret_cast<unsigned*>(proxy)==0x8B4B7C){*reinterpret_cast<unsigned*>(proxy+0x18)=*reinterpret_cast<unsigned*>(tree);*reinterpret_cast<void**>(proxy+0x1C)=tree;}Log("PROP_HIERARCHY id=%u nodes=%u native_render=%u",p.id,*reinterpret_cast<unsigned*>(tree+4),p.nativeRender);}
 // Single-model source props already have an intact converted draw. Release
 // their competing scenery proxy through MW; retain multi-piece proxies that
 // provide native destruction animation transforms.
 bool sourceSingle=!p.authored&&PursuitPayloadOwner(&p)&&hierarchy!=propHierarchies.end()&&*reinterpret_cast<unsigned*>(hierarchy->second.data()+4)==1;
 if(!propEffectsDiagnostics&&(!p.nativeRender||sourceSingle)){reinterpret_cast<void(__thiscall*)(void*)>(0x6EEC20)(p.spawner);if(sourceSingle)Log("PROP_RENDER_INITIAL_RELEASED id=%u source_single=1",p.id);}
 propModelOwners[static_cast<BYTE*>(p.spawner)+0x34]=&p;
 PropObstacleOwnerGone(p.id);p.nativeTouched=false;p.nativeFractured=false;
 TracePropProxy(p);p.initialized=PropMatrix(p.spawner,p.initial);
 p.last=p.initial;activeProps.insert(&p);propModelOwners[static_cast<BYTE*>(p.spawner)+0x34]=&p;++added;Log("PROP_NATIVE id=%u spawner=%p asset=%08X matrix=%u",p.id,p.spawner,*reinterpret_cast<unsigned*>(p.record+32),p.initialized);
 if(worldLogEnabled&&p.rockportCushion){
  p.cushionMotionLogged=false;
  Log("ROCKPORT_CUSHION_SPAWNED id=%u at=%.4f,%.4f,%.4f",p.id,p.initial._41,p.initial._42,p.initial._43);
 }
 if(propPursuitDiagnostics)Log("PROP_SEQUENCE_READY id=%u sequence=%p context=%p solid=%08X source_scenery=%08X hierarchy=%p",p.id,*reinterpret_cast<void**>(static_cast<BYTE*>(p.spawner)+0x6C),static_cast<BYTE*>(p.spawner)+0x40,*reinterpret_cast<unsigned*>(static_cast<BYTE*>(p.spawner)+0xCC),*reinterpret_cast<unsigned*>(p.record+44),*reinterpret_cast<void**>(static_cast<BYTE*>(p.spawner)+0xC8));
}TickCarbonBreakerPrototype();}
static D3DMATRIX PropDelta(const D3DMATRIX& initial,const D3DMATRIX& current){
 auto transform=[&](Vec3 r){float phys[3]={-r.y,r.z,r.x},q[3]={0,0,0},out[3]={current._41,current._42,current._43};for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)q[i]+=(phys[j]-initial.m[3][j])*initial.m[i][j];for(unsigned j=0;j<3;j++)for(unsigned i=0;i<3;i++)out[j]+=q[i]*current.m[i][j];return Vec3{out[2],-out[0],out[1]};};
 Vec3 t=transform({0,0,0});D3DMATRIX result{};for(unsigned i=0;i<3;i++){Vec3 v{};(&v.x)[i]=1;v=Sub(transform(v),t);result.m[i][0]=v.x;result.m[i][1]=v.y;result.m[i][2]=v.z;}result._41=t.x;result._42=t.y;result._43=t.z;result._44=1;return result;
}
static void TraceRockportCushion(NativeProp& prop,const D3DMATRIX& current){
 if(!worldLogEnabled||(!prop.rockportCushion&&!prop.authored)||prop.cushionMotionLogged)return;
 const float x=current._41-prop.initial._41, y=current._42-prop.initial._42,
             z=current._43-prop.initial._43;
 const float travel=prop.authored?x*x+z*z:x*x+y*y+z*z;
 if(travel<=.64f)return;
 prop.cushionMotionLogged=true;
 Log(prop.authored?"AUTHORED_PROP_MOVED id=%u metres=%.3f at=%.4f,%.4f,%.4f":"ROCKPORT_CUSHION_MOVED id=%u metres=%.3f at=%.4f,%.4f,%.4f",
     prop.id,std::sqrt(travel),current._41,current._42,current._43);
}
static void TraceNativePropChildren(){static DWORD last=0;DWORD now=GetTickCount();if((!propEffectsDiagnostics&&!propPursuitDiagnostics)||now-last<100)return;last=now;for(auto it=propChildSims.begin();it!=propChildSims.end();){auto sim=it->first;auto p=it->second;if(*reinterpret_cast<unsigned*>(sim)!=0x8AA638){Log("PROP_CHILD_RELEASED id=%u sim=%p vtable=%p",p->id,sim,*reinterpret_cast<void**>(sim));it=propChildSims.erase(it);continue;}D3DMATRIX m{};SimMatrix(sim,m);Vec3 v{};auto vt=*reinterpret_cast<void***>(sim);reinterpret_cast<void(__thiscall*)(void*,void*)>(vt[26])(sim,&v);Log("PROP_CHILD_MOTION id=%u sim=%p at=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f",p->id,sim,m._41,m._42,m._43,v.x,v.y,v.z);++it;}}
static void DrawNativeProps(const D3DMATRIX& clip){propObstacleThread=GetCurrentThreadId();const RenderFrustum frustum(clip);if(!envDepthPass&&envAvailable)BindEnvironmentPropColour();TraceNativePropChildren();static DWORD last=0;for(auto ptr:activeProps){auto& p=*ptr;if(!p.spawner||!p.initialized)continue;if(p.sim)SimMatrix(p.sim,p.last);D3DMATRIX current=p.last;if(worldLogEnabled && p.sim && (p.rockportCushion||p.authored))TraceRockportCushion(p,current);if(!PursuitPayloadOwner(&p)&&(!p.nativeRender||!p.nativeTouched)&&p.gpu.vb){auto delta=PropDelta(p.initial,current);Vec3 low{},high{};TransformRenderBounds(p.gpu.header.minimum,p.gpu.header.maximum,delta,low,high);if(!frustum.Visible(low,high))continue;device->SetTransform(D3DTS_WORLD,&delta);if(envDepthPass)SetEnvironmentPropDepth(delta);device->SetStreamSource(0,p.gpu.vb,0,sizeof(Vertex));device->SetIndices(p.gpu.ib);for(auto& m:p.gpu.meshes){if(envDepthPass&&MaterialOf(m.texture).blend)continue;if(m.texture&&!Texture(m.texture))continue;SetMaterial(m.texture);device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,m.firstVertex,0,m.vertexCount,m.firstIndex,m.indexCount/3);}}if(p.sim&&GetTickCount()-last>500){last=GetTickCount();TracePropPhysics(p);Log("PROP_TRANSFORM id=%u position=%.3f,%.3f,%.3f up=%.3f,%.3f,%.3f flags=%u",p.id,current._41,current._42,current._43,current._21,current._22,current._23,unsigned(static_cast<BYTE*>(p.spawner)[0xEC]));}}}
