#pragma once
#ifndef BESTDRIVER_TRAFFIC_SYSTEM_H
#define BESTDRIVER_TRAFFIC_SYSTEM_H

#include "common_types.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace bestdriver {

    enum class SignalPhase {
        MainGreen,
        MainYellow,
        LeftTurnGreen,
        LeftTurnYellow,
        AllRed,
        PedCrossing
    };

    enum class SignalRole {
        MainStraight,
        LeftTurn,
        Pedestrian,
        AllVehicle
    };

    struct TrafficSignalBinding {
        int signalId;
        SignalRole role;
        TrafficSignalBinding() : signalId(-1), role(SignalRole::AllVehicle) {}
        TrafficSignalBinding(int id, SignalRole r) : signalId(id), role(r) {}
    };

    struct TrafficSignalGroup {
        int groupId;
        std::vector<int> signalIds;
        std::vector<TrafficSignalBinding> bindings;
        std::vector<SignalPhase> phaseSequence;
        std::vector<float> phaseDurations;
        int currentPhaseIndex;
        float phaseTimer;
        bool pedestrianWalk;

        TrafficSignalGroup()
            : groupId(-1), currentPhaseIndex(0), phaseTimer(0), pedestrianWalk(false) {
        }
    };

    class TrafficSystem {
    public:
        void init(const std::vector<TrafficSignal>& rawSignals, const std::string& signalConfigFile);
        void init(const std::vector<TrafficSignal>& rawSignals, const std::vector<TrafficSignalGroup>& groups);

        void update(float dt);

        const std::vector<TrafficSignal>& getSignals() const;
        const std::vector<TrafficSignalGroup>& getGroups() const;

        TrafficLight getSignalAt(Vec2 position, float maxDistance = 999999.0f) const;
        float getRemainingTime(int signalId) const;
        bool isPedestrianWalkActive(int groupId) const;
        bool tryGetSignal(int signalId, TrafficSignal& outSignal) const;
        int getGroupIdForSignal(int signalId) const;

        void bindSignalToGroup(int signalId, int groupId);
        void bindSignalToGroup(int signalId, int groupId, SignalRole role);
        void setSignalRole(int signalId, SignalRole role);

        SignalRole getSignalRole(int signalId) const;

        void clear();

    private:
        std::vector<TrafficSignal> signals_;
        std::vector<TrafficSignalGroup> groups_;
        std::unordered_map<int, size_t> signalIndexById_;
        std::unordered_map<int, size_t> groupIndexById_;
        std::unordered_map<int, int> signalToGroup_;
        std::unordered_map<int, SignalRole> signalRoleById_;

        void rebuildSignalIndex();
        void rebuildGroupIndex();

        void loadGroupConfig(const std::string& filepath);
        void autoAssignGroups();
        void autoAssignRoles();
        void syncBindingsFromMaps();

        void applyAllCurrentPhases();
        void applyPhaseToSignals(TrafficSignalGroup& group);

        static SignalPhase parsePhaseToken(const std::string& token);
        static SignalRole parseRoleToken(const std::string& token);
        static TrafficLight resolveLightForRole(SignalPhase phase, SignalRole role);

        void applyInitialPhaseOffsets();
        void advanceGroupByOffset(TrafficSignalGroup& group, float offsetSeconds);
        Vec2 computeGroupAnchor(const TrafficSignalGroup& group) const;
        float computeInitialOffsetForGroup(const TrafficSignalGroup& group) const;
        float getTotalCycleTime(const TrafficSignalGroup& group) const;
    };

} // namespace bestdriver

#endif