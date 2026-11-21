#pragma once
#include "BoundedDisk.h"

namespace w2collision {
namespace d = w2v2disk;
inline constexpr d::U MaxManifestBytes = 32 * d::MiB;
inline constexpr size_t MaxAddedFloor = 65536;
inline constexpr size_t MaxAddedWalls = 98304;

struct Node {
    float lo[3], hi[3];
    unsigned left, right;
};
struct Face {
    float vertices[3][3];
    unsigned surface, wall;
};
static_assert(sizeof(Node) == 32 && sizeof(Face) == 44, "WMC record size");

// Combine two validated BVHs with a common root. Keep native triangle ordinals
// stable and share surface names where both inputs use the same name.
inline d::B combine(const d::B& base, const d::B& extra) {
 using namespace w2v2disk;
 const unsigned baseFaces=r32(base,8), extraFaces=r32(extra,8);
 const unsigned baseNodes=r32(base,12), extraNodes=r32(extra,12);
 const unsigned baseSurfaces=r32(base,16), extraSurfaces=r32(extra,16);
 need(U(baseFaces)+extraFaces<=4000000&&U(baseNodes)+extraNodes+1<1000000,
      "Tri-City Bay merged collision budget");
 B names(base.begin()+20,base.begin()+20+80ull*baseSurfaces);
 std::vector<unsigned> surfaceMap;
 for(unsigned i=0;i<extraSurfaces;++i) {
  const auto* name=reinterpret_cast<const char*>(extra.data()+20+80ull*i);
  unsigned found=0;
  for(;found<names.size()/80;++found) {
   if(!strncmp(name,reinterpret_cast<const char*>(names.data()+80ull*found),80))break;
  }
  if(found==names.size()/80)names.insert(names.end(),name,name+80);
  surfaceMap.push_back(found);
 }
 const size_t nodeStart=20+names.size();
 const size_t faceStart=nodeStart+32ull*(1+baseNodes+extraNodes);
 B output(faceStart+44ull*(baseFaces+extraFaces));
 const unsigned header[]={0x31434d57,1,baseFaces+extraFaces,1+baseNodes+extraNodes,unsigned(names.size()/80)};
 memcpy(output.data(),header,sizeof(header));
 memcpy(output.data()+20,names.data(),names.size());
 Node joined{};
 for(unsigned i=0;i<3;++i) {
  joined.lo[i]=std::min(rf(base,20+80ull*baseSurfaces+4*i),rf(extra,20+80ull*extraSurfaces+4*i));
  joined.hi[i]=std::max(rf(base,32+80ull*baseSurfaces+4*i),rf(extra,32+80ull*extraSurfaces+4*i));
 }
 joined.left=1;
 joined.right=1+baseNodes;
 memcpy(output.data()+nodeStart,&joined,sizeof(joined));
 const auto copy=[&](const B& bytes,unsigned n,unsigned faces,unsigned surfaceCount,
                     unsigned nodeBias,unsigned faceBias,bool remap) {
  const size_t inNodes=20+80ull*surfaceCount;
  const size_t inFaces=inNodes+32ull*n;
  for(unsigned i=0;i<n;++i) {
   Node row;
   memcpy(&row,bytes.data()+inNodes+32ull*i,sizeof(row));
   if(row.right&0x80000000u)row.left+=faceBias;
   else {row.left+=nodeBias;row.right+=nodeBias;}
   memcpy(output.data()+nodeStart+32ull*(nodeBias+i),&row,sizeof(row));
  }
  auto* destination=output.data()+faceStart+44ull*faceBias;
  memcpy(destination,bytes.data()+inFaces,44ull*faces);
  if(remap)for(unsigned i=0;i<faces;++i) {
   unsigned key;
   memcpy(&key,destination+44ull*i+36,4);
   const auto translated=surfaceMap.at(key);
   memcpy(destination+44ull*i+36,&translated,4);
  }
 };
 copy(base,baseNodes,baseFaces,baseSurfaces,1,0,false);
 copy(extra,extraNodes,extraFaces,extraSurfaces,1+baseNodes,baseFaces,true);
 return output;
}

// The caller verifies base bytes with the source bundle before applying an add-on.
// Return a new WMC in memory. The original generation remains the source of truth.
inline d::B apply(const d::B& base, const d::J& patch, const std::string& generation) {
    using d::need;
    const auto schema = d::uintv(patch.at("schema"));
    if (schema == 3 && patch.contains("addWalls"))
        d::keys(patch, {"schema", "generation", "collisionSha256", "removeWalls", "addFloor", "keepWallFragments",
                       "closureDraws", "closureProps", "removeFloor", "keepFloorFragments", "geometryPages",
                       "addMetalFloor", "addWalls"});
    else if (schema == 3)
        d::keys(patch, {"schema", "generation", "collisionSha256", "removeWalls", "addFloor", "keepWallFragments",
                       "closureDraws", "closureProps", "removeFloor", "keepFloorFragments", "geometryPages", "addMetalFloor"});
    else if (schema == 2)
        d::keys(patch, {"schema", "generation", "collisionSha256", "removeWalls", "addFloor", "keepWallFragments",
                       "closureDraws", "closureProps"});
    else
        d::keys(patch, {"schema", "generation", "collisionSha256", "removeWalls", "addFloor", "keepWallFragments"});
    need((schema == 1 || schema == 2 || schema == 3) && patch.at("generation") == generation,
         "collision addon generation mismatch");
    need(d::sha(base) == d::str(patch.at("collisionSha256"), 64), "collision addon base SHA-256 mismatch");
    const auto oldFaces = d::r32(base, 8), oldNodes = d::r32(base, 12), surfaces = d::r32(base, 16);
    const size_t nodeStart = 20 + size_t(surfaces)*80, faceStart = nodeStart + size_t(oldNodes)*32;
    need(base.size() == faceStart + size_t(oldFaces)*44 && oldNodes && oldFaces && surfaces,
         "collision addon base extent");
    const auto& walls = patch.at("removeWalls");
    const auto& floor = patch.at("addFloor");
    const auto& fragments = patch.at("keepWallFragments");
    const auto empty = d::J::array();
    const auto& removedFloor = schema == 3 ? patch.at("removeFloor") : empty;
    const auto& floorFragments = schema == 3 ? patch.at("keepFloorFragments") : empty;
    const auto& metalFloor = schema == 3 ? patch.at("addMetalFloor") : empty;
    const auto& newWalls = schema == 3 && patch.contains("addWalls") ? patch.at("addWalls") : empty;
    need(walls.is_array() && walls.size() <= 20000 && floor.is_array() && floor.size() <= MaxAddedFloor &&
         metalFloor.is_array() && metalFloor.size() <= MaxAddedFloor-floor.size() &&
         fragments.is_array() && fragments.size() <= 20000 &&
         removedFloor.is_array() && removedFloor.size() <= 20000 &&
         floorFragments.is_array() && floorFragments.size() <= 20000 &&
         newWalls.is_array() && newWalls.size() <= MaxAddedWalls &&
         (!walls.empty() || !floor.empty() || !metalFloor.empty() || !removedFloor.empty() ||
          !newWalls.empty() ||
          (schema == 3 && !patch.at("geometryPages").empty())),
         "collision addon change count");
    const auto readFace = [&](unsigned index) {
        Face f;
        std::memcpy(&f, base.data()+faceStart+size_t(index)*44, sizeof(f));
        return f;
    };
    std::vector<bool> removed(oldFaces);
    for (const auto& raw : walls) {
        auto id = unsigned(d::uintv(raw, oldFaces-1));
        need(!removed[id] && readFace(id).wall == 1, "collision addon may only remove listed wall triangles");
        removed[id] = true;
    }
    for (const auto& raw : removedFloor) {
        auto id = unsigned(d::uintv(raw, oldFaces-1));
        need(!removed[id] && readFace(id).wall == 0, "collision addon listed floor differs");
        removed[id] = true;
    }
    std::vector<Face> added;
    added.reserve(floor.size()+metalFloor.size()+newWalls.size()+fragments.size()+floorFragments.size());
    const auto parseFace = [&](const d::J& row) {
        need(row.is_array() && row.size()==9, "collision addon floor dimensions");
        Face face{};
        for (unsigned v=0;v<3;++v) for (unsigned i=0;i<3;++i) {
            const auto value=d::number(row[v*3+i]);
            need(std::abs(value)<30000, "collision addon coordinate bounds");
            face.vertices[v][i]=float(value);
        }
        return face;
    };
    const auto appendFloor = [&](const d::J& rows, const char* material, size_t length) {
        if (rows.empty()) return;
        unsigned surface = 0;
        while (surface < surfaces && std::memcmp(base.data()+20+size_t(surface)*80,material,length)!=0)
            ++surface;
        need(surface < surfaces, "collision addon floor material is missing");
        for (const auto& row : rows) {
            auto face=parseFace(row);
            face.surface=surface;
            const auto& a = face.vertices[0]; const auto& b = face.vertices[1]; const auto& c = face.vertices[2];
            const float nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
            const float ny=(b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]);
            const float nz=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
            need(ny > .001f && ny*ny > .40f*(nx*nx+ny*ny+nz*nz), "collision addon floor slope or winding");
            added.push_back(face);
        }
    };
    appendFloor(floor, "asphalt", sizeof("asphalt"));
    appendFloor(metalFloor, "metal", sizeof("metal"));
    if (!newWalls.empty()) {
        unsigned concrete = 0;
        while (concrete < surfaces && std::memcmp(base.data()+20+size_t(concrete)*80,"concrete",sizeof("concrete"))!=0)
            ++concrete;
        need(concrete < surfaces, "collision addon wall material is missing");
        for (const auto& row : newWalls) {
            auto face=parseFace(row);
            const auto& a=face.vertices[0];
            const auto& b=face.vertices[1];
            const auto& c=face.vertices[2];
            const float nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
            const float ny=(b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]);
            const float nz=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
            need(nx*nx+ny*ny+nz*nz > 1e-6f, "collision addon wall area");
            face.surface=concrete;
            face.wall=1;
            added.push_back(face);
        }
    }
    const auto retainFragment = [&](const d::J& item, unsigned wall) {
        need(item.is_array() && item.size()==2, "collision addon fragment fields");
        const auto originalIndex=unsigned(d::uintv(item[0],oldFaces-1));
        need(removed[originalIndex], "collision addon fragment has no original wall");
        const auto original=readFace(originalIndex);
        need(original.wall == wall, "collision addon fragment type mismatch");
        auto face=parseFace(item[1]);
        for (const auto& v : face.vertices) for (unsigned axis=0;axis<3;++axis) {
            const auto a=original.vertices[0][axis], b=original.vertices[1][axis], c=original.vertices[2][axis];
            need(v[axis]>=std::min({a,b,c})-.002f && v[axis]<=std::max({a,b,c})+.002f,
                 "collision addon fragment outside original wall");
        }
        face.wall=wall;
        face.surface=original.surface;
        added.push_back(face);
    };
    for (const auto& item : fragments) retainFragment(item, 1);
    for (const auto& item : floorFragments) retainFragment(item, 0);

    constexpr unsigned missing=~0u, leaf=0x80000000;
    std::vector<Node> nodes(1);
    std::vector<Face> faces;
    nodes.reserve(oldNodes+2+added.size());
    faces.reserve(oldFaces-walls.size()-removedFloor.size()+added.size());
    const auto writeNode = [&](const Node& n) { nodes.push_back(n); return unsigned(nodes.size()-1); };
    const auto merge = [](const Node& a,const Node& b,unsigned left,unsigned right) {
        Node n{};
        n.left=left; n.right=right;
        for (unsigned i=0;i<3;++i) { n.lo[i]=std::min(a.lo[i],b.lo[i]); n.hi[i]=std::max(a.hi[i],b.hi[i]); }
        return n;
    };
    std::vector<bool> visited(oldNodes);
    const auto copy = [&](const auto& self,unsigned index,unsigned depth) -> unsigned {
        need(index<oldNodes && depth<126 && !visited[index], "collision addon base BVH node");
        visited[index]=true;
        Node n;
        std::memcpy(&n,base.data()+nodeStart+size_t(index)*32,sizeof(n));
        if (n.right&leaf) {
            const unsigned count=n.right&~leaf;
            need(count && n.left<oldFaces && count<=oldFaces-n.left, "collision addon base BVH leaf");
            const unsigned begin=unsigned(faces.size());
            for (unsigned i=n.left;i<n.left+count;++i) if (!removed[i]) faces.push_back(readFace(i));
            if (faces.size()==begin) return missing;
            n.left=begin; n.right=leaf|unsigned(faces.size()-begin);
            return writeNode(n);
        }
        auto left=self(self,n.left,depth+1), right=self(self,n.right,depth+1);
        if (left==missing) return right;
        if (right==missing) return left;
        return writeNode(merge(nodes[left],nodes[right],left,right));
    };
    auto original=copy(copy,0,0);
    need(original!=missing && faces.size()==oldFaces-walls.size()-removedFloor.size(), "collision addon base coverage");
    const auto build = [&](const auto& self,size_t begin,size_t end)->unsigned {
        Node n{};
        for (unsigned i=0;i<3;++i) n.lo[i]=n.hi[i]=added[begin].vertices[0][i];
        for (size_t j=begin;j<end;++j) for (const auto& p : added[j].vertices) for (unsigned i=0;i<3;++i) {
            n.lo[i]=std::min(n.lo[i],p[i]); n.hi[i]=std::max(n.hi[i],p[i]);
        }
        if (end-begin<=8) {
            n.left=unsigned(faces.size()); n.right=leaf|unsigned(end-begin);
            faces.insert(faces.end(),added.begin()+begin,added.begin()+end);
        } else {
            unsigned axis=0;
            for (unsigned i=1;i<3;++i) if (n.hi[i]-n.lo[i]>n.hi[axis]-n.lo[axis]) axis=i;
            const auto mid=(begin+end)/2;
            const auto center=[axis](const Face& f) {
                return f.vertices[0][axis]+f.vertices[1][axis]+f.vertices[2][axis];
            };
            std::nth_element(added.begin()+begin,added.begin()+mid,added.begin()+end,
                             [&](const Face& a,const Face& b){return center(a)<center(b);});
            n.left=self(self,begin,mid); n.right=self(self,mid,end);
        }
        return writeNode(n);
    };
    if (added.empty()) {
        // Keep a single root with no duplicate child references.
        nodes[0]=nodes.back();
        nodes.pop_back();
    } else {
        auto extra=build(build,0,added.size());
        nodes[0]=merge(nodes[original],nodes[extra],original,extra);
    }
    const unsigned header[]{0x31434d57,1,unsigned(faces.size()),unsigned(nodes.size()),surfaces};
    d::B output(nodeStart+nodes.size()*32+faces.size()*44);
    std::memcpy(output.data(),header,sizeof(header));
    std::memcpy(output.data()+20,base.data()+20,nodeStart-20);
    std::memcpy(output.data()+nodeStart,nodes.data(),nodes.size()*32);
    std::memcpy(output.data()+nodeStart+nodes.size()*32,faces.data(),faces.size()*44);
    d::Record record;
    record.count=header[2]; record.stride=44;
    record.meta={{"triangleCount",header[2]},{"nodeCount",header[3]},{"surfaceCount",surfaces}};
    d::wmc(record,output);
    return output;
}
}
