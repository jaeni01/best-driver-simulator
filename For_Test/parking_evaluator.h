#pragma once
#ifndef BESTDRIVER_PARKING_EVALUATOR_H
#define BESTDRIVER_PARKING_EVALUATOR_H

#include "common_types.h"

namespace bestdriver {

    struct ParkingEvaluation {
        bool completed;
        float positionError;
        float angleError;
        bool stopped;
        bool gearPark;
        ParkingEvaluation() : completed(false), positionError(0), angleError(0), stopped(false), gearPark(false) {}
    };

    class ParkingEvaluator {
    public:
        ParkingEvaluator(float positionTolerance = 1.5f, float angleToleranceRad = 0.35f);
        ParkingEvaluation evaluate(const VehicleState& vehicle, const ParkingSlot& targetSlot) const;
        bool checkComplete(const VehicleState& vehicle, const ParkingSlot& targetSlot) const;
        void setTolerances(float positionTolerance, float angleToleranceRad);
        float getPositionTolerance() const;
        float getAngleTolerance() const;
    private:
        float positionTolerance_;
        float angleToleranceRad_;
        //float npcDelay_ = 0.0f; //������ 3/31 �߰�. (���� ��ȣ��)
        static float normalizeAngle(float angle);
    };

} // namespace bestdriver

#endif
