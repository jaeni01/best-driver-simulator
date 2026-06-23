#pragma once
#ifndef BESTDRIVER_SIM_ENGINE_H
#define BESTDRIVER_SIM_ENGINE_H

#include "common_types.h"
#include "physics.h"
#include "map_system.h"

namespace bestdriver {

    class SimEngine {
    public:
        SimEngine();
        void init(GameMode mode);
        void setMap(MapSystem* map) { map_ = map; }
        void spawnVehicle(Vec2 pos, float heading);
        void tick(float dt, const VehicleControl& ctrl);
        const VehicleState& getVehicleState() const { return vehicle_; }
        VehicleState& getVehicleStateMut() { return vehicle_; }
        float getElapsedTime() const { return elapsedTime_; }
        bool checkCollision(float nx, float ny) const;
        bool isFinished() const { return finished_; }
        void setFinished(bool f) { finished_ = f; }
        int getCollisionCount() const { return collisions_; }
    private:
        bool checkNpcCollision(float nx, float ny) const;
        VehicleState vehicle_;
        MapSystem* map_;
        GameMode mode_;
        float elapsedTime_;
        bool finished_;
        int collisions_;
        float collisionCooldown_ = 0.0f;
        float smoothSteerInput_;
    };

} // namespace bestdriver

#endif
