#pragma once
#ifndef BESTDRIVER_COMMON_TYPES_H
#define BESTDRIVER_COMMON_TYPES_H
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace bestdriver {

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float _x, float _y) : x(_x), y(_y) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x+o.x, y+o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x-o.x, y-o.y); }
    Vec2 operator*(float s) const { return Vec2(x*s, y*s); }
    float length() const { return std::sqrt(x*x+y*y); }
};

inline float normalizeAngle(float a) { while(a>(float)M_PI) a-=2.0f*(float)M_PI; while(a<-(float)M_PI) a+=2.0f*(float)M_PI; return a; }
inline float clampFloat(float v, float lo, float hi) { if(v<lo) return lo; if(v>hi) return hi; return v; }

enum class Gear { P, R, N, D };
enum class Signal { Off, Left, Right, Hazard };
enum class CellType { Road, Wall, ParkingSlot, Curb, StopLine, Crosswalk, SpeedBump, CenterLine, Obstacle, Grass, Building };
enum class TrafficLight { Red, Yellow, Green, LeftArrow };
enum class GameMode { Parking, RoadDriving };

struct VehicleControl {
    float accel, brake, steering;
    float dirX, dirY;   // arrow key direction (-1~+1 each)
    Gear gear; Signal signal;
    bool engineOn, autoHold, seatbeltOn;
    VehicleControl() : accel(0), brake(0), steering(0), dirX(0), dirY(0),
        gear(Gear::P), signal(Signal::Off), engineOn(false), autoHold(false), seatbeltOn(false) {}
};

struct VehicleState {
    Vec2 position; float heading, speed, steerAngle, wheel;
    Gear gear; Signal signal; bool engineOn, autoHold, seatbeltOn; float width, length;
    VehicleState() : heading(0), speed(0), steerAngle(0), wheel(0), gear(Gear::P), signal(Signal::Off),
        engineOn(false), autoHold(false), seatbeltOn(false), width(1.8f), length(4.2f) {}
};

struct ParkingSlot { int id; Vec2 position; float angle; bool isTarget; bool isParked = false;  ParkingSlot():id(0),angle(0),isTarget(false){} };
struct TrafficSignal { int id; Vec2 position; TrafficLight state; bool isHorizontal; TrafficSignal():id(0),state(TrafficLight::Red),isHorizontal(true){} };

enum class PenaltyType { NoSignal, Speeding, SignalViolation, CourseDeviation, SuddenStop, SuddenAccel, WrongGear, EngineOff, NoSeatbelt, NotNeutral, CenterLineCross, StopLineCross, ObstacleCollision, Collision };

struct Penalty {
    PenaltyType type; int points; float timestamp; std::string description;
    Penalty():type(PenaltyType::Collision),points(0),timestamp(0){}
    Penalty(PenaltyType t,int p,float ts,const std::string& d):type(t),points(p),timestamp(ts),description(d){}
};

struct Checkpoint {
    int id; Vec2 position; float radius; bool requireSignal; Signal requiredSignal; float speedLimit; bool passed;
    Checkpoint():id(0),radius(4.0f),requireSignal(false),requiredSignal(Signal::Off),speedLimit(0),passed(false){}
};

struct CourseInfo {
    std::string name, mapFile; std::vector<Checkpoint> checkpoints; float timeLimit, defaultSpeedLimit;
    CourseInfo():timeLimit(300),defaultSpeedLimit(50){}
};

inline const char* gearToChar(Gear g) { switch(g){case Gear::P:return"P";case Gear::R:return"R";case Gear::N:return"N";case Gear::D:return"D";}return"?"; }

} // namespace bestdriver
#endif
