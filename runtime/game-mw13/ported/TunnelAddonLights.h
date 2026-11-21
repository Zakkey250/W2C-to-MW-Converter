#pragma once
#include "../../format/GeometryAddon.h"

struct TunnelAddonLight {
    unsigned section;
    Vec3 position;
};
static std::vector<TunnelAddonLight> tunnelAddonLights;

static std::vector<TunnelAddonLight> ReadTunnelAddonLights(
    const std::filesystem::path& path, const std::string& generation,
    const std::string& collisionSha, const std::map<unsigned,w2geometry::Page>& geometry) {
    if (!std::filesystem::exists(path)) return {};
    w2v2disk::File file(path);
    constexpr uint64_t limit = 64 * 1024;
    const auto payload = w2v2disk::parse(file.read(0, file.size, limit), limit);
    w2v2disk::need(payload.is_object() && payload.size() == 4 &&
        w2v2disk::uintv(payload.at("schema")) == 1 &&
        w2v2disk::str(payload.at("generation")) == generation &&
        w2v2disk::str(payload.at("collisionAddonSha256")) == collisionSha, "tunnel light package identity");
    const auto& lights = payload.at("lights");
    w2v2disk::need(lights.is_array() && lights.size() <= 384, "tunnel light count");
    std::vector<TunnelAddonLight> result;
    std::set<std::tuple<int,int,int>> occupied;
    for (const auto& row : lights) {
        w2v2disk::need(row.is_array() && row.size() == 4, "tunnel light row");
        const auto section = w2v2disk::uintv(row[0]);
        w2v2disk::need(section <= 65535 && geometry.count(unsigned(section)), "tunnel light section");
        const auto& page = geometry.at(unsigned(section)).bounds;
        float position[3];
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto value = w2v2disk::number(row[axis + 1]);
            const auto minimum = page[axis];
            const auto maximum = page[3+axis];
            w2v2disk::need(std::isfinite(value) && value >= minimum - .05 && value <= maximum + .05,
                           "tunnel light outside its geometry page");
            position[axis] = float(value);
        }
        w2v2disk::need(occupied.emplace(int(position[0] * 10), int(position[1] * 10), int(position[2] * 10)).second,
                       "duplicate tunnel light");
        result.push_back({unsigned(section), {position[0], position[1], position[2]}});
    }
    return result;
}
