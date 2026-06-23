#pragma once
#ifndef BESTDRIVER_REPLAY_PLAYER_H
#define BESTDRIVER_REPLAY_PLAYER_H

#include "replay_recorder.h"
#include <vector>
#include <utility>

namespace bestdriver {

    class ReplayPlayer {
    public:
        void load(const std::vector<ReplayFrame>& replayFrames);
        const ReplayFrame* advance(float dt);
        const ReplayFrame* getCurrentFrame() const;
        std::vector< std::pair<Vec2, Penalty> > getPenaltyMarkers() const;
        void play();
        void pause();
        void toggle();
        void setSpeed(float speed);
        void jumpTo(int frameIndex);
        void jumpToNextPenalty();
        void jumpToPrevPenalty();
        bool isPlaying() const;
        bool isFinished() const;
        int getCurrentIndex() const;
        int getTotalFrames() const;
        float getPlaybackClock() const;
    private:
        const std::vector<ReplayFrame>* frames_;
        int currentIndex_;
        bool playing_;
        float playbackSpeed_;
        float playbackClock_;
        ReplayFrame interpolatedFrame_;
    };

} // namespace bestdriver

#endif
