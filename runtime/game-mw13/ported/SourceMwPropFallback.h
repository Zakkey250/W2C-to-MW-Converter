#pragma once

namespace source_mw_prop_fallback {
static std::vector<std::array<BYTE,64>> placements;
static std::map<unsigned,w2v2disk::B> trees;
static w2v2disk::B models;

static void Parse(const w2v2disk::B& bytes) {
 using namespace w2v2disk;
 need(bytes.size() >= 24 && !memcmp(bytes.data(), "W2MPROP", 8), "MW fallback header");
 const unsigned count = r32(bytes, 8), treeCount = r32(bytes, 12), modelBytes = r32(bytes, 16);
 need(count && count <= 30000 && treeCount && treeCount <= 512 && modelBytes >= 16 &&
      modelBytes <= 16 * MiB && !r32(bytes, 20) && 24 + 64 * count <= bytes.size(), "MW fallback dimensions");
 size_t at = 24;
 std::vector<std::array<BYTE,64>> records(count);
 std::set<unsigned> referenced;
 for (auto& record : records) {
  for (unsigned component = 0; component < 7; ++component) rf(bytes, at + component * 4);
  const unsigned asset = r32(bytes, at + 32);
  need(asset && r32(bytes, at + 40) && !source_mw_tolls::Find(asset), "MW fallback stock asset");
  referenced.insert(asset);
  memcpy(record.data(), bytes.data() + at, record.size());
  at += record.size();
 }
 std::map<unsigned,B> definitions;
 for (unsigned i = 0; i < treeCount; ++i) {
  const unsigned asset = r32(bytes, at), length = r32(bytes, at + 4);
  at += 8;
  need(referenced.erase(asset) == 1 && length >= 24 && length <= 520 && (length - 8) % 16 == 0 &&
       at + length <= bytes.size(), "MW fallback hierarchy extent");
  const unsigned nodes = r32(bytes, at + 4);
  need(r32(bytes, at) && nodes == (length - 8) / 16, "MW fallback hierarchy header");
  for (unsigned j = 0; j < nodes; ++j) {
   const size_t node = at + 8 + j * 16;
   need(r32(bytes, node + 4) && !r32(bytes, node + 8) &&
        (bytes[node + 12] < nodes || bytes[node + 12] == 255) &&
        unsigned(bytes[node + 14]) + bytes[node + 15] <= nodes, "MW fallback hierarchy links");
  }
  definitions.emplace(asset, B(bytes.begin() + at, bytes.begin() + at + length));
  at += length;
 }
 need(referenced.empty() && bytes.size() - at == modelBytes && r32(bytes, at) == 0x80134000 &&
      r32(bytes, at + 4) == modelBytes - 8, "MW fallback model envelope");
 models.assign(bytes.begin() + at, bytes.end());
 trees = std::move(definitions);
 placements = std::move(records);
 Log("SOURCE_MW_PROP_FALLBACK_ACTIVE placements=%u trees=%u stock_attrib=1", count, treeCount);
}
}
