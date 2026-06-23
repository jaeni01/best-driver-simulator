#pragma once
#ifndef BESTDRIVER_REPLAY_RECORDER_H
#define BESTDRIVER_REPLAY_RECORDER_H

#include "common_types.h"
#include "map_system.h"
#include <vector>

namespace bestdriver {

    struct ReplayFrame {
        float timestamp;
        VehicleState vehicle;
        VehicleControl control;
        std::vector<Penalty> penaltiesThisFrame;
        std::vector<NpcCar> npcCars;
        std::vector<TrafficSignal> signals; // traffic light states for this frame
        // Per-checkpoint passed state at this frame. Stored as unsigned char
        // to avoid vector<bool> proxy weirdness. 0 = not yet passed, 1 = passed.
        std::vector<unsigned char> cpPassed;
        ReplayFrame() : timestamp(0) {}
    };

    class ReplayRecorder {
    public:
        ReplayRecorder(float recordInterval = 0.033f);
        // Legacy signature — kept for compatibility with existing evaluator code.
        void record(float elapsedTime, float dt, const VehicleState& vehicle, const VehicleControl& ctrl,
                    const std::vector<Penalty>& penalties, const std::vector<NpcCar>& npcs);
        // Set signals on the most-recently recorded frame (if any). Call after
        // record() from the game loop to attach traffic light state.
        void setLastFrameSignals(const std::vector<TrafficSignal>& signals);
        // Set per-checkpoint passed state on the most-recently recorded frame.
        void setLastFrameCpPassed(const std::vector<unsigned char>& cpPassed);
        const std::vector<ReplayFrame>& getFrames() const;
        void reset();
        void setRecordInterval(float interval);
        float getRecordInterval() const;
    private:
        std::vector<ReplayFrame> frames_;
        float recordInterval_;
        float timeSinceLastRecord_;
        size_t lastRecordedCount_;
    };

} // namespace bestdriver

#endif
