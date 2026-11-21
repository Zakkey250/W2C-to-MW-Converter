#pragma once

#include <cstring>

// W2C owns these constants within one captured environment pass. A pass starts
// with an empty cache because the game, mirrors and other mods share the device.
template <unsigned Registers>
struct EnvironmentRegisterCache {
    bool valid = false;
    float values[Registers * 4];

    template <typename Submit>
    void bind(const float (&next)[Registers * 4], Submit submit) {
        if (valid && std::memcmp(values, next, sizeof(values)) == 0) return;
        valid = submit(next, Registers);
        if (valid) std::memcpy(values, next, sizeof(values));
    }
};

struct EnvironmentSubmitState {
    EnvironmentRegisterCache<1> uv;
    EnvironmentRegisterCache<2> emission;
    EnvironmentRegisterCache<2> signal;
    EnvironmentRegisterCache<1> road;

    void reset() {
        uv.valid = emission.valid = signal.valid = road.valid = false;
    }
};
