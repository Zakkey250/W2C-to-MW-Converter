#pragma once
// Streaming keeps both source copies. Suppress a duplicate only while the
// section owning the same exact material/position/UV face can actually draw.
static std::vector<std::vector<unsigned>> overlapGroups;
static void LoadOverlapGroups(){
 FILE* f=nullptr;fopen_s(&f,Path("NativeWorld/Overlap/groups.bin").c_str(),"rb");if(!f)return;
 unsigned h[3]{};bool ok=fread(h,4,3,f)==3&&h[0]==0x31505247&&h[1]==1&&h[2]<100000;
 std::vector<std::vector<unsigned>> groups(ok?h[2]+1:0);
 for(unsigned n=0;ok&&n<h[2];++n){unsigned r[2]{};ok=fread(r,4,2,f)==2&&r[0]&&r[0]<groups.size()&&r[1]>=2&&r[1]<2171;if(!ok)break;auto& members=groups[r[0]];if(!members.empty()){ok=false;break;}members.resize(r[1]);ok=fread(members.data(),4,r[1],f)==r[1]&&std::is_sorted(members.begin(),members.end());}
 ok=ok&&fgetc(f)==EOF;fclose(f);if(ok){overlapGroups=std::move(groups);Log("OVERLAP_GROUPS groups=%u",h[2]);}else Log("OVERLAP_INVALID groups=1");
}
static void ReadOverlapOwnership(CpuSection& data){
 if(overlapGroups.empty())return;char name[100];sprintf_s(name,"NativeWorld/Overlap/%u.bin",data.header.section);FILE* f=nullptr;fopen_s(&f,Path(name).c_str(),"rb");if(!f)return;
 unsigned h[3]{};BYTE expected[32]{},actual[32]{};bool ok=fread(h,4,3,f)==3&&h[0]==0x31504C4F&&h[1]==1&&h[2]<=data.header.indices/3&&fread(expected,1,32,f)==32;
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;DWORD size=32;
 ok=ok&&CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash);
 if(ok)ok=CryptHashData(hash,reinterpret_cast<const BYTE*>(&data.header),sizeof(Header),0)&&CryptHashData(hash,reinterpret_cast<const BYTE*>(data.meshes.data()),DWORD(data.meshes.size()*sizeof(Mesh)),0)&&CryptHashData(hash,reinterpret_cast<const BYTE*>(data.vertices.data()),DWORD(data.vertices.size()*sizeof(Vertex)),0)&&CryptHashData(hash,reinterpret_cast<const BYTE*>(data.indices.data()),DWORD(data.indices.size()*4),0)&&CryptGetHashParam(hash,HP_HASHVAL,actual,&size,0)&&!memcmp(expected,actual,32);
 if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
 std::vector<unsigned> owners(ok?data.header.indices/3:0,0);unsigned previous=~0u;
 for(unsigned n=0;ok&&n<h[2];++n){unsigned r[2]{};ok=fread(r,4,2,f)==2&&r[0]%3==0&&r[0]<data.header.indices&&(n==0||r[0]>previous)&&(r[1]==~0u||(r[1]&&r[1]<overlapGroups.size()));if(!ok)break;previous=r[0];if(r[1]!=~0u){const auto& members=overlapGroups[r[1]];ok=std::binary_search(members.begin(),members.end(),data.header.section);if(!ok)break;}owners[r[0]/3]=r[1];}
 ok=ok&&fgetc(f)==EOF;fclose(f);if(ok){data.triangleOverlap=std::move(owners);Log("OVERLAP_SECTION section=%u source_verified=1 records=%u",data.header.section,h[2]);}else Log("OVERLAP_INVALID section=%u",data.header.section);
}
static bool OverlapSectionEligible(unsigned id,Vec3 eye){auto it=sections.find(id);return it!=sections.end()&&it->second.vb&&it->second.ib&&Distance(it->second.header,eye)<=650.f*650.f&&(zoneIndex==~0u||id==26000||zones[zoneIndex].sections.count(id));}
static bool DrawOverlapOwner(unsigned section,unsigned group,Vec3 eye){if(!group)return true;if(group==~0u)return false;if(group>=overlapGroups.size())return true;for(auto owner:overlapGroups[group])if(OverlapSectionEligible(owner,eye))return owner==section;return true;}
