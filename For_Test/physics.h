#pragma once
#ifndef BESTDRIVER_PHYSICS_H
#define BESTDRIVER_PHYSICS_H

#include "common_types.h"
#include <cmath>
#include <vector>

namespace bestdriver {

    static const float PHY_ACCEL = 5.0f;
    static const float PHY_BRAKE = 12.0f;
    static const float PHY_MAX_FWD = 8.3f;
    static const float PHY_MAX_REV = 2.5f;
    static const float PHY_MAX_STEER = 0.45f;
    static const float PHY_WHEELBASE = 3.0f;
    static const float PHY_WHEEL_TURN = 1.8f;
    static const float PHY_WHEEL_RET = 3.0f;

    inline float calcBicycleHeading(float h, float spd, float steer, float wb, float dt) {
        if (std::abs(spd) < 0.05f) return h;
        if (std::abs(steer) < 0.0005f) return h;
        float tr = wb / std::tan(steer);
        return normalizeAngle(h + (spd / tr) * dt);
    }

    inline Vec2 calcNewPosition(Vec2 pos, float spd, float h, float dt) {
        return Vec2(pos.x + spd * std::cos(h) * dt, pos.y + spd * std::sin(h) * dt);
    }

    inline Vec2 calcCollisionPushback(Vec2 pos, float h, float spd, float d = 0.15f) {
        float dir = (spd >= 0.0f) ? -1.0f : 1.0f;
        return Vec2(pos.x + dir * d * std::cos(h), pos.y + dir * d * std::sin(h));
    }

    inline float applyBraking(float spd, float force, float dt) {
        if (spd > 0.0f) { float r = spd - force * dt; return r < 0.0f ? 0.0f : r; }
        if (spd < 0.0f) { float r = spd + force * dt; return r > 0.0f ? 0.0f : r; }
        return 0.0f;
    }

} // namespace bestdriver

#endif