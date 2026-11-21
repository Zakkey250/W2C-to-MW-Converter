#pragma once
#include "PublicTunnelFixturePositions.generated.h"
#include "TunnelAddonLights.h"

// These flags are derived from the source zones after loading, never stored
// in the world pack. Daytime ceiling lights only affect sheltered pixels.
static constexpr unsigned publicCeilingLight = 4;
static constexpr unsigned publicCeilingFlareLight = 8;
static constexpr unsigned publicCeilingUnboundedRoof = 16;
enum class PublicCeilingFlare : BYTE { Stock, Always, DayOnly };
static std::vector<PublicCeilingFlare> publicCeilingFlares;
static unsigned publicDayCeilingDraws=0;
static std::map<std::pair<int,int>,std::vector<unsigned>> publicCeilingGrid;
static std::map<std::pair<int,int>,std::vector<unsigned>> publicCeilingReceiverGrid;

static std::pair<int,int> PublicLightCell(Vec3 position) {
    return {int(std::floor(position.x / 64)), int(std::floor(position.y / 64))};
}

enum class PublicTunnelRoof { Outside, HeightLimited, Polygon };
static PublicTunnelRoof PublicSourceTunnel(Vec3 position, float heightTolerance) {
    auto cell = w2cWeatherZoneGrid.find(PublicLightCell(position));
    if (cell == w2cWeatherZoneGrid.end()) return PublicTunnelRoof::Outside;
    auto result = PublicTunnelRoof::Outside;
    for (auto index : cell->second) {
        const auto& zone = w2cWeatherZones[index];
        if (zone.type != 3 || !W2CPointInWeatherZone(zone, position.x, position.y)) continue;
        // Some high-altitude canyon tunnels have no authored roof height.
        if (zone.elevation == 0.f) result = PublicTunnelRoof::Polygon;
        else if (position.z <= zone.elevation + heightTolerance) return PublicTunnelRoof::HeightLimited;
    }
    return result;
}

static bool PublicHasCeilingLightNear(Vec3 position) {
    const auto [x, y] = PublicLightCell(position);
    for (int gx = x - 1; gx <= x + 1; ++gx) {
        for (int gy = y - 1; gy <= y + 1; ++gy) {
            auto cell = publicCeilingGrid.find({gx, gy});
            if (cell == publicCeilingGrid.end()) continue;
            for (auto index : cell->second) {
                const auto& light = envLights[index];
                const auto offset = Sub(light.position, position);
                if (offset.x * offset.x + offset.y * offset.y <= 64.f &&
                    std::abs(offset.z) <= 4.f) return true;
            }
        }
    }
    return false;
}

static void ApplyPublicDayCeilingLights(Vec3 minimum, Vec3 maximum, std::array<unsigned,8>& day) {
    const Vec3 middle{(minimum.x + maximum.x) * .5f, (minimum.y + maximum.y) * .5f,
                      (minimum.z + maximum.z) * .5f};
    auto cell = publicCeilingReceiverGrid.find(PublicLightCell(middle));
    if (cell == publicCeilingReceiverGrid.end()) return;
    std::array<std::pair<float,unsigned>,8> closest;
    closest.fill({FLT_MAX, ~0u});
    for (auto index : cell->second) {
        const auto& light = envLights[index];
        if (!RenderLightIntersects(light.position, 1.f / light.range, minimum, maximum)) continue;
        const auto offset = Sub(light.position, middle);
        const std::pair<float,unsigned> candidate{Dot(offset, offset), index};
        auto slot = std::lower_bound(closest.begin(), closest.end(), candidate);
        if (slot == closest.end()) continue;
        std::move_backward(slot, closest.end() - 1, closest.end());
        *slot = candidate;
    }
    if (closest[0].second == ~0u) return;
    auto other = day;
    day.fill(~0u);
    unsigned count = 0;
    for (const auto& light : closest) {
        if (light.second == ~0u) break;
        day[count++] = light.second;
    }
    for (auto light : other) {
        if (count == day.size() || light == ~0u) break;
        if (std::find(day.begin(), day.begin() + count, light) == day.begin() + count) day[count++] = light;
    }
}

static void AddPublicCeilingLight(Vec3 position, PublicTunnelRoof roof, bool compact, bool authored = false) {
    position.z -= .18f;
    const auto cell = PublicLightCell(position);
    const auto index = unsigned(envLights.size());
    envLightGrid[cell].push_back(index);
    publicCeilingGrid[cell].push_back(index);
    const auto roofFlag = roof == PublicTunnelRoof::Polygon ? publicCeilingUnboundedRoof : 0u;
    envLights.push_back({position, {1.f,.91f,.78f}, authored ? 1.55f : 1.85f,
                        compact ? 25.f : 33.f, {0.f,0.f,-1.f},
                        2u | publicCeilingLight | publicCeilingFlareLight | roofFlag,
                        authored ? .80f : .88f, authored ? .23f : .40f, 0});
}

static void PreparePublicCeilingLights() {
    unsigned sourceLights = 0, addedLights = 0, meshLights = 0;
    publicCeilingGrid.clear();
    publicCeilingReceiverGrid.clear();
    for (unsigned index = 0; index < envLights.size(); ++index) {
        auto& light = envLights[index];
        if (!(light.flags & 1) || light.blink || light.intensity <= 0.f ||
            !(light.direction.z <= -.5f)) continue;
        const auto roof = PublicSourceTunnel(light.position, 2.f);
        if (roof == PublicTunnelRoof::Outside) continue;
        light.flags |= publicCeilingLight;
        if (roof == PublicTunnelRoof::Polygon) light.flags |= publicCeilingUnboundedRoof;
        publicCeilingGrid[PublicLightCell(light.position)].push_back(index);
        ++sourceLights;
    }
    publicCeilingFlares.assign(envFlares.size(), PublicCeilingFlare::Stock);
    for (unsigned index = 0; index < envFlares.size(); ++index) {
        const auto& flare = envFlares[index];
        if (flare.type != 12 || !std::isfinite(flare.position.y) ||
            !std::isfinite(flare.position.z)) continue;
        const auto roof = PublicSourceTunnel(flare.position, 2.f);
        if (roof == PublicTunnelRoof::Outside) continue;
        publicCeilingFlares[index] = PublicCeilingFlare::Always;
        if (PublicHasCeilingLightNear(flare.position)) continue;

        // Some tunnel sections have the native lamp flare but no diffuse
        // light record. Reuse the fixture position and light the road below.
        AddPublicCeilingLight(flare.position, roof, true);
        ++addedLights;
    }
    for (const auto& fixture : publicLampFixtures) {
        const auto roof = PublicSourceTunnel(fixture.position, 12.f);
        if (roof == PublicTunnelRoof::Outside || PublicHasCeilingLightNear(fixture.position)) continue;
        AddPublicCeilingLight(fixture.position, roof, fixture.compact);
        if (fixture.compact) {
            const auto index = unsigned(envFlares.size());
            auto position = fixture.position;
            position.z -= .22f;
            envFlares.push_back({position,0,12,fixture.section,{0.f,0.f,1.f}});
            envFlareGrid[PublicLightCell(position)].push_back(index);
            publicCeilingFlares.push_back(PublicCeilingFlare::DayOnly);
        }
        ++meshLights;
    }
    for (const auto& fixture : tunnelAddonLights) {
        AddPublicCeilingLight(fixture.position, PublicTunnelRoof::Polygon, true, true);
        const auto index = unsigned(envFlares.size());
        auto position = fixture.position;
        position.z -= .22f;
        envFlares.push_back({position,0,12,fixture.section,{0.f,0.f,1.f}});
        envFlareGrid[PublicLightCell(position)].push_back(index);
        publicCeilingFlares.push_back(PublicCeilingFlare::Always);
    }
    if (worldLogEnabled && !tunnelAddonLights.empty())
        Log("WORLD_TUNNEL_ADDON_LIGHTS_ACTIVE fixtures=%u", unsigned(tunnelAddonLights.size()));
    // Compiled terrain draw tiles are 32 m wide; expand the receiver grid
    // once so the stream worker needs only one lookup per draw.
    for (const auto& [cell, indices] : publicCeilingGrid) {
        for (auto index : indices) {
            const auto& light = envLights[index];
            const float span = std::min(light.range, 160.f) + 32.f;
            const auto [left, bottom] = PublicLightCell({light.position.x-span,light.position.y-span,0.f});
            const auto [right, top] = PublicLightCell({light.position.x+span,light.position.y+span,0.f});
            for (int x = left; x <= right; ++x) {
                for (int y = bottom; y <= top; ++y) publicCeilingReceiverGrid[{x,y}].push_back(index);
            }
        }
    }
    if (worldLogEnabled) Log("PUBLIC_TUNNEL_CEILING source_lights=%u flare_lights=%u mesh_lights=%u",
                             sourceLights, addedLights, meshLights);
}
