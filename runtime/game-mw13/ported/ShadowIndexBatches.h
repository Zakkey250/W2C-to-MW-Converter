#pragma once
// Preserve overlap ownership in the CPU-built opaque shadow index buffer.
// One draw per ownership group, without returning to one draw per material.
template<class Data> static void BuildShadowIndexBatches(Data& data){
 data.shadowIndices.clear();data.shadowSpans.clear();
 std::map<unsigned,std::vector<unsigned>> groups;
 for(unsigned n=0;n<data.meshes.size();++n){const auto& mesh=data.meshes[n];auto mat=MaterialOf(mesh.texture);
  if(mat.blend||mat.usage==1||mesh.texture==0x57FC91B7||mesh.texture==0xD2E09151||mesh.texture==0x29D9478C)continue;
  unsigned owner=n<data.meshOverlap.size()?data.meshOverlap[n]:0;if(owner==~0u)continue;
  auto& indices=groups[owner];for(unsigned i=0;i<mesh.indexCount;++i)indices.push_back(data.indices[mesh.firstIndex+i]+mesh.firstVertex);
 }
 for(const auto& group:groups){if(group.second.empty())continue;data.shadowSpans.push_back({group.first,unsigned(data.shadowIndices.size()),unsigned(group.second.size())});data.shadowIndices.insert(data.shadowIndices.end(),group.second.begin(),group.second.end());}
}
