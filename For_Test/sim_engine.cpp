#include "sim_engine.h"
#include <cmath>
#include <algorithm>

namespace bestdriver {

    namespace {

        struct RectOBB {
            Vec2 center;
            Vec2 axisX;
            Vec2 axisY;
            float halfX;
            float halfY;
        };

        static float dot2(const Vec2& a, const Vec2& b) {
            return a.x * b.x + a.y * b.y;
        }

        static float len2(const Vec2& v) {
            return std::sqrt(v.x * v.x + v.y * v.y);
        }

        static Vec2 normalize2(const Vec2& v) {
            float l = len2(v);
            if (l <= 0.00001f) return Vec2(0.0f, 0.0f);
            return Vec2(v.x / l, v.y / l);
        }

        static void projectOBB(const RectOBB& r, const Vec2& axis, float& outMin, float& outMax) {
            Vec2 ex(r.axisX.x * r.halfX, r.axisX.y * r.halfX);
            Vec2 ey(r.axisY.x * r.halfY, r.axisY.y * r.halfY);

            Vec2 p1(r.center.x + ex.x + ey.x, r.center.y + ex.y + ey.y);
            Vec2 p2(r.center.x + ex.x - ey.x, r.center.y + ex.y - ey.y);
            Vec2 p3(r.center.x - ex.x + ey.x, r.center.y - ex.y + ey.y);
            Vec2 p4(r.center.x - ex.x - ey.x, r.center.y - ex.y - ey.y);

            float d1 = dot2(p1, axis);
            float d2 = dot2(p2, axis);
            float d3 = dot2(p3, axis);
            float d4 = dot2(p4, axis);

            outMin = std::min(std::min(d1, d2), std::min(d3, d4));
            outMax = std::max(std::max(d1, d2), std::max(d3, d4));
        }

        static bool overlapOnAxis(const RectOBB& a, const RectOBB& b, const Vec2& axis) {
            Vec2 n = normalize2(axis);
            if (std::abs(n.x) < 0.00001f && std::abs(n.y) < 0.00001f) return true;

            float aMin, aMax, bMin, bMax;
            projectOBB(a, n, aMin, aMax);
            projectOBB(b, n, bMin, bMax);

            return !(aMax < bMin || bMax < aMin);
        }

        static bool obbIntersects(const RectOBB& a, const RectOBB& b) {
            if (!overlapOnAxis(a, b, a.axisX)) return false;
            if (!overlapOnAxis(a, b, a.axisY)) return false;
            if (!overlapOnAxis(a, b, b.axisX)) return false;
            if (!overlapOnAxis(a, b, b.axisY)) return false;
            return true;
        }

        static RectOBB makePlayerRect(const VehicleState& vehicle, float x, float y) {
            Vec2 forward(std::cos(vehicle.heading), std::sin(vehicle.heading));
            Vec2 right(-forward.y, forward.x);

            RectOBB r;
            const float forwardOffset = 0.5f;

            r.center = Vec2(
                x + forward.x * forwardOffset,
                y + forward.y * forwardOffset
            );

            r.axisX = normalize2(forward);
            r.axisY = normalize2(right);
            r.halfX = 1.28f;
            r.halfY = 0.64f;
            return r;
        }

        static RectOBB makeNpcRect(const NpcCar& car) {
            RectOBB r;
            r.center = car.position;

            if (car.dir == NPC_LEFT || car.dir == NPC_RIGHT) {
                r.axisX = Vec2(1.0f, 0.0f);
                r.axisY = Vec2(0.0f, 1.0f);
                r.halfX = 0.88f;
                r.halfY = 0.50f;
            }
            else {
                r.axisX = Vec2(0.0f, 1.0f);
                r.axisY = Vec2(1.0f, 0.0f);
                r.halfX = 0.88f;
                r.halfY = 0.50f;
            }

            return r;
        }

    } // namespace

    SimEngine::SimEngine() : map_(NULL), mode_(GameMode::RoadDriving), elapsedTime_(0), finished_(false), collisions_(0) {}
    void SimEngine::init(GameMode mode) { mode_ = mode; elapsedTime_ = 0; finished_ = false; collisions_ = 0; collisionCooldown_ = 0.0f; vehicle_ = VehicleState(); }
    void SimEngine::spawnVehicle(Vec2 pos, float heading) { vehicle_ = VehicleState(); vehicle_.position = pos; vehicle_.heading = heading; }

    void SimEngine::tick(float dt, const VehicleControl& ctrl) {
        if (collisionCooldown_ > 0.0f) collisionCooldown_ -= dt;
        if (finished_) return;
        vehicle_.gear = ctrl.gear; vehicle_.signal = ctrl.signal; vehicle_.engineOn = ctrl.engineOn;
        vehicle_.autoHold = ctrl.autoHold; vehicle_.seatbeltOn = ctrl.seatbeltOn;

        if (!vehicle_.engineOn) {
            vehicle_.speed *= 0.90f;
            if (std::abs(vehicle_.speed) < 0.1f) vehicle_.speed = 0;
            elapsedTime_ += dt;
            return;
        }

        float aDir = 0;
        if (vehicle_.gear == Gear::D) aDir = 1; else if (vehicle_.gear == Gear::R) aDir = -1;

        if (vehicle_.gear == Gear::P || vehicle_.gear == Gear::N) {
            vehicle_.speed *= 0.85f;
            if (std::abs(vehicle_.speed) < 0.1f) vehicle_.speed = 0;
        }
        else {
            vehicle_.speed += ctrl.accel * PHY_ACCEL * aDir * dt;

            if (ctrl.brake > 0) {
                if (vehicle_.speed > 0) { vehicle_.speed -= PHY_BRAKE * ctrl.brake * dt; if (vehicle_.speed < 0) vehicle_.speed = 0; }
                else { vehicle_.speed += PHY_BRAKE * ctrl.brake * dt; if (vehicle_.speed > 0) vehicle_.speed = 0; }
            }

            if (ctrl.accel < 0.01f && ctrl.brake < 0.01f) {
                vehicle_.speed *= 0.92f;
            }
            if (std::abs(vehicle_.speed) < 0.15f && ctrl.accel < 0.01f) {
                vehicle_.speed = 0;
            }
        }

        if (vehicle_.autoHold && std::abs(vehicle_.speed) < 0.3f && ctrl.accel < 0.01f) vehicle_.speed = 0;
        vehicle_.speed = clampFloat(vehicle_.speed, -PHY_MAX_REV, PHY_MAX_FWD);

        if (std::abs(ctrl.dirX) > 0.01f && std::abs(vehicle_.speed) > 0.01f) {
            float turnRate = 1.8f; // 4.0f → 1.8f
            vehicle_.heading += ctrl.dirX * turnRate * dt;
            vehicle_.heading = normalizeAngle(vehicle_.heading);
            //Smooth steering: prevent instant sharp turning
            float targetSteer = ctrl.dirX;

            // If there is no input, the steering wheel slowly returns to center
            //float steerResponse = (std::abs(targetSteer) > 0.01f) ? 4.0f : 6.0f;

            // Smooth interpolation based on dt
            //float steerAlpha = steerResponse * dt;
            //if (steerAlpha > 1.0f) steerAlpha = 1.0f;
            //smoothSteerInput_ += (targetSteer - smoothSteerInput_) * steerAlpha;

            // Clean up very small values to zero
            if (std::abs(smoothSteerInput_) < 0.01f) {
                smoothSteerInput_ = 0.0f;
            }

            if (std::abs(smoothSteerInput_) > 0.01f && std::abs(vehicle_.speed) > 0.01f) {
                float speedAbs = std::abs(vehicle_.speed);

                // 저속에서는 조금 더 잘 돌고, 고속에서는 덜 꺾이게
                float speedFactor = 1.0f;
                if (speedAbs > 3.0f) {
                    speedFactor = 3.0f / speedAbs;
                    if (speedFactor < 0.45f) speedFactor = 0.45f;
                }
            }
        }

        if (std::abs(vehicle_.speed) > 0.01f) {
            float tx = vehicle_.position.x + std::cos(vehicle_.heading) * vehicle_.speed * dt;
            float ty = vehicle_.position.y + std::sin(vehicle_.heading) * vehicle_.speed * dt;

            float dx = tx - vehicle_.position.x;
            float dy = ty - vehicle_.position.y;
            float dist = std::sqrt(dx * dx + dy * dy);

            int steps = (int)std::ceil(dist / 0.015f);
            if (steps < 1) steps = 1;

            float lastSafeX = vehicle_.position.x;
            float lastSafeY = vehicle_.position.y;
            bool hit = false;

            for (int i = 1; i <= steps; ++i) {
                float t = (float)i / (float)steps;
                float nx = vehicle_.position.x + dx * t;
                float ny = vehicle_.position.y + dy * t;

                if (checkCollision(nx, ny)) {
                    hit = true;
                    break;
                }

                lastSafeX = nx;
                lastSafeY = ny;
            }

            if (!hit) {
                vehicle_.position.x = tx;
                vehicle_.position.y = ty;
            }
            else {
                vehicle_.position.x = lastSafeX;
                vehicle_.position.y = lastSafeY;

                float mvLen = std::sqrt(dx * dx + dy * dy);
                if (mvLen > 0.00001f) {
                    float backoff = 0.05f;
                    vehicle_.position.x -= (dx / mvLen) * backoff;
                    vehicle_.position.y -= (dy / mvLen) * backoff;
                }

                vehicle_.speed *= -0.1f;
                if (std::abs(vehicle_.speed) < 0.3f) vehicle_.speed = 0;
                if (collisionCooldown_ <= 0.0f) {
                    collisions_++;
                    collisionCooldown_ = 2.0f;
                }
            }
        }

        elapsedTime_ += dt;
    }

    bool SimEngine::checkNpcCollision(float nx, float ny) const {
        if (!map_) return false;

        const std::vector<NpcCar>& npcs = map_->getNpcCars();
        if (npcs.empty()) return false;

        RectOBB playerRect = makePlayerRect(vehicle_, nx, ny);

        const float hx = std::cos(vehicle_.heading);
        const float hy = std::sin(vehicle_.heading);
        const float rx = -hy;
        const float ry = hx;

        const float halfLen = playerRect.halfX;
        const float halfWid = playerRect.halfY;

        const float frontCenterX = nx + hx * halfLen;
        const float frontCenterY = ny + hy * halfLen;
        const float rearCenterX = nx - hx * halfLen;
        const float rearCenterY = ny - hy * halfLen;
        const float leftCenterX = nx - rx * halfWid;
        const float leftCenterY = ny - ry * halfWid;
        const float rightCenterX = nx + rx * halfWid;
        const float rightCenterY = ny + ry * halfWid;
        const float frontLeftX = nx + hx * halfLen - rx * halfWid;
        const float frontLeftY = ny + hy * halfLen - ry * halfWid;
        const float frontRightX = nx + hx * halfLen + rx * halfWid;
        const float frontRightY = ny + hy * halfLen + ry * halfWid;
        const float rearLeftX = nx - hx * halfLen - rx * halfWid;
        const float rearLeftY = ny - hy * halfLen - ry * halfWid;
        const float rearRightX = nx - hx * halfLen + rx * halfWid;
        const float rearRightY = ny - hy * halfLen + ry * halfWid;

        for (size_t i = 0; i < npcs.size(); ++i) {
            const NpcCar& car = npcs[i];
            RectOBB npcRect = makeNpcRect(car);

            if (obbIntersects(playerRect, npcRect)) {
                return true;
            }

            Vec2 localAxisX = npcRect.axisX;
            Vec2 localAxisY = npcRect.axisY;

            auto pointInsideNpc = [&](float px, float py) -> bool {
                Vec2 diff(px - npcRect.center.x, py - npcRect.center.y);
                float projX = std::abs(dot2(diff, localAxisX));
                float projY = std::abs(dot2(diff, localAxisY));
                return projX <= npcRect.halfX && projY <= npcRect.halfY;
            };

            if (pointInsideNpc(frontCenterX, frontCenterY)) return true;
            if (pointInsideNpc(rearCenterX, rearCenterY)) return true;
            if (pointInsideNpc(leftCenterX, leftCenterY)) return true;
            if (pointInsideNpc(rightCenterX, rightCenterY)) return true;
            if (pointInsideNpc(frontLeftX, frontLeftY)) return true;
            if (pointInsideNpc(frontRightX, frontRightY)) return true;
            if (pointInsideNpc(rearLeftX, rearLeftY)) return true;
            if (pointInsideNpc(rearRightX, rearRightY)) return true;
        }

        return false;
    }

    bool SimEngine::checkCollision(float nx, float ny) const {
        if (!map_) return false;

        int cx = (int)(nx + 0.5f);
        int cy = (int)(ny + 0.5f);

        float hx = std::cos(vehicle_.heading);
        float hy = std::sin(vehicle_.heading);
        float rx = -hy;
        float ry = hx;

        // Forward/lateral offsets for tile collision sampling.
        // Walls are detected via isPassable (RC_CURB excluded).
        // Values are conservative to avoid false positives near
        // parked NPC cars and sidewalk edges.
        const float fwdOff = 0.55f;
        const float latOff = 0.42f;

        // Forward-facing points
        float fwdX  = nx + hx * fwdOff;
        float fwdY  = ny + hy * fwdOff;
        float flX   = nx + hx * fwdOff - rx * latOff;
        float flY   = ny + hy * fwdOff - ry * latOff;
        float frX   = nx + hx * fwdOff + rx * latOff;
        float frY   = ny + hy * fwdOff + ry * latOff;

        // Rear-facing points
        float rearX = nx - hx * fwdOff;
        float rearY = ny - hy * fwdOff;
        float rlX   = nx - hx * fwdOff - rx * latOff;
        float rlY   = ny - hy * fwdOff - ry * latOff;
        float rrX   = nx - hx * fwdOff + rx * latOff;
        float rrY   = ny - hy * fwdOff + ry * latOff;

        // Side midpoints
        float lcX   = nx - rx * latOff;
        float lcY   = ny - ry * latOff;
        float rcX   = nx + rx * latOff;
        float rcY   = ny + ry * latOff;

        if (!map_->isPassable(cx, cy)) return true;
        if (!map_->isPassable((int)(fwdX  + 0.5f), (int)(fwdY  + 0.5f))) return true;
        if (!map_->isPassable((int)(flX   + 0.5f), (int)(flY   + 0.5f))) return true;
        if (!map_->isPassable((int)(frX   + 0.5f), (int)(frY   + 0.5f))) return true;
        if (!map_->isPassable((int)(rearX + 0.5f), (int)(rearY + 0.5f))) return true;
        if (!map_->isPassable((int)(rlX   + 0.5f), (int)(rlY   + 0.5f))) return true;
        if (!map_->isPassable((int)(rrX   + 0.5f), (int)(rrY   + 0.5f))) return true;
        if (!map_->isPassable((int)(lcX   + 0.5f), (int)(lcY   + 0.5f))) return true;
        if (!map_->isPassable((int)(rcX   + 0.5f), (int)(rcY   + 0.5f))) return true;

        if (checkNpcCollision(nx, ny)) return true;
        return false;
    }

} // namespace bestdriver
