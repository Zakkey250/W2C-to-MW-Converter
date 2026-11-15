#pragma once

// NFSMW 1.3's render list and player car pose are read only while the W2C
// weather service reports a tunnel at the player's physical position.
static bool ReadPublicTunnelHeadlightPose(D3DMATRIX& pose) {
    __try {
        if (*reinterpret_cast<int*>(0x925e90) != 6) return false;
        Vec3 physical{};
        if (!W2CReadPlayerPosition(physical)) return false;
        const Vec3 world{physical.z, -physical.x, physical.y};
        auto list = *reinterpret_cast<BYTE***>(0x9b37cc);
        const unsigned count = *reinterpret_cast<unsigned*>(0x9b37d4);
        if (!list || count > 64) return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto conn = list[i];
            if (!conn || *reinterpret_cast<const unsigned*>(conn) != 0x8b5254 || conn[0x60]) continue;
            const auto info = *reinterpret_cast<const BYTE* const*>(conn + 0x44);
            if (!info) continue;
            const auto ride = *reinterpret_cast<const BYTE* const*>(info + 0x84);
            if (!ride || *reinterpret_cast<const unsigned*>(ride + 0x308) != 0) continue;

            const auto& matrix = *reinterpret_cast<const D3DMATRIX*>(conn + 0x330);
            const Vec3 origin{matrix._41, matrix._42, matrix._43};
            const float distance = Dot(Sub(origin, world), Sub(origin, world));
            const Vec3 axes[] = {{matrix._11, matrix._12, matrix._13},
                                 {matrix._21, matrix._22, matrix._23},
                                 {matrix._31, matrix._32, matrix._33}};
            if (!(distance <= 36.f) || !(std::abs(matrix._44 - 1.f) < .05f)) continue;
            bool rigid = true;
            for (const auto axis : axes) {
                const float squared = Dot(axis, axis);
                rigid &= squared > .8f && squared < 1.2f;
            }
            if (!rigid || std::abs(Dot(axes[0], axes[1])) > .15f) continue;
            pose = matrix;
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static float publicTunnelHeadlightBlend = 0.f;
static unsigned publicTunnelNativeLights = 0, publicTunnelFillLights = 0;
static void UpdatePublicTunnelHeadlightBlend() {
    static DWORD lastTick = GetTickCount();
    const auto tick = GetTickCount();
    const float step = std::min(.25f, float(tick - lastTick) * .004f);
    lastTick = tick;
    publicTunnelHeadlightBlend = std::clamp(publicTunnelHeadlightBlend +
                                          (w2cPlayerInTunnel ? step : -step), 0.f, 1.f);
}

static bool AddPublicTunnelHeadlights() {
    D3DMATRIX pose{};
    if (!ReadPublicTunnelHeadlightPose(pose)) return false;
    // Use the car's forward axis with a slight downward pitch in the same
    // render coordinates as SunSet's headlamps.
    const Vec3 direction{pose._11 - .065f * pose._31, pose._12 - .065f * pose._32,
                         pose._13 - .065f * pose._33};
    const float inverseLength = 1.f / std::sqrt(Dot(direction, direction));
    const unsigned retained = std::min(envVehicleLights, 14u);
    for (unsigned i = retained; i > 0; --i) {
        memcpy(envDP[i + 1], envDP[i - 1], sizeof envDP[0]);
        memcpy(envDD[i + 1], envDD[i - 1], sizeof envDD[0]);
        memcpy(envDC[i + 1], envDC[i - 1], sizeof envDC[0]);
    }
    envVehicleLights = retained + 2;
    for (unsigned i = 0; i < 2; ++i) {
        const float side = i == 0 ? -.61f : .61f;
        envDP[i][0] = pose._41 + 1.70f * pose._11 + side * pose._21 + .60f * pose._31;
        envDP[i][1] = pose._42 + 1.70f * pose._12 + side * pose._22 + .60f * pose._32;
        envDP[i][2] = pose._43 + 1.70f * pose._13 + side * pose._23 + .60f * pose._33;
        envDP[i][3] = 1.f / 62.f;
        envDD[i][0] = direction.x * inverseLength;
        envDD[i][1] = direction.y * inverseLength;
        envDD[i][2] = direction.z * inverseLength;
        envDD[i][3] = .643f; // 50-degree outer cone
        const float power = 1.7f * publicTunnelHeadlightBlend;
        envDC[i][0] = power;
        envDC[i][1] = .96f * power;
        envDC[i][2] = .89f * power;
        envDC[i][3] = .985f; // 10-degree inner cone
    }
    return true;
}
