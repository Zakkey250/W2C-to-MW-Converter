#pragma once
#include <cstdint>
namespace w2mw {
constexpr uint32_t Magic=0x31574D57; // WMW1, little endian
constexpr uint32_t Version=1;
struct Vec3 {float x,y,z;};
struct Vertex {Vec3 position; uint32_t color; float u,v;};
struct Header {uint32_t magic,version,section,meshes,vertices,indices; Vec3 minimum,maximum;};
struct Mesh {uint32_t texture,firstVertex,vertexCount,firstIndex,indexCount; Vec3 minimum,maximum;};
static_assert(sizeof(Header)==48 && sizeof(Mesh)==44 && sizeof(Vertex)==24);
}
