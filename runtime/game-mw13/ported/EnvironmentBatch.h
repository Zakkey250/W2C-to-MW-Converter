#pragma once
static void BuildEnvironmentTiles(CpuSection& data){std::vector<Mesh> meshes;std::vector<unsigned> indices;std::vector<unsigned> ownership;std::vector<std::array<unsigned,4>> sourceGroups;
 for(auto& source:data.meshes){std::map<std::array<int,3>,std::vector<unsigned>> tiles;auto v=data.vertices.data()+source.firstVertex;
  for(unsigned n=0;n<source.indexCount;n+=3){auto ix=data.indices.data()+source.firstIndex+n;unsigned owner=data.triangleOverlap.empty()?0:data.triangleOverlap[(source.firstIndex+n)/3];if(owner==~0u)continue;Vec3 c{(v[ix[0]].position.x+v[ix[1]].position.x+v[ix[2]].position.x)/3,(v[ix[0]].position.y+v[ix[1]].position.y+v[ix[2]].position.y)/3,0};auto& t=tiles[{int(std::floor(c.x/32)),int(std::floor(c.y/32)),int(owner)}];t.insert(t.end(),ix,ix+3);}
  for(auto& tile:tiles){Mesh m=source;m.firstIndex=unsigned(indices.size());m.indexCount=unsigned(tile.second.size());m.minimum={FLT_MAX,FLT_MAX,FLT_MAX};m.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};for(auto index:tile.second){auto p=v[index].position;m.minimum.x=std::min(m.minimum.x,p.x);m.minimum.y=std::min(m.minimum.y,p.y);m.minimum.z=std::min(m.minimum.z,p.z);m.maximum.x=std::max(m.maximum.x,p.x);m.maximum.y=std::max(m.maximum.y,p.y);m.maximum.z=std::max(m.maximum.z,p.z);}indices.insert(indices.end(),tile.second.begin(),tile.second.end());meshes.push_back(m);ownership.push_back(unsigned(tile.first[2]));
   Vec3 extent{source.maximum.x-source.minimum.x,source.maximum.y-source.minimum.y,source.maximum.z-source.minimum.z};
   if(source.vertexCount<=128&&extent.x<=32&&extent.y<=32&&extent.z<=32)sourceGroups.push_back({0,unsigned(int(std::floor((source.minimum.x+source.maximum.x)/64))),unsigned(int(std::floor((source.minimum.y+source.maximum.y)/64))),unsigned(int(std::floor((source.minimum.z+source.maximum.z)/64)))});
   else sourceGroups.push_back({1,source.firstVertex,source.vertexCount,0});}}
 data.meshes=std::move(meshes);data.indices=std::move(indices);data.header.meshes=unsigned(data.meshes.size());data.lights.reserve(data.meshes.size());
 for(auto& m:data.meshes){EnvironmentMeshLights result;result.day.fill(~0u);result.night.fill(~0u);Vec3 mid{(m.minimum.x+m.maximum.x)*.5f,(m.minimum.y+m.maximum.y)*.5f,(m.minimum.z+m.maximum.z)*.5f};std::vector<std::pair<float,unsigned>> day,night;int gx=int(std::floor(mid.x/64)),gy=int(std::floor(mid.y/64));
  for(int x=gx-2;x<=gx+2;++x)for(int y=gy-2;y<=gy+2;++y){auto it=envLightGrid.find({x,y});if(it==envLightGrid.end())continue;for(auto n:it->second){auto& l=envLights[n];float dx=std::max({m.minimum.x-l.position.x,0.f,l.position.x-m.maximum.x}),dy=std::max({m.minimum.y-l.position.y,0.f,l.position.y-m.maximum.y}),dz=std::max({m.minimum.z-l.position.z,0.f,l.position.z-m.maximum.z});if(dx*dx+dy*dy+dz*dz>l.range*l.range)continue;dx=l.position.x-mid.x;dy=l.position.y-mid.y;dz=l.position.z-mid.z;float distance=dx*dx+dy*dy+dz*dz;
#ifdef W2C_PUBLIC_BUILD
   if(!(l.flags&publicCeilingFlareLight))night.push_back({distance,n});
   if(!(l.flags&1)||(l.flags&publicCeilingLight))day.push_back({distance,n});
#else
   night.push_back({distance,n});if(!(l.flags&1))day.push_back({distance,n});
#endif
  }}
  std::sort(day.begin(),day.end());std::sort(night.begin(),night.end());for(unsigned n=0;n<std::min(8u,unsigned(day.size()));++n)result.day[n]=day[n].second;for(unsigned n=0;n<std::min(8u,unsigned(night.size()));++n)result.night[n]=night[n].second;data.lights.push_back(result);}
 std::map<std::vector<unsigned>,unsigned> groups;std::vector<Mesh> merged;std::vector<EnvironmentMeshLights> mergedLights;std::vector<unsigned> mergedOwnership;std::vector<std::vector<unsigned>> pieces;
 for(unsigned n=0;n<data.meshes.size();++n){auto& m=data.meshes[n];auto& l=data.lights[n];// A shared vertex buffer permits opaque objects with different source
  // base vertices to share a spatial/material/light batch. Rebase only
  // indices; normals, UVs, colours and source ownership remain intact.
  std::vector<unsigned> key{m.texture};key.insert(key.end(),sourceGroups[n].begin(),sourceGroups[n].end());key.push_back(ownership[n]);key.insert(key.end(),l.day.begin(),l.day.end());key.insert(key.end(),l.night.begin(),l.night.end());
  // Alpha-over surfaces retain source submission order. Opaque and additive
  // batches can be combined without changing their compositing order.
  if(MaterialOf(m.texture).blend&&MaterialOf(m.texture).blend!=2)key.push_back(n);
  auto [it,inserted]=groups.emplace(key,unsigned(merged.size()));if(inserted){merged.push_back(m);mergedLights.push_back(l);mergedOwnership.push_back(ownership[n]);pieces.emplace_back();}else{auto& a=merged[it->second];a.minimum={std::min(a.minimum.x,m.minimum.x),std::min(a.minimum.y,m.minimum.y),std::min(a.minimum.z,m.minimum.z)};a.maximum={std::max(a.maximum.x,m.maximum.x),std::max(a.maximum.y,m.maximum.y),std::max(a.maximum.z,m.maximum.z)};}
  auto& p=pieces[it->second];for(unsigned i=0;i<m.indexCount;++i)p.push_back(data.indices[m.firstIndex+i]+m.firstVertex);
 }
 unsigned before=unsigned(data.meshes.size());std::vector<unsigned> batched;batched.reserve(data.indices.size());for(unsigned n=0;n<merged.size();++n){auto& part=pieces[n];auto bounds=std::minmax_element(part.begin(),part.end());unsigned base=*bounds.first;merged[n].firstVertex=base;merged[n].vertexCount=*bounds.second-base+1;merged[n].firstIndex=unsigned(batched.size());merged[n].indexCount=unsigned(part.size());for(auto index:part)batched.push_back(index-base);}
 data.meshes=std::move(merged);data.lights=std::move(mergedLights);data.meshOverlap=std::move(mergedOwnership);data.triangleOverlap.clear();data.indices=std::move(batched);data.header.meshes=unsigned(data.meshes.size());data.header.indices=unsigned(data.indices.size());Log("ENVIRONMENT_BATCH section=%u tiles=%u batches=%u indices=%u",data.header.section,before,data.header.meshes,data.header.indices);
}

