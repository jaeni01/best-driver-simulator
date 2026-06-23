#include "course_evaluator.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <vector>

using namespace std;

namespace bestdriver {

    namespace {
        struct CrosswalkCluster {
            bool found;
            Vec2 center;
            int minX;
            int maxX;
            int minY;
            int maxY;
            CrosswalkCluster()
                : found(false), center(0.0f, 0.0f), minX(0), maxX(0), minY(0), maxY(0) {
            }
        };

        static CrosswalkCluster collectCrosswalkCluster(const ICourseMap* map, int sx, int sy, int radius) {
            CrosswalkCluster out;
            if (!map) return out;
            if (map->getCellAt(sx, sy) != CellType::Crosswalk) return out;

            const int size = radius * 2 + 1;
            vector<vector<bool> > visited(size, vector<bool>(size, false));
            vector<pair<int, int> > q;
            q.push_back(make_pair(sx, sy));
            visited[radius][radius] = true;

            int head = 0;
            long long sumX = 0;
            long long sumY = 0;
            int count = 0;

            out.found = true;
            out.minX = sx;
            out.maxX = sx;
            out.minY = sy;
            out.maxY = sy;

            while (head < (int)q.size()) {
                int x = q[head].first;
                int y = q[head].second;
                head++;

                if (map->getCellAt(x, y) != CellType::Crosswalk) continue;

                sumX += x;
                sumY += y;
                count++;

                if (x < out.minX) out.minX = x;
                if (x > out.maxX) out.maxX = x;
                if (y < out.minY) out.minY = y;
                if (y > out.maxY) out.maxY = y;

                const int dx[4] = { 1, -1, 0, 0 };
                const int dy[4] = { 0, 0, 1, -1 };

                for (int i = 0; i < 4; i++) {
                    int nx = x + dx[i];
                    int ny = y + dy[i];

                    if (nx < sx - radius || nx > sx + radius || ny < sy - radius || ny > sy + radius) continue;
                    int vx = nx - sx + radius;
                    int vy = ny - sy + radius;
                    if (visited[vy][vx]) continue;
                    visited[vy][vx] = true;

                    if (map->getCellAt(nx, ny) == CellType::Crosswalk) {
                        q.push_back(make_pair(nx, ny));
                    }
                }
            }

            if (count <= 0) {
                out.found = false;
                return out;
            }

            out.center = Vec2((float)sumX / (float)count, (float)sumY / (float)count);
            return out;
        }
    }

    CourseEvaluator::CourseEvaluator()
        : map_(NULL),
        trafficSystem_(NULL),
        replayRecorder_(NULL),
        elapsedTime_(0.0f),
        prevSpeed_(0.0f),
        hasPrevSpeed_(false),
        speedingTimer_(0.0f),
        speedingPenalized_(false),
        deviationTimer_(0.0f),
        deviationDisqualified_(false),
        accelCooldown_(0.0f),
        brakeCooldown_(0.0f),
        notNeutralTimer_(0.0f),
        notNeutralPenalized_(false),
        seatbeltDisqualified_(false),
        centerLineDisqualified_(false),
        obstacleDisqualified_(false),
        stopLineLatched_(false),
        wrongGearLatched_(false),
        engineOffLatched_(false),
        disqualified_(false),
        totalPenaltyPoints_(0),
        speedBumpLatched_(false),
        startPositionCaptured_(false),
        startPosition_(0.0f, 0.0f),
        startCellIgnoreSeconds_(2.0f),
        startCellIgnoreDistance_(2.0f),
        yellowCommitLatched_(false),
        yellowCommitSignalId_(-1),
        yellowCommitTimer_(0.0f),
        inIntersectionZone_(false),
        intersectionGraceTimer_(0.0f),
        lockedSignalId_(-1),
        lockedSignalUntil_(0.0f),
        protectedSignalId_(-1),
        protectedSignalUntil_(0.0f),
        wrongWayTimer_(0.0f),
        wrongWayLatched_(false),
        turnAccumAngle_(0.0f),
        turnTimer_(0.0f),
        prevHeading_(0.0f),
        hasPrevHeading_(false),
        turnSignalPenalized_(false) {
    }

    CourseEvaluator::CourseEvaluator(const CourseEvaluatorConfig& c)
        : config_(c),
        map_(NULL),
        trafficSystem_(NULL),
        replayRecorder_(NULL),
        elapsedTime_(0.0f),
        prevSpeed_(0.0f),
        hasPrevSpeed_(false),
        speedingTimer_(0.0f),
        speedingPenalized_(false),
        deviationTimer_(0.0f),
        deviationDisqualified_(false),
        accelCooldown_(0.0f),
        brakeCooldown_(0.0f),
        notNeutralTimer_(0.0f),
        notNeutralPenalized_(false),
        seatbeltDisqualified_(false),
        centerLineDisqualified_(false),
        obstacleDisqualified_(false),
        stopLineLatched_(false),
        wrongGearLatched_(false),
        engineOffLatched_(false),
        disqualified_(false),
        totalPenaltyPoints_(0),
        speedBumpLatched_(false),
        startPositionCaptured_(false),
        startPosition_(0.0f, 0.0f),
        startCellIgnoreSeconds_(2.0f),
        startCellIgnoreDistance_(2.0f),
        yellowCommitLatched_(false),
        yellowCommitSignalId_(-1),
        yellowCommitTimer_(0.0f),
        inIntersectionZone_(false),
        intersectionGraceTimer_(0.0f),
        lockedSignalId_(-1),
        lockedSignalUntil_(0.0f),
        protectedSignalId_(-1),
        protectedSignalUntil_(0.0f),
        wrongWayTimer_(0.0f),
        wrongWayLatched_(false),
        turnAccumAngle_(0.0f),
        turnTimer_(0.0f),
        prevHeading_(0.0f),
        hasPrevHeading_(false),
        turnSignalPenalized_(false) {
    }

    void CourseEvaluator::setConfig(const CourseEvaluatorConfig& c) { config_ = c; }
    const CourseEvaluatorConfig& CourseEvaluator::getConfig() const { return config_; }
    void CourseEvaluator::setMap(const ICourseMap* m) { map_ = m; }
    void CourseEvaluator::setTrafficSystem(TrafficSystem* ts) { trafficSystem_ = ts; }
    void CourseEvaluator::setReplayRecorder(ReplayRecorder* rr) { replayRecorder_ = rr; }

    void CourseEvaluator::loadCourse(const CourseInfo& ci) {
        checkpoints_ = ci.checkpoints;
        if (ci.defaultSpeedLimit > 0) config_.defaultSpeedLimit = ci.defaultSpeedLimit;
    }

    void CourseEvaluator::setCheckpoints(const vector<Checkpoint>& cps) {
        checkpoints_ = cps;
    }

    void CourseEvaluator::reset() {
        elapsedTime_ = 0.0f;
        prevSpeed_ = 0.0f;
        hasPrevSpeed_ = false;
        speedingTimer_ = 0.0f;
        speedingPenalized_ = false;
        deviationTimer_ = 0.0f;
        deviationDisqualified_ = false;
        accelCooldown_ = 0.0f;
        brakeCooldown_ = 0.0f;
        notNeutralTimer_ = 0.0f;
        notNeutralPenalized_ = false;
        seatbeltDisqualified_ = false;
        centerLineDisqualified_ = false;
        obstacleDisqualified_ = false;
        stopLineLatched_ = false;
        wrongGearLatched_ = false;
        engineOffLatched_ = false;
        disqualified_ = false;
        totalPenaltyPoints_ = 0;
        penaltyLog_.clear();
        signalViolationLatched_.clear();
        speedBumpLatched_ = false;
        startPositionCaptured_ = false;
        startPosition_ = Vec2(0.0f, 0.0f);

        yellowCommitLatched_ = false;
        yellowCommitSignalId_ = -1;
        yellowCommitTimer_ = 0.0f;
        inIntersectionZone_ = false;
        intersectionGraceTimer_ = 0.0f;

        lockedSignalId_ = -1;
        lockedSignalUntil_ = 0.0f;
        protectedSignalId_ = -1;
        protectedSignalUntil_ = 0.0f;

        wrongWayTimer_ = 0.0f;
        wrongWayLatched_ = false;

        turnAccumAngle_ = 0.0f;
        turnTimer_ = 0.0f;
        prevHeading_ = 0.0f;
        hasPrevHeading_ = false;
        turnSignalPenalized_ = false;

        for (size_t i = 0; i < checkpoints_.size(); i++) {
            checkpoints_[i].passed = false;
        }

        if (replayRecorder_) {
            replayRecorder_->reset();
        }
    }

    vector<Penalty> CourseEvaluator::evaluate(const VehicleState& v, const VehicleControl& c, float dt, const std::vector<NpcCar>* npcs) {
        vector<Penalty> pen;

        if (dt < 0.0f) dt = 0.0f;
        elapsedTime_ += dt;

        if (!startPositionCaptured_) {
            startPosition_ = v.position;
            startPositionCaptured_ = true;
        }

        accelCooldown_ -= dt;
        if (accelCooldown_ < 0.0f) accelCooldown_ = 0.0f;

        brakeCooldown_ -= dt;
        if (brakeCooldown_ < 0.0f) brakeCooldown_ = 0.0f;

        if (trafficSystem_) {
            trafficSystem_->update(dt);
        }

        checkSeatbeltStart(v, c, pen);
        checkCheckpoints(v, c, pen);
        checkSpeeding(v, dt, pen);
        checkTrafficSignals(v, pen);
        checkCellViolations(v, pen);
        checkSpeedBump(v, pen);
        checkCourseDeviation(v, dt, pen);
        checkSuddenMovement(v, c, dt, pen);
        checkWrongGear(v, pen);
        checkWrongWay(v, dt, pen);
        checkEngineState(v, pen);
        checkNeutralStop(v, dt, pen);
        checkTurnSignal(v, c, dt, pen);

        if (replayRecorder_) {
            static const std::vector<NpcCar> kEmptyNpcs;
            replayRecorder_->record(elapsedTime_, dt, v, c, pen, npcs ? *npcs : kEmptyNpcs);
        }

        return pen;
    }

    ParkingEvaluation CourseEvaluator::evaluateParking(const VehicleState& v, const ParkingSlot& s) const {
        return parkingEvaluator_.evaluate(v, s);
    }

    bool CourseEvaluator::checkParkingComplete(const VehicleState& v, const ParkingSlot& s) const {
        return parkingEvaluator_.checkComplete(v, s);
    }

    bool CourseEvaluator::isDisqualified() const {
        return disqualified_;
    }

    int CourseEvaluator::getTotalPenaltyPoints() const {
        return totalPenaltyPoints_;
    }

    float CourseEvaluator::getElapsedTime() const {
        return elapsedTime_;
    }

    const vector<Penalty>& CourseEvaluator::getPenaltyLog() const {
        return penaltyLog_;
    }

    const vector<Checkpoint>& CourseEvaluator::getCheckpoints() const {
        return checkpoints_;
    }

    int CourseEvaluator::getPassedCheckpointCount() const {
        int n = 0;
        for (size_t i = 0; i < checkpoints_.size(); i++) {
            if (checkpoints_[i].passed) n++;
        }
        return n;
    }

    bool CourseEvaluator::allCheckpointsPassed() const {
        return !checkpoints_.empty() && getPassedCheckpointCount() == (int)checkpoints_.size();
    }

    const vector<ReplayFrame>& CourseEvaluator::getReplayFrames() const {
        static vector<ReplayFrame> empty;
        return replayRecorder_ ? replayRecorder_->getFrames() : empty;
    }


    void CourseEvaluator::appendPenalty(vector<Penalty>& pen, const Penalty& p) {
        pen.push_back(p);
        penaltyLog_.push_back(p);
        totalPenaltyPoints_ += p.points;
        if (p.points >= 30) {
            disqualified_ = true;
        }
    }

    void CourseEvaluator::checkCheckpoints(const VehicleState& v, const VehicleControl& c, vector<Penalty>& pen) {
        for (size_t i = 0; i < checkpoints_.size(); i++) {
            if (checkpoints_[i].passed) continue;
            if ((v.position - checkpoints_[i].position).length() > checkpoints_[i].radius) continue;

            checkpoints_[i].passed = true;

            if (checkpoints_[i].requireSignal && c.signal != checkpoints_[i].requiredSignal) {
                ostringstream oss;
                oss << "CP " << checkpoints_[i].id << ": signal";
                appendPenalty(pen, Penalty(PenaltyType::NoSignal, config_.checkpointSignalPenalty, elapsedTime_, oss.str()));
            }
        }
    }

    void CourseEvaluator::checkSpeeding(const VehicleState& v, float dt, vector<Penalty>& pen) {
        float limit = getCurrentSpeedLimit(v.position);
        float kmh = (float)fabs(v.speed) * 3.6f;

        if (kmh > limit + 0.1f) {
            speedingTimer_ += dt;
            if (speedingTimer_ > config_.speedingGraceSeconds && !speedingPenalized_) {
                ostringstream oss;
                oss << "Speeding: " << (int)kmh << "km/h";
                appendPenalty(pen, Penalty(PenaltyType::Speeding, config_.speedingPenalty, elapsedTime_, oss.str()));
                speedingPenalized_ = true;
            }
        }
        else {
            speedingTimer_ = 0.0f;
            speedingPenalized_ = false;
        }
    }

    void CourseEvaluator::checkTrafficSignals(const VehicleState& v, vector<Penalty>& pen) {
        if (elapsedTime_ < 0.5f) return;
        if (!trafficSystem_ || !map_) return;

        const float speedAbs = (float)fabs(v.speed);
        if (speedAbs <= 0.35f) return;

        const vector<TrafficSignal>& sigs = trafficSystem_->getSignals();
        if (sigs.empty()) return;

        const float hx = (float)cos(v.heading);
        const float hy = (float)sin(v.heading);
        const bool movingHorizontal = (float)fabs(hx) >= (float)fabs(hy);

        if (protectedSignalId_ != -1 && elapsedTime_ > protectedSignalUntil_) {
            protectedSignalId_ = -1;
            protectedSignalUntil_ = 0.0f;
        }

        auto calcMetrics = [&](const TrafficSignal& sig, float& forward, float& lateral, float& bumperFwd, float& score) {
            float dx = sig.position.x - v.position.x;
            float dy = sig.position.y - v.position.y;

            forward = dx * hx + dy * hy;
            lateral = (float)fabs(dx * (-hy) + dy * hx);
            bumperFwd = forward - 1.4f;

            float scoreForward = (float)fabs(bumperFwd);
            float scoreLateral = lateral * 4.2f;
            float scoreBehind = (forward < 0.0f) ? 2.5f : 0.0f;
            score = scoreForward + scoreLateral + scoreBehind;
            };

        const TrafficSignal* targetSig = NULL;
        float targetForward = 0.0f;
        float targetLateral = 0.0f;
        float targetBumperFwd = 0.0f;


        if (lockedSignalId_ != -1) {
            for (size_t i = 0; i < sigs.size(); ++i) {
                if (sigs[i].id != lockedSignalId_) continue;

                float forward, lateral, bumperFwd, score;
                calcMetrics(sigs[i], forward, lateral, bumperFwd, score);

                if (sigs[i].isHorizontal == movingHorizontal &&
                    forward > -8.0f && forward < 12.0f &&
                    lateral < 4.5f) {
                    targetSig = &sigs[i];
                    targetForward = forward;
                    targetLateral = lateral;
                    targetBumperFwd = bumperFwd;
                }
                else {
                    lockedSignalId_ = -1;
                    lockedSignalUntil_ = 0.0f;
                }
                break;
            }
        }


        if (!targetSig) {
            int bestIdx = -1;
            float bestScore = 999999.0f;
            float bestForward = 0.0f;
            float bestLateral = 0.0f;
            float bestBumperFwd = 0.0f;

            for (size_t i = 0; i < sigs.size(); ++i) {
                const TrafficSignal& sig = sigs[i];

                if (sig.isHorizontal != movingHorizontal) continue;

                float forward, lateral, bumperFwd, score;
                calcMetrics(sig, forward, lateral, bumperFwd, score);

                if (forward < -2.0f) continue;
                if (forward > 10.0f) continue;
                if (lateral > 3.0f) continue;

                if (score < bestScore) {
                    bestScore = score;
                    bestIdx = (int)i;
                    bestForward = forward;
                    bestLateral = lateral;
                    bestBumperFwd = bumperFwd;
                }
            }

            if (bestIdx == -1) {
                for (auto it = signalViolationLatched_.begin(); it != signalViolationLatched_.end(); ) {
                    bool found = false;
                    for (size_t i = 0; i < sigs.size(); ++i) {
                        if (sigs[i].id == it->first) {
                            float dx = sigs[i].position.x - v.position.x;
                            float dy = sigs[i].position.y - v.position.y;
                            float fwd = dx * hx + dy * hy;
                            if (fwd > -8.0f && fwd < 10.0f) found = true;
                            break;
                        }
                    }
                    if (!found) it = signalViolationLatched_.erase(it);
                    else ++it;
                }
                return;
            }

            targetSig = &sigs[bestIdx];
            targetForward = bestForward;
            targetLateral = bestLateral;
            targetBumperFwd = bestBumperFwd;


            if (targetForward > -1.5f && targetBumperFwd < 3.2f) {
                lockedSignalId_ = targetSig->id;
                lockedSignalUntil_ = elapsedTime_ + 2.5f;
            }
        }

        if (!targetSig) return;


        if (targetLateral > 2.8f && targetForward < 2.0f) {
            return;
        }

        std::unordered_map<int, bool>::iterator it = signalViolationLatched_.find(targetSig->id);
        bool alreadyLatched = (it != signalViolationLatched_.end());

        bool bumperPastLine = (targetBumperFwd <= 0.0f);

        if (bumperPastLine && !alreadyLatched) {
            signalViolationLatched_[targetSig->id] = true;

            if (targetSig->state == TrafficLight::Red) {
                if (targetSig->id == protectedSignalId_ && elapsedTime_ <= protectedSignalUntil_)
                    return;
                if (yellowCommitLatched_ && yellowCommitSignalId_ == targetSig->id)
                    return;

                ostringstream oss;
                oss << "RedLight: signal " << targetSig->id;
                appendPenalty(
                    pen,
                    Penalty(PenaltyType::SignalViolation, config_.signalViolationPenalty, elapsedTime_, oss.str()));
                disqualified_ = true;
                return;
            }

            if (targetSig->state == TrafficLight::Green || targetSig->state == TrafficLight::LeftArrow) {
                protectedSignalId_ = targetSig->id;
                protectedSignalUntil_ = elapsedTime_ + 4.0f;
            }

            if (targetSig->state == TrafficLight::Yellow) {
                yellowCommitLatched_ = true;
                yellowCommitSignalId_ = targetSig->id;
                protectedSignalId_ = targetSig->id;
                protectedSignalUntil_ = elapsedTime_ + 4.0f;
            }
        }

        if (!bumperPastLine && targetBumperFwd < 2.6f) {
            if (targetSig->state == TrafficLight::Green || targetSig->state == TrafficLight::LeftArrow) {
                protectedSignalId_ = targetSig->id;
                protectedSignalUntil_ = elapsedTime_ + 4.0f;
            }

            if (targetSig->state == TrafficLight::Yellow && targetBumperFwd < 1.3f) {
                yellowCommitLatched_ = true;
                yellowCommitSignalId_ = targetSig->id;
                protectedSignalId_ = targetSig->id;
                protectedSignalUntil_ = elapsedTime_ + 4.0f;
            }
        }

        if (lockedSignalId_ != -1 && elapsedTime_ > lockedSignalUntil_) {
            lockedSignalId_ = -1;
            lockedSignalUntil_ = 0.0f;
        }

        if (targetForward < -6.0f) {
            if (lockedSignalId_ == targetSig->id) {
                lockedSignalId_ = -1;
                lockedSignalUntil_ = 0.0f;
            }
        }

        for (auto it2 = signalViolationLatched_.begin(); it2 != signalViolationLatched_.end(); ) {
            bool found = false;
            for (size_t i = 0; i < sigs.size(); ++i) {
                if (sigs[i].id == it2->first) {
                    float dx = sigs[i].position.x - v.position.x;
                    float dy = sigs[i].position.y - v.position.y;
                    float fwd = dx * hx + dy * hy;
                    if (fwd > -8.0f && fwd < 10.0f) found = true;
                    break;
                }
            }
            if (!found) it2 = signalViolationLatched_.erase(it2);
            else ++it2;
        }
    }

    void CourseEvaluator::checkCellViolations(const VehicleState& v, vector<Penalty>& pen) {
        if (!map_) return;

        float moved = startPositionCaptured_ ? (v.position - startPosition_).length() : 999999.0f;
        bool inStartGrace = (elapsedTime_ < startCellIgnoreSeconds_) && (moved < startCellIgnoreDistance_);
        if (inStartGrace) return;

        CellType c = map_->getCellAt((int)v.position.x, (int)v.position.y);

        if (c == CellType::CenterLine && !centerLineDisqualified_) {
            centerLineDisqualified_ = true;
            appendPenalty(
                pen,
                Penalty(PenaltyType::CenterLineCross, config_.centerLinePenalty, elapsedTime_, "CenterLine")
            );
        }
        else if (c != CellType::CenterLine) {
            centerLineDisqualified_ = false;
        }

        if (c == CellType::Obstacle && !obstacleDisqualified_) {
            obstacleDisqualified_ = true;
            appendPenalty(
                pen,
                Penalty(PenaltyType::ObstacleCollision, config_.obstaclePenalty, elapsedTime_, "Obstacle")
            );
        }
        else if (c != CellType::Obstacle) {
            obstacleDisqualified_ = false;
        }
    }

    void CourseEvaluator::checkSpeedBump(const VehicleState& v, vector<Penalty>& pen) {
        if (!map_) return;

        CellType c = map_->getCellAt((int)v.position.x, (int)v.position.y);

        if (c == CellType::SpeedBump) {
            float kmh = (float)fabs(v.speed) * 3.6f;
            if (kmh >= config_.speedBumpSpeedLimit && !speedBumpLatched_) {
                ostringstream oss;
                oss << "SpeedBump: " << (int)kmh << "km/h (limit " << (int)config_.speedBumpSpeedLimit << "km/h)";
                appendPenalty(pen, Penalty(PenaltyType::Speeding, config_.speedBumpPenalty, elapsedTime_, oss.str()));
                speedBumpLatched_ = true;
            }
        }
        else {
            speedBumpLatched_ = false;
        }
    }

    void CourseEvaluator::checkCourseDeviation(const VehicleState& v, float dt, vector<Penalty>& pen) {
        if (!map_) return;

        CellType c = map_->getCellAt((int)v.position.x, (int)v.position.y);

        if (!isRoadLikeCell(c)) {
            deviationTimer_ += dt;
            if (deviationTimer_ > config_.maxDeviationSeconds && !deviationDisqualified_) {
                deviationDisqualified_ = true;
                appendPenalty(
                    pen,
                    Penalty(PenaltyType::CourseDeviation, 100, elapsedTime_, "Deviation>20s")
                );
            }
            return;
        }

        if (deviationTimer_ > 0.0f && deviationTimer_ <= config_.maxDeviationSeconds) {
            appendPenalty(
                pen,
                Penalty(PenaltyType::CourseDeviation, config_.courseDeviationPenalty, elapsedTime_, "Deviation")
            );
        }

        deviationTimer_ = 0.0f;
    }

    void CourseEvaluator::checkSuddenMovement(const VehicleState& v, const VehicleControl& c, float dt, vector<Penalty>& pen) {
        if (dt <= 0.0f) return;

        if (!hasPrevSpeed_) {
            prevSpeed_ = v.speed;
            hasPrevSpeed_ = true;
            return;
        }

        float prevAbs = (float)fabs(prevSpeed_);
        float currAbs = (float)fabs(v.speed);
        float rate = (v.speed - prevSpeed_) / dt;
        prevSpeed_ = v.speed;

        if (elapsedTime_ < 1.5f) return;
        if ((float)fabs(v.speed) < 2.5f) return;
        if (prevAbs < 1.5f && currAbs < 3.0f) return;

        // Sudden acceleration: only penalize when the player is actively
        // pressing the accelerator. Coasting / external speed bumps up
        // shouldn't be flagged.
        if (rate > config_.suddenAccelThreshold && c.accel > 0.1f && accelCooldown_ <= 0.0f) {
            appendPenalty(
                pen,
                Penalty(PenaltyType::SuddenAccel, config_.suddenPenalty, elapsedTime_, "SuddenAccel")
            );
            accelCooldown_ = 1.5f;
        }

        // Sudden brake: only penalize when the player is actively pressing
        // the brake. Speed drops from releasing the accelerator (engine
        // braking / coasting) or from collisions must not count as sudden
        // braking, since the player did not actually hit the brake pedal.
        if (rate < -config_.suddenBrakeThreshold && c.brake > 0.1f && brakeCooldown_ <= 0.0f) {
            appendPenalty(
                pen,
                Penalty(PenaltyType::SuddenStop, config_.suddenPenalty, elapsedTime_, "SuddenBrake")
            );
            brakeCooldown_ = 1.5f;
        }
    }

    void CourseEvaluator::checkWrongGear(const VehicleState& v, vector<Penalty>& pen) {
        bool bad = (v.gear == Gear::D && v.speed < -0.3f) || (v.gear == Gear::R && v.speed > 0.3f);

        if (bad && !wrongGearLatched_) {
            appendPenalty(
                pen,
                Penalty(PenaltyType::WrongGear, config_.wrongGearPenalty, elapsedTime_, "WrongGear")
            );
            wrongGearLatched_ = true;
        }
        else if (!bad) {
            wrongGearLatched_ = false;
        }
    }

    void CourseEvaluator::checkEngineState(const VehicleState& v, vector<Penalty>& pen) {
        if (!v.engineOn && (float)fabs(v.speed) > 0.2f) {
            if (!engineOffLatched_) {
                appendPenalty(
                    pen,
                    Penalty(PenaltyType::EngineOff, config_.engineOffPenalty, elapsedTime_, "EngineOff")
                );
                engineOffLatched_ = true;
            }
        }
        else if (v.engineOn) {
            engineOffLatched_ = false;
        }
    }

    void CourseEvaluator::checkNeutralStop(const VehicleState& v, float dt, vector<Penalty>& pen) {
        bool stopped = (float)fabs(v.speed) < 0.05f;
        bool notN = (v.gear != Gear::N && v.gear != Gear::P);

        if (stopped && v.engineOn && notN) {
            notNeutralTimer_ += dt;
            if (notNeutralTimer_ > 10.0f && !notNeutralPenalized_) {
                appendPenalty(
                    pen,
                    Penalty(PenaltyType::NotNeutral, config_.notNeutralPenalty, elapsedTime_, "NotNeutral>10s")
                );
                notNeutralPenalized_ = true;
            }
        }
        else {
            notNeutralTimer_ = 0.0f;
            notNeutralPenalized_ = false;
        }
    }

    void CourseEvaluator::checkSeatbeltStart(const VehicleState& v, const VehicleControl& c, vector<Penalty>& pen) {
        if (elapsedTime_ < 0.5f) return;

        bool actuallyMoving = (float)fabs(v.speed) > 0.8f;
        bool reallyStarting = actuallyMoving || (c.accel > 0.2f && v.engineOn);

        if (reallyStarting && !v.seatbeltOn && !seatbeltDisqualified_) {
            seatbeltDisqualified_ = true;
            appendPenalty(
                pen,
                Penalty(PenaltyType::NoSeatbelt, config_.noSeatbeltPenalty, elapsedTime_, "NoSeatbelt")
            );
        }
    }

    void CourseEvaluator::checkTurnSignal(const VehicleState& v, const VehicleControl& c, float dt, vector<Penalty>& pen) {
        if (dt <= 0.0f) return;
        if (elapsedTime_ < 0.5f) return;

        // Slow/stopped: keep accumulated angle, just restart the heading sample.
        // This lets drivers creep through intersections without losing the turn so far.
        if ((float)fabs(v.speed) < 1.0f) {
            hasPrevHeading_ = false;
            return;
        }

        if (!hasPrevHeading_) {
            prevHeading_ = v.heading;
            hasPrevHeading_ = true;
            return;
        }

        // Per-frame heading delta (normalized to -pi..pi)
        float delta = normalizeAngle(v.heading - prevHeading_);
        prevHeading_ = v.heading;

        // Reset accumulator if direction flips (filters small jitter during straight driving)
        if ((turnAccumAngle_ > 0.0f && delta < 0.0f) ||
            (turnAccumAngle_ < 0.0f && delta > 0.0f)) {
            if ((float)fabs(delta) > 0.02f) {
                turnAccumAngle_ = 0.0f;
                turnSignalPenalized_ = false;
            }
        }

        turnAccumAngle_ += delta;

        // Fire penalty when threshold angle is exceeded
        // Use 55 deg (0.96 rad) locally to be a bit more forgiving for cautious turns.
        const float threshold = 0.96f;
        float absAngle = (float)fabs(turnAccumAngle_);
        if (absAngle >= threshold && !turnSignalPenalized_) {
            // SFML coordinate: heading increases clockwise = right turn
            bool isRightTurn = (turnAccumAngle_ > 0.0f);
            Signal expected = isRightTurn ? Signal::Right : Signal::Left;

            // Hazard lights count as both blinkers on -> no penalty
            bool signalOk = (c.signal == expected) || (c.signal == Signal::Hazard);

            if (!signalOk) {
                ostringstream oss;
                oss << "NoSignalTurn: " << (isRightTurn ? "Right" : "Left");
                appendPenalty(
                    pen,
                    Penalty(PenaltyType::NoSignal, config_.noSignalTurnPenalty, elapsedTime_, oss.str())
                );
            }
            turnSignalPenalized_ = true;  // prevent duplicate penalty in same turn

            // After firing, reset accumulator so the NEXT turn can be detected independently.
            turnAccumAngle_ = 0.0f;
            turnSignalPenalized_ = false;
        }
    }



    float CourseEvaluator::getCurrentSpeedLimit(const Vec2& pos) const { 
        float limit = config_.defaultSpeedLimit;
        float minD = 999999.0f;

        for (size_t i = 0; i < checkpoints_.size(); i++) {
            if (checkpoints_[i].speedLimit <= 0) continue;

            float d = (pos - checkpoints_[i].position).length();
            float active = checkpoints_[i].radius * 1.5f;
            if (active < 3.0f) active = 3.0f;

            if (d <= active && d < minD) {
                minD = d;
                limit = checkpoints_[i].speedLimit;
            }
        }

        return limit;
    }

    bool CourseEvaluator::isRoadLikeCell(CellType c) const {
        return c == CellType::Road ||
            c == CellType::CenterLine ||
            c == CellType::Curb ||
            c == CellType::ParkingSlot ||
            c == CellType::StopLine ||
            c == CellType::Crosswalk ||
            c == CellType::SpeedBump;
    }

    float CourseEvaluator::normalizeAngle_(float a) {
        while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
        while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
        return a;
    }

    void CourseEvaluator::checkWrongWay(const VehicleState& v, float dt, vector<Penalty>& pen) {
        if (!map_) return;
        if (dt < 0.0f) dt = 0.0f;

        auto resetWrongWay = [&]() {
            wrongWayTimer_ = 0.0f;
            wrongWayLatched_ = false;
            };

        auto hScore = [&](int row, int cx, int cy) -> int {
            int score = 0;
            for (int x = cx - 6; x <= cx + 6; ++x) {
                if (map_->getCellAt(x, row) == CellType::CenterLine) {
                    score += 2;
                    if (map_->getCellAt(x - 1, row) == CellType::CenterLine) score += 1;
                    if (map_->getCellAt(x + 1, row) == CellType::CenterLine) score += 1;
                }
            }
            score -= (int)(fabs((float)(row - cy)) * 3.0f);
            return score;
            };

        auto vScore = [&](int col, int cx, int cy) -> int {
            int score = 0;
            for (int y = cy - 6; y <= cy + 6; ++y) {
                if (map_->getCellAt(col, y) == CellType::CenterLine) {
                    score += 2;
                    if (map_->getCellAt(col, y - 1) == CellType::CenterLine) score += 1;
                    if (map_->getCellAt(col, y + 1) == CellType::CenterLine) score += 1;
                }
            }
            score -= (int)(fabs((float)(col - cx)) * 3.0f);
            return score;
            };

        if (elapsedTime_ < 0.5f) {
            resetWrongWay();
            return;
        }

        if (v.gear == Gear::R) {
            resetWrongWay();
            return;
        }

        if (v.speed < 0.6f) {
            resetWrongWay();
            return;
        }

        int cx = (int)floor(v.position.x);
        int cy = (int)floor(v.position.y);

        CellType here = map_->getCellAt(cx, cy);
        if (!(here == CellType::Road ||
            here == CellType::CenterLine ||
            here == CellType::StopLine ||
            here == CellType::Crosswalk ||
            here == CellType::SpeedBump ||
            here == CellType::ParkingSlot)) {
            resetWrongWay();
            return;
        }

        if (here == CellType::Crosswalk || here == CellType::StopLine) {
            wrongWayTimer_ = 0.0f;
            return;
        }

        float moveX = (float)(cos(v.heading) * v.speed);
        float moveY = (float)(sin(v.heading) * v.speed);

        bool preferHorizontal = (float)fabs(moveX) >= (float)fabs(moveY);

        int bestRow = cy;
        int bestRowScore = -999999;
        for (int y = cy - 3; y <= cy + 3; ++y) {
            int s = hScore(y, cx, cy);
            if (s > bestRowScore) {
                bestRowScore = s;
                bestRow = y;
            }
        }

        int bestCol = cx;
        int bestColScore = -999999;
        for (int x = cx - 3; x <= cx + 3; ++x) {
            int s = vScore(x, cx, cy);
            if (s > bestColScore) {
                bestColScore = s;
                bestCol = x;
            }
        }

        bool useHorizontal = false;
        bool useVertical = false;

        if (preferHorizontal) {
            if (bestRowScore >= 3) useHorizontal = true;
            else if (bestColScore >= 6) useVertical = true;
        }
        else {
            if (bestColScore >= 3) useVertical = true;
            else if (bestRowScore >= 6) useHorizontal = true;
        }

        if (!useHorizontal && !useVertical) {
            resetWrongWay();
            return;
        }

        bool wrong = false;
        bool correct = false;

        if (useHorizontal) {
            float centerY = (float)bestRow + 0.5f;
            float laneOffset = v.position.y - centerY;

            if ((float)fabs(laneOffset) < 0.35f) {
                resetWrongWay();
                return;
            }

            float along = moveX;
            float signedAlong = 0.0f;

            if (laneOffset > 0.0f) {
                signedAlong = along;
            }
            else {
                signedAlong = -along;
            }

            wrong = (signedAlong < -0.55f);
            correct = (signedAlong > 0.20f);
        }
        else {
            float centerX = (float)bestCol + 0.5f;
            float laneOffset = v.position.x - centerX;

            if ((float)fabs(laneOffset) < 0.35f) {
                resetWrongWay();
                return;
            }

            float along = moveY;
            float signedAlong = 0.0f;

            if (laneOffset < 0.0f) {
                signedAlong = along;
            }
            else {
                signedAlong = -along;
            }

            wrong = (signedAlong < -0.55f);
            correct = (signedAlong > 0.20f);
        }

        if (wrong) {
            wrongWayTimer_ += dt;

            if (wrongWayTimer_ >= 0.20f && !wrongWayLatched_) {
                appendPenalty(
                    pen,
                    Penalty(PenaltyType::CenterLineCross, 100, elapsedTime_, "WrongWay")
                );
                wrongWayLatched_ = true;
            }
        }
        else if (correct) {
            resetWrongWay();
        }
        else {
            wrongWayTimer_ = 0.0f;
        }
    }

} // namespace bestdriver