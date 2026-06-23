#include "replay_player.h"
#include <cmath>

namespace bestdriver {

static float lerpF(float a, float b, float t) { return a + (b - a) * t; }

static float lerpAngle(float a, float b, float t) {
    const float PI = 3.14159265358979323846f;
    float d = b - a;
    while (d >  PI) d -= 2.0f * PI;
    while (d < -PI) d += 2.0f * PI;
    return a + d * t;
}

static void buildInterpolatedFrame(const ReplayFrame& a, const ReplayFrame& b, float t, ReplayFrame& out) {
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    out = a;
    out.timestamp = lerpF(a.timestamp, b.timestamp, t);
    out.vehicle.position.x = lerpF(a.vehicle.position.x, b.vehicle.position.x, t);
    out.vehicle.position.y = lerpF(a.vehicle.position.y, b.vehicle.position.y, t);
    out.vehicle.heading    = lerpAngle(a.vehicle.heading, b.vehicle.heading, t);
    out.vehicle.speed      = lerpF(a.vehicle.speed, b.vehicle.speed, t);

    // Interpolate NPC positions — assume NPC ordering is stable between
    // adjacent frames. If it's not (e.g. list length changed), fall back to
    // the earlier frame's NPC state for those entries.
    out.npcCars = a.npcCars;
    const size_t n = (a.npcCars.size() < b.npcCars.size()) ? a.npcCars.size() : b.npcCars.size();
    for (size_t i = 0; i < n; ++i) {
        out.npcCars[i] = a.npcCars[i];
        out.npcCars[i].position.x = lerpF(a.npcCars[i].position.x, b.npcCars[i].position.x, t);
        out.npcCars[i].position.y = lerpF(a.npcCars[i].position.y, b.npcCars[i].position.y, t);
        out.npcCars[i].heading    = lerpAngle(a.npcCars[i].heading, b.npcCars[i].heading, t);
    }

    // Traffic signals are discrete state — use snapshot from 'a' frame as-is
    // (already copied by 'out = a' above, kept explicit for clarity).
    out.signals = a.signals;

    // Checkpoint passed state is discrete — use 'a' frame so a checkpoint
    // turns green exactly when the player actually reached it during replay,
    // not halfway there via interpolation.
    out.cpPassed = a.cpPassed;
}

void ReplayPlayer::load(const std::vector<ReplayFrame>& rf) {
    frames_=&rf; currentIndex_=0; playing_=false; playbackSpeed_=1.0f;
    playbackClock_=rf.empty() ? 0.0f : rf.front().timestamp;
    if (!rf.empty()) interpolatedFrame_ = rf.front();
}

const ReplayFrame* ReplayPlayer::advance(float dt) {
    if (!frames_ || frames_->empty()) return NULL;
    if (!playing_) {
        interpolatedFrame_ = (*frames_)[currentIndex_];
        return &interpolatedFrame_;
    }
    if (dt < 0) dt = 0;
    playbackClock_ += dt * playbackSpeed_;
    while (currentIndex_+1 < (int)frames_->size() && (*frames_)[currentIndex_+1].timestamp <= playbackClock_) ++currentIndex_;
    if (currentIndex_ >= (int)frames_->size()) currentIndex_ = (int)frames_->size()-1;

    const ReplayFrame& cur = (*frames_)[currentIndex_];
    if (currentIndex_ + 1 < (int)frames_->size()) {
        const ReplayFrame& nxt = (*frames_)[currentIndex_ + 1];
        float span = nxt.timestamp - cur.timestamp;
        float t = (span > 1e-6f) ? (playbackClock_ - cur.timestamp) / span : 0.0f;
        buildInterpolatedFrame(cur, nxt, t, interpolatedFrame_);
    } else {
        interpolatedFrame_ = cur;
    }
    return &interpolatedFrame_;
}

const ReplayFrame* ReplayPlayer::getCurrentFrame() const {
    if (!frames_ || frames_->empty()) return NULL;
    return &interpolatedFrame_;
}

std::vector< std::pair<Vec2, Penalty> > ReplayPlayer::getPenaltyMarkers() const {
    std::vector< std::pair<Vec2, Penalty> > markers;
    if (!frames_) return markers;
    for (size_t i=0; i<frames_->size(); i++)
        for (size_t j=0; j<(*frames_)[i].penaltiesThisFrame.size(); j++)
            markers.push_back(std::pair<Vec2,Penalty>((*frames_)[i].vehicle.position, (*frames_)[i].penaltiesThisFrame[j]));
    return markers;
}

void ReplayPlayer::play() { if (frames_ && !frames_->empty()) playing_=true; }
void ReplayPlayer::pause() { playing_=false; }
void ReplayPlayer::toggle() { if (playing_) pause(); else play(); }
void ReplayPlayer::setSpeed(float s) { if (s>0) playbackSpeed_=s; }

void ReplayPlayer::jumpTo(int fi) {
    if (!frames_ || frames_->empty()) return;
    if (fi<0) fi=0; if (fi>(int)frames_->size()-1) fi=(int)frames_->size()-1;
    currentIndex_=fi; playbackClock_=(*frames_)[currentIndex_].timestamp;
    interpolatedFrame_ = (*frames_)[currentIndex_];
}

void ReplayPlayer::jumpToNextPenalty() {
    if (!frames_) return;
    for (int i=currentIndex_+1; i<(int)frames_->size(); i++) if (!(*frames_)[i].penaltiesThisFrame.empty()) { jumpTo(i); return; }
}

void ReplayPlayer::jumpToPrevPenalty() {
    if (!frames_) return;
    for (int i=currentIndex_-1; i>=0; i--) if (!(*frames_)[i].penaltiesThisFrame.empty()) { jumpTo(i); return; }
}

bool ReplayPlayer::isPlaying() const { return playing_; }
bool ReplayPlayer::isFinished() const { if (!frames_ || frames_->empty()) return true; return currentIndex_>=(int)frames_->size()-1; }
int ReplayPlayer::getCurrentIndex() const { return currentIndex_; }
int ReplayPlayer::getTotalFrames() const { return frames_ ? (int)frames_->size() : 0; }
float ReplayPlayer::getPlaybackClock() const { return playbackClock_; }

} // namespace bestdriver
