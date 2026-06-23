// course_evaluator.h
#pragma once
#ifndef BESTDRIVER_COURSE_EVALUATOR_H
#define BESTDRIVER_COURSE_EVALUATOR_H

#include "common_types.h"
#include "map_system.h"
#include "traffic_system.h"
#include "replay_recorder.h"
#include "parking_evaluator.h"
#include <vector>
#include <unordered_map>

namespace bestdriver {

    struct CourseEvaluatorConfig {
        float defaultSpeedLimit;
        float speedingGraceSeconds;
        float maxDeviationSeconds;
        float suddenAccelThreshold;
        float suddenBrakeThreshold;
        float signalTriggerDistance;
        float signalResetDistance;
        float stopLineTriggerDistance;
        int checkpointSignalPenalty;
        int speedingPenalty;
        int signalViolationPenalty;
        int yellowSignalPenalty;
        int courseDeviationPenalty;
        int suddenPenalty;
        int wrongGearPenalty;
        int engineOffPenalty;
        int notNeutralPenalty;
        int stopLinePenalty;
        int centerLinePenalty;
        int obstaclePenalty;
        int noSeatbeltPenalty;
        int speedBumpPenalty;
        float speedBumpSpeedLimit;

        // No-signal turn penalty settings
        int noSignalTurnPenalty;
        float turnDetectThreshold;
        float turnTrackWindow;

        CourseEvaluatorConfig()
            : defaultSpeedLimit(50),
            speedingGraceSeconds(1.5f),
            maxDeviationSeconds(20.0f),
            suddenAccelThreshold(30.0f),
            suddenBrakeThreshold(25.0f),
            signalTriggerDistance(6.0f),
            signalResetDistance(8.0f),
            stopLineTriggerDistance(6.0f),
            checkpointSignalPenalty(10),
            speedingPenalty(10),
            signalViolationPenalty(100),
            yellowSignalPenalty(20),
            courseDeviationPenalty(5),
            suddenPenalty(10),
            wrongGearPenalty(10),
            engineOffPenalty(10),
            notNeutralPenalty(10),
            stopLinePenalty(10),
            centerLinePenalty(100),
            obstaclePenalty(100),
            noSeatbeltPenalty(100),
            speedBumpPenalty(10),
            speedBumpSpeedLimit(40.0f),
            noSignalTurnPenalty(5),
            turnDetectThreshold(1.047f),   // about 60 degrees
            turnTrackWindow(2.5f) {
        }
    };

    class CourseEvaluator {
    public:
        CourseEvaluator();
        CourseEvaluator(const CourseEvaluatorConfig& config);

        void setConfig(const CourseEvaluatorConfig& config);
        const CourseEvaluatorConfig& getConfig() const;

        void setMap(const ICourseMap* map);
        void setTrafficSystem(TrafficSystem* ts);
        void setReplayRecorder(ReplayRecorder* rr);

        void loadCourse(const CourseInfo& courseInfo);
        void setCheckpoints(const std::vector<Checkpoint>& cps);
        void reset();

        std::vector<Penalty> evaluate(const VehicleState& vehicle, const VehicleControl& ctrl, float dt,
                                      const std::vector<NpcCar>* npcs = nullptr);

        ParkingEvaluation evaluateParking(const VehicleState& vehicle, const ParkingSlot& slot) const;
        bool checkParkingComplete(const VehicleState& vehicle, const ParkingSlot& slot) const;

        bool isDisqualified() const;
        int getTotalPenaltyPoints() const;
        float getElapsedTime() const;
        const std::vector<Penalty>& getPenaltyLog() const;
        const std::vector<Checkpoint>& getCheckpoints() const;
        int getPassedCheckpointCount() const;
        bool allCheckpointsPassed() const;
        const std::vector<ReplayFrame>& getReplayFrames() const;
        void checkWrongWay(const VehicleState& v, float dt, std::vector<Penalty>& pen);
    private:
        const ICourseMap* map_;
        TrafficSystem* trafficSystem_;
        ReplayRecorder* replayRecorder_;
        ParkingEvaluator parkingEvaluator_;
        CourseEvaluatorConfig config_;
        std::vector<Checkpoint> checkpoints_;

        float elapsedTime_;
        float prevSpeed_;
        bool hasPrevSpeed_;
        float speedingTimer_;
        bool speedingPenalized_;
        float deviationTimer_;
        bool deviationDisqualified_;
        float accelCooldown_;
        float brakeCooldown_;
        float notNeutralTimer_;
        bool notNeutralPenalized_;
        bool seatbeltDisqualified_;
        bool centerLineDisqualified_;
        bool obstacleDisqualified_;
        bool stopLineLatched_;
        bool wrongGearLatched_;
        bool engineOffLatched_;
        bool disqualified_;
        int totalPenaltyPoints_;
        std::vector<Penalty> penaltyLog_;
        std::unordered_map<int, bool> signalViolationLatched_;
        bool speedBumpLatched_;

        bool startPositionCaptured_;
        Vec2 startPosition_;
        float startCellIgnoreSeconds_;
        float startCellIgnoreDistance_;

        bool yellowCommitLatched_;
        int yellowCommitSignalId_;
        float yellowCommitTimer_;
        bool inIntersectionZone_;
        float intersectionGraceTimer_;

        int lockedSignalId_;
        float lockedSignalUntil_;
        int protectedSignalId_;
        float protectedSignalUntil_;

        // wrong way direction
        float wrongWayTimer_;
        bool wrongWayLatched_;

        // turn signal tracking during a turn
        float turnAccumAngle_;
        float turnTimer_;
        float prevHeading_;
        bool  hasPrevHeading_;
        bool  turnSignalPenalized_;

        void appendPenalty(std::vector<Penalty>& penalties, const Penalty& p);
        void checkCheckpoints(const VehicleState& v, const VehicleControl& c, std::vector<Penalty>& p);
        void checkSpeeding(const VehicleState& v, float dt, std::vector<Penalty>& p);
        void checkTrafficSignals(const VehicleState& v, std::vector<Penalty>& p);
        void checkCellViolations(const VehicleState& v, std::vector<Penalty>& p);
        void checkSpeedBump(const VehicleState& v, std::vector<Penalty>& p);
        void checkCourseDeviation(const VehicleState& v, float dt, std::vector<Penalty>& p);
        void checkSuddenMovement(const VehicleState& v, const VehicleControl& c, float dt, std::vector<Penalty>& p);
        void checkWrongGear(const VehicleState& v, std::vector<Penalty>& p);
        void checkEngineState(const VehicleState& v, std::vector<Penalty>& p);
        void checkNeutralStop(const VehicleState& v, float dt, std::vector<Penalty>& p);
        void checkSeatbeltStart(const VehicleState& v, const VehicleControl& c, std::vector<Penalty>& p);
        void checkTurnSignal(const VehicleState& v, const VehicleControl& c, float dt, std::vector<Penalty>& p);
        float getCurrentSpeedLimit(const Vec2& pos) const;
        bool isRoadLikeCell(CellType cell) const;
        static float normalizeAngle_(float angle);
    };

} // namespace bestdriver

#endif
