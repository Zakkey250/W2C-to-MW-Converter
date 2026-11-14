#pragma once
// Construct pass lists once while CPU data is owned by its read worker.
// Preserve the original order within each opacity pass.
template<class Data> static void PrepareMeshDrawLists(Data& data){
 for(auto& list:data.drawMeshes)list.clear();
 for(unsigned n=0;n<data.meshes.size();++n)data.drawMeshes[MaterialOf(data.meshes[n].texture).blend?1:0].push_back(n);
}
