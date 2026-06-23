#include "parking_evaluator.h"
#include <cmath>

namespace bestdriver {

ParkingEvaluator::ParkingEvaluator(float pt, float at) : positionTolerance_(pt), angleToleranceRad_(at) {}

ParkingEvaluation ParkingEvaluator::evaluate(const VehicleState& v, const ParkingSlot& s) const {
    ParkingEvaluation r;
    r.positionError = (v.position - s.position).length();
    r.angleError = (float)fabs(normalizeAngle(v.heading - s.angle));
    r.stopped = (float)fabs(v.speed) < 0.05f;
    r.gearPark = (v.gear == Gear::P);
    r.completed = r.positionError < positionTolerance_ && r.angleError < angleToleranceRad_ && r.stopped && r.gearPark;
    return r;
}

bool ParkingEvaluator::checkComplete(const VehicleState& v, const ParkingSlot& s) const { return evaluate(v,s).completed; }
void ParkingEvaluator::setTolerances(float p, float a) { positionTolerance_=p; angleToleranceRad_=a; }
float ParkingEvaluator::getPositionTolerance() const { return positionTolerance_; }
float ParkingEvaluator::getAngleTolerance() const { return angleToleranceRad_; }

float ParkingEvaluator::normalizeAngle(float angle) {
    while (angle > (float)M_PI) angle -= 2.0f*(float)M_PI;
    while (angle < -(float)M_PI) angle += 2.0f*(float)M_PI;
    return angle;
}

} // namespace bestdriver
