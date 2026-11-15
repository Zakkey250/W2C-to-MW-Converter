#pragma once
#include <algorithm>
#include <cmath>

namespace w2c_public_light {
inline float DaylightWeight(float sunElevation, bool daytime, bool inTunnel) {
    if (!daytime || inTunnel || !std::isfinite(sunElevation)) return 0.f;
    const float t = std::clamp((sunElevation - .06f) / .28f, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline void BalanceDaylight(float* diffuse, float* ambient, float peak, float shadeWeight) {
    float brightest = 0.f;
    float direct = 0.f;
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (!std::isfinite(diffuse[channel]) || !std::isfinite(ambient[channel])) return;
        direct = std::max(direct, diffuse[channel]);
        brightest = std::max(brightest, std::max(0.f, diffuse[channel]) +
                                         std::max(0.f, ambient[channel]));
    }
    if (direct < .05f) return;
    if (brightest > peak) {
        const float scale = peak / brightest;
        for (unsigned channel = 0; channel < 3; ++channel) {
            diffuse[channel] *= scale;
            ambient[channel] *= scale;
        }
    }
    if (shadeWeight <= 0.f || !std::isfinite(diffuse[3])) return;

    const float averageAmbient = (ambient[0] + ambient[1] + ambient[2]) / 3.f;
    const float fill = std::clamp(.38f - averageAmbient, 0.f, .16f) * shadeWeight;
    // WorldShader's neutral ambient factor is .5. With vertex colors enabled,
    // compensate for the brightest vertices too so this cannot clip highlights.
    const float compensation = std::lerp(.5f, 2.f, std::clamp(diffuse[3], 0.f, 1.f));
    for (unsigned channel = 0; channel < 3; ++channel) {
        const float transferred = std::min(fill, std::max(0.f, diffuse[channel]) / compensation);
        diffuse[channel] -= transferred * compensation;
        ambient[channel] += transferred;
    }
}

inline float TunnelHeadlightGain(float strongestChannel, float blend) {
    if (!std::isfinite(strongestChannel) || strongestChannel <= 0.f) return 1.f;
    return std::lerp(1.f, std::clamp(1.6f / strongestChannel, 1.f, 4.f), blend);
}
}
