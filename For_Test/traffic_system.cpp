#include "traffic_system.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdlib>

namespace bestdriver {

    namespace {
        //trim upper 등등 지워도 댐 ㅇㅋ?
        std::string trim(const std::string& s) {
            size_t b = 0;
            while (b < s.size() && isspace((unsigned char)s[b])) ++b;
            size_t e = s.size();
            while (e > b && isspace((unsigned char)s[e - 1])) --e;
            return s.substr(b, e - b);
        }

        std::vector<std::string> split(const std::string& s, char d) {
            std::vector<std::string> out;
            std::stringstream ss(s);
            std::string item;
            while (std::getline(ss, item, d)) out.push_back(item);
            return out;
        }

        bool startsWith(const std::string& s, const std::string& p) {
            return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
        }

        std::string toUpperCopy(std::string s) {
            for (size_t i = 0; i < s.size(); ++i) {
                s[i] = (char)std::toupper((unsigned char)s[i]);
            }
            return s;
        }

        bool isBCourseConfig(const std::string& cfg) {
            std::string u = toUpperCopy(cfg);

            return u.find("BCOURSE") != std::string::npos ||
                u.find("COURSE_B") != std::string::npos ||
                u.find("COURSEB") != std::string::npos ||
                u.find("B_COURSE") != std::string::npos ||
                u.find("_B") != std::string::npos ||
                u.find("\\B\\") != std::string::npos ||
                u.find("/B/") != std::string::npos;
        }

        bool looksLikeBCourseLayout(const std::vector<TrafficSignalGroup>& groups,
            const std::vector<TrafficSignal>& signals) {
            return groups.size() >= 14 || signals.size() >= 24;
        }

        float getCycleTimeFast(const TrafficSignalGroup& group) {
            float total = 0.0f;
            for (size_t i = 0; i < group.phaseDurations.size(); ++i) {
                if (group.phaseDurations[i] > 0.0f) {
                    total += group.phaseDurations[i];
                }
            }
            return total;
        }

        Vec2 getAnchorFast(const TrafficSignalGroup& group,
            const std::vector<TrafficSignal>& signals,
            const std::unordered_map<int, size_t>& signalIndexById) {
            float sx = 0.0f;
            float sy = 0.0f;
            int count = 0;

            if (!group.bindings.empty()) {
                for (size_t i = 0; i < group.bindings.size(); ++i) {
                    std::unordered_map<int, size_t>::const_iterator it =
                        signalIndexById.find(group.bindings[i].signalId);
                    if (it == signalIndexById.end()) continue;
                    sx += signals[it->second].position.x;
                    sy += signals[it->second].position.y;
                    ++count;
                }
            }
            else {
                for (size_t i = 0; i < group.signalIds.size(); ++i) {
                    std::unordered_map<int, size_t>::const_iterator it =
                        signalIndexById.find(group.signalIds[i]);
                    if (it == signalIndexById.end()) continue;
                    sx += signals[it->second].position.x;
                    sy += signals[it->second].position.y;
                    ++count;
                }
            }

            if (count <= 0) return Vec2(0.0f, 0.0f);
            return Vec2(sx / (float)count, sy / (float)count);
        }

        float computeBCourseOffsetForGroup(const TrafficSignalGroup& group,
            const std::vector<TrafficSignal>& signals,
            const std::unordered_map<int, size_t>& signalIndexById) {
            float cycle = getCycleTimeFast(group);
            if (cycle <= 0.0f) return 0.0f;

            Vec2 anchor = getAnchorFast(group, signals, signalIndexById);

            int gridX = (int)(anchor.x / 14.0f);
            int gridY = (int)(anchor.y / 14.0f);

            int px = ((gridX % 2) + 2) % 2;
            int py = ((gridY % 2) + 2) % 2;
            int bucket = py * 2 + px;

            float offset = cycle * 0.25f * (float)bucket;

            while (offset >= cycle) offset -= cycle;
            while (offset < 0.0f) offset += cycle;

            return offset;
        }
    }

    void TrafficSystem::init(const std::vector<TrafficSignal>& raw, const std::string& cfg) {
        clear();

        signals_ = raw;
        rebuildSignalIndex();
        loadGroupConfig(cfg);

        if (groups_.empty()) {
            for (size_t i = 0; i < signals_.size(); i++) {
                TrafficSignalGroup g;
                g.groupId = signals_[i].id;
                g.signalIds.push_back(signals_[i].id);
                g.phaseSequence.push_back(SignalPhase::MainGreen);
                g.phaseDurations.push_back(999999.0f);
                g.currentPhaseIndex = 0;
                g.phaseTimer = g.phaseDurations.front();
                groups_.push_back(g);
            }
        }

        rebuildGroupIndex();
        autoAssignGroups();
        autoAssignRoles();
        syncBindingsFromMaps();

        bool useBCourseTiming = isBCourseConfig(cfg) ||
            looksLikeBCourseLayout(groups_, signals_);

        if (useBCourseTiming) {
            for (size_t i = 0; i < groups_.size(); ++i) {
                float offset = computeBCourseOffsetForGroup(groups_[i], signals_, signalIndexById_);
                advanceGroupByOffset(groups_[i], offset);
            }
        }
        else {
            applyInitialPhaseOffsets();
        }

        applyAllCurrentPhases();
    }

    void TrafficSystem::init(const std::vector<TrafficSignal>& raw, const std::vector<TrafficSignalGroup>& grps) {
        clear();

        signals_ = raw;
        groups_ = grps;

        rebuildSignalIndex();
        rebuildGroupIndex();
        autoAssignGroups();
        autoAssignRoles();
        syncBindingsFromMaps();

        bool useBCourseTiming = looksLikeBCourseLayout(groups_, signals_);

        if (useBCourseTiming) {
            for (size_t i = 0; i < groups_.size(); ++i) {
                float offset = computeBCourseOffsetForGroup(groups_[i], signals_, signalIndexById_);
                advanceGroupByOffset(groups_[i], offset);
            }
        }
        else {
            applyInitialPhaseOffsets();
        }

        applyAllCurrentPhases();
    }

    void TrafficSystem::update(float dt) {
        if (dt <= 0.0f) return;

        for (size_t gi = 0; gi < groups_.size(); gi++) {
            TrafficSignalGroup& g = groups_[gi];
            if (g.phaseSequence.empty() || g.phaseDurations.empty()) continue;

            if (g.currentPhaseIndex < 0 || g.currentPhaseIndex >= (int)g.phaseSequence.size()) {
                g.currentPhaseIndex = 0;
            }

            if (g.phaseTimer <= 0.0f) {
                g.phaseTimer = g.phaseDurations[g.currentPhaseIndex];
                if (g.phaseTimer <= 0.0f) g.phaseTimer = 0.001f;
            }

            g.phaseTimer -= dt;

            while (g.phaseTimer <= 0.0f) {
                float overflow = -g.phaseTimer;
                g.currentPhaseIndex = (g.currentPhaseIndex + 1) % (int)g.phaseSequence.size();
                g.phaseTimer = g.phaseDurations[g.currentPhaseIndex] - overflow;
                if (g.phaseTimer <= 0.0f) g.phaseTimer = 0.001f;
                applyPhaseToSignals(g);
            }
        }
    }

    const std::vector<TrafficSignal>& TrafficSystem::getSignals() const {
        return signals_;
    }

    const std::vector<TrafficSignalGroup>& TrafficSystem::getGroups() const {
        return groups_;
    }

    //지워도 되는것
    TrafficLight TrafficSystem::getSignalAt(Vec2 pos, float maxDist) const {
        float minD = maxDist;
        TrafficLight nearest = TrafficLight::Green;

        for (size_t i = 0; i < signals_.size(); i++) {
            float d = (signals_[i].position - pos).length();
            if (d < minD) {
                minD = d;
                nearest = signals_[i].state;
            }
        }

        return nearest;
    }

    float TrafficSystem::getRemainingTime(int sid) const {
        std::unordered_map<int, int>::const_iterator it = signalToGroup_.find(sid);
        if (it == signalToGroup_.end()) return 0.0f;

        std::unordered_map<int, size_t>::const_iterator git = groupIndexById_.find(it->second);
        if (git == groupIndexById_.end()) return 0.0f;

        return groups_[git->second].phaseTimer;
    }

    //지워도 댐
    bool TrafficSystem::isPedestrianWalkActive(int gid) const {
        std::unordered_map<int, size_t>::const_iterator it = groupIndexById_.find(gid);
        if (it == groupIndexById_.end()) return false;
        return groups_[it->second].pedestrianWalk;
    }

    //지워도 댐
    bool TrafficSystem::tryGetSignal(int sid, TrafficSignal& out) const {
        std::unordered_map<int, size_t>::const_iterator it = signalIndexById_.find(sid);
        if (it == signalIndexById_.end()) return false;
        out = signals_[it->second];
        return true;
    }

    //지워도댐
    int TrafficSystem::getGroupIdForSignal(int sid) const {
        std::unordered_map<int, int>::const_iterator it = signalToGroup_.find(sid);
        if (it == signalToGroup_.end()) return -1;
        return it->second;
    }

    void TrafficSystem::bindSignalToGroup(int signalId, int groupId) {
        bindSignalToGroup(signalId, groupId, getSignalRole(signalId));
    }

    void TrafficSystem::bindSignalToGroup(int signalId, int groupId, SignalRole role) {
        std::unordered_map<int, size_t>::const_iterator sigIt = signalIndexById_.find(signalId);
        std::unordered_map<int, size_t>::const_iterator groupIt = groupIndexById_.find(groupId);
        if (sigIt == signalIndexById_.end() || groupIt == groupIndexById_.end()) return;

        std::unordered_map<int, int>::iterator prev = signalToGroup_.find(signalId);
        if (prev != signalToGroup_.end()) {
            std::unordered_map<int, size_t>::const_iterator oldGit = groupIndexById_.find(prev->second);
            if (oldGit != groupIndexById_.end()) {
                TrafficSignalGroup& oldGroup = groups_[oldGit->second];

                oldGroup.signalIds.erase(
                    std::remove(oldGroup.signalIds.begin(), oldGroup.signalIds.end(), signalId),
                    oldGroup.signalIds.end()
                );

                oldGroup.bindings.erase(
                    std::remove_if(oldGroup.bindings.begin(), oldGroup.bindings.end(),
                        [&](const TrafficSignalBinding& b) { return b.signalId == signalId; }),
                    oldGroup.bindings.end()
                );
            }
        }

        signalToGroup_[signalId] = groupId;
        signalRoleById_[signalId] = role;

        TrafficSignalGroup& group = groups_[groupIt->second];

        if (std::find(group.signalIds.begin(), group.signalIds.end(), signalId) == group.signalIds.end()) {
            group.signalIds.push_back(signalId);
        }

        bool foundBinding = false;
        for (size_t i = 0; i < group.bindings.size(); ++i) {
            if (group.bindings[i].signalId == signalId) {
                group.bindings[i].role = role;
                foundBinding = true;
                break;
            }
        }
        if (!foundBinding) {
            group.bindings.push_back(TrafficSignalBinding(signalId, role));
        }

        applyPhaseToSignals(group);
    }

    void TrafficSystem::setSignalRole(int signalId, SignalRole role) {
        signalRoleById_[signalId] = role;

        std::unordered_map<int, int>::const_iterator gt = signalToGroup_.find(signalId);
        if (gt == signalToGroup_.end()) return;

        std::unordered_map<int, size_t>::const_iterator git = groupIndexById_.find(gt->second);
        if (git == groupIndexById_.end()) return;

        TrafficSignalGroup& g = groups_[git->second];
        bool found = false;

        for (size_t i = 0; i < g.bindings.size(); ++i) {
            if (g.bindings[i].signalId == signalId) {
                g.bindings[i].role = role;
                found = true;
                break;
            }
        }

        if (!found) {
            g.bindings.push_back(TrafficSignalBinding(signalId, role));
        }

        applyPhaseToSignals(g);
    }

    SignalRole TrafficSystem::getSignalRole(int signalId) const {
        std::unordered_map<int, SignalRole>::const_iterator it = signalRoleById_.find(signalId);
        if (it == signalRoleById_.end()) return SignalRole::AllVehicle;
        return it->second;
    }

    void TrafficSystem::clear() {
        signals_.clear();
        groups_.clear();
        signalIndexById_.clear();
        groupIndexById_.clear();
        signalToGroup_.clear();
        signalRoleById_.clear();
    }

    void TrafficSystem::rebuildSignalIndex() {
        signalIndexById_.clear();
        for (size_t i = 0; i < signals_.size(); i++) {
            signalIndexById_[signals_[i].id] = i;
        }
    }

    void TrafficSystem::rebuildGroupIndex() {
        groupIndexById_.clear();
        for (size_t i = 0; i < groups_.size(); i++) {
            groupIndexById_[groups_[i].groupId] = i;
        }
    }

    void TrafficSystem::loadGroupConfig(const std::string& fp) {
        if (fp.empty()) return;

        std::ifstream fin(fp.c_str());
        if (!fin.is_open()) return;

        std::string line;
        while (std::getline(fin, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            std::vector<std::string> fields = split(line, ',');
            if (fields.empty()) continue;

            TrafficSignalGroup g;
            g.groupId = atoi(trim(fields[0]).c_str());

            std::vector<int> pendingIds;
            std::vector<SignalRole> pendingRoles;

            for (size_t i = 1; i < fields.size(); i++) {
                std::string f = trim(fields[i]);
                if (f.empty()) continue;

                if (startsWith(f, "ids=")) {
                    std::string ip = f.substr(4);
                    for (size_t c = 0; c < ip.size(); c++) {
                        if (ip[c] == '|' || ip[c] == ';') ip[c] = ' ';
                    }

                    std::stringstream ids(ip);
                    int id;
                    while (ids >> id) {
                        pendingIds.push_back(id);
                        g.signalIds.push_back(id);
                    }
                    continue;
                }

                if (startsWith(f, "roles=")) {
                    std::string rp = f.substr(6);
                    std::vector<std::string> roleTokens = split(rp, '|');
                    for (size_t k = 0; k < roleTokens.size(); ++k) {
                        pendingRoles.push_back(parseRoleToken(trim(roleTokens[k])));
                    }
                    continue;
                }

                std::stringstream ts(f);
                std::string tok;
                while (ts >> tok) {
                    size_t pos = tok.find(':');
                    if (pos == std::string::npos) continue;

                    std::string pn = trim(tok.substr(0, pos));
                    std::string dur = trim(tok.substr(pos + 1));
                    if (pn.empty() || dur.empty()) continue;

                    g.phaseSequence.push_back(parsePhaseToken(pn));
                    g.phaseDurations.push_back((float)atof(dur.c_str()));
                }
            }

            if (g.phaseSequence.empty()) {
                g.phaseSequence.push_back(SignalPhase::MainGreen);
                g.phaseDurations.push_back(999999.0f);
            }

            for (size_t bi = 0; bi < pendingIds.size(); ++bi) {
                SignalRole role = SignalRole::AllVehicle;
                if (bi < pendingRoles.size()) role = pendingRoles[bi];
                g.bindings.push_back(TrafficSignalBinding(pendingIds[bi], role));
            }

            g.currentPhaseIndex = 0;
            g.phaseTimer = g.phaseDurations.front();
            groups_.push_back(g);
        }
    }

    void TrafficSystem::autoAssignGroups() {
        for (size_t i = 0; i < groups_.size(); i++) {
            for (size_t j = 0; j < groups_[i].signalIds.size(); j++) {
                if (signalIndexById_.find(groups_[i].signalIds[j]) != signalIndexById_.end()) {
                    signalToGroup_[groups_[i].signalIds[j]] = groups_[i].groupId;
                }
            }
            for (size_t j = 0; j < groups_[i].bindings.size(); ++j) {
                if (signalIndexById_.find(groups_[i].bindings[j].signalId) != signalIndexById_.end()) {
                    signalToGroup_[groups_[i].bindings[j].signalId] = groups_[i].groupId;
                    signalRoleById_[groups_[i].bindings[j].signalId] = groups_[i].bindings[j].role;
                }
            }
        }

        for (size_t i = 0; i < groups_.size(); i++) {
            if (!groups_[i].signalIds.empty()) continue;

            bool matched = false;
            for (size_t j = 0; j < signals_.size(); j++) {
                if (signals_[j].id == groups_[i].groupId) {
                    groups_[i].signalIds.push_back(signals_[j].id);
                    signalToGroup_[signals_[j].id] = groups_[i].groupId;
                    matched = true;
                }
            }

            if (!matched && i < signals_.size()) {
                groups_[i].signalIds.push_back(signals_[i].id);
                signalToGroup_[signals_[i].id] = groups_[i].groupId;
            }
        }

        if (groups_.size() == 1) {
            for (size_t i = 0; i < signals_.size(); i++) {
                bindSignalToGroup(signals_[i].id, groups_.front().groupId);
            }
            return;
        }

        for (size_t i = 0; i < signals_.size(); i++) {
            if (signalToGroup_.find(signals_[i].id) != signalToGroup_.end()) continue;
            size_t gi = i < groups_.size() ? i : groups_.size() - 1;
            bindSignalToGroup(signals_[i].id, groups_[gi].groupId);
        }
    }

    void TrafficSystem::autoAssignRoles() {
        for (size_t gi = 0; gi < groups_.size(); ++gi) {
            TrafficSignalGroup& g = groups_[gi];

            if (!g.bindings.empty()) {
                for (size_t bi = 0; bi < g.bindings.size(); ++bi) {
                    signalRoleById_[g.bindings[bi].signalId] = g.bindings[bi].role;
                }
                continue;
            }

            for (size_t si = 0; si < g.signalIds.size(); ++si) {
                int sid = g.signalIds[si];

                SignalRole role = SignalRole::AllVehicle;
                if (g.signalIds.size() >= 3) {
                    if (si == 0) role = SignalRole::MainStraight;
                    else if (si == 1) role = SignalRole::LeftTurn;
                    else if (si == 2) role = SignalRole::Pedestrian;
                    else role = SignalRole::AllVehicle;
                }
                else if (g.signalIds.size() == 2) {
                    if (si == 0) role = SignalRole::MainStraight;
                    else role = SignalRole::LeftTurn;
                }
                else {
                    role = SignalRole::AllVehicle;
                }

                signalRoleById_[sid] = role;
            }
        }
    }

    void TrafficSystem::syncBindingsFromMaps() {
        for (size_t gi = 0; gi < groups_.size(); ++gi) {
            TrafficSignalGroup& g = groups_[gi];
            g.bindings.clear();

            for (size_t si = 0; si < g.signalIds.size(); ++si) {
                int sid = g.signalIds[si];
                SignalRole role = getSignalRole(sid);
                g.bindings.push_back(TrafficSignalBinding(sid, role));
            }
        }
    }

    float TrafficSystem::getTotalCycleTime(const TrafficSignalGroup& group) const {
        float total = 0.0f;
        for (size_t i = 0; i < group.phaseDurations.size(); ++i) {
            if (group.phaseDurations[i] > 0.0f) {
                total += group.phaseDurations[i];
            }
        }
        return total;
    }

    Vec2 TrafficSystem::computeGroupAnchor(const TrafficSignalGroup& group) const {
        float sx = 0.0f;
        float sy = 0.0f;
        int count = 0;

        if (!group.bindings.empty()) {
            for (size_t i = 0; i < group.bindings.size(); ++i) {
                std::unordered_map<int, size_t>::const_iterator it = signalIndexById_.find(group.bindings[i].signalId);
                if (it == signalIndexById_.end()) continue;
                sx += signals_[it->second].position.x;
                sy += signals_[it->second].position.y;
                ++count;
            }
        }
        else {
            for (size_t i = 0; i < group.signalIds.size(); ++i) {
                std::unordered_map<int, size_t>::const_iterator it = signalIndexById_.find(group.signalIds[i]);
                if (it == signalIndexById_.end()) continue;
                sx += signals_[it->second].position.x;
                sy += signals_[it->second].position.y;
                ++count;
            }
        }

        if (count <= 0) return Vec2(0.0f, 0.0f);
        return Vec2(sx / (float)count, sy / (float)count);
    }

    float TrafficSystem::computeInitialOffsetForGroup(const TrafficSignalGroup& group) const {
        float cycle = getTotalCycleTime(group);
        if (cycle <= 0.0f) return 0.0f;

        Vec2 anchor = computeGroupAnchor(group);

        int gridX = (int)(anchor.x / 14.0f);
        int gridY = (int)(anchor.y / 14.0f);

        float corridorWave = (float)gridX * 2.75f;
        float crossDelay = (float)(gridY % 3) * 1.35f;
        float alternation = ((gridX + gridY) % 2 == 0) ? 0.0f : 4.0f;
        float jitter = (float)((group.groupId * 37) % 7) * 0.17f;

        float offset = corridorWave + crossDelay + alternation + jitter;

        while (offset >= cycle) offset -= cycle;
        while (offset < 0.0f) offset += cycle;

        return offset;
    }

    void TrafficSystem::advanceGroupByOffset(TrafficSignalGroup& group, float offsetSeconds) {
        if (group.phaseSequence.empty() || group.phaseDurations.empty()) return;

        float cycle = getTotalCycleTime(group);
        if (cycle <= 0.0f) {
            group.currentPhaseIndex = 0;
            group.phaseTimer = group.phaseDurations[0] > 0.0f ? group.phaseDurations[0] : 0.001f;
            applyPhaseToSignals(group);
            return;
        }

        while (offsetSeconds >= cycle) offsetSeconds -= cycle;
        while (offsetSeconds < 0.0f) offsetSeconds += cycle;

        int phaseIndex = 0;

        while (true) {
            float dur = group.phaseDurations[phaseIndex];
            if (dur <= 0.0f) dur = 0.001f;

            if (offsetSeconds < dur) {
                group.currentPhaseIndex = phaseIndex;
                group.phaseTimer = dur - offsetSeconds;
                if (group.phaseTimer <= 0.0f) group.phaseTimer = 0.001f;
                break;
            }

            offsetSeconds -= dur;
            phaseIndex = (phaseIndex + 1) % (int)group.phaseDurations.size();
        }

        applyPhaseToSignals(group);
    }

    void TrafficSystem::applyInitialPhaseOffsets() {
        for (size_t i = 0; i < groups_.size(); ++i) {
            float offset = computeInitialOffsetForGroup(groups_[i]);
            advanceGroupByOffset(groups_[i], offset);
        }
    }

    void TrafficSystem::applyAllCurrentPhases() {
        for (size_t i = 0; i < groups_.size(); i++) {
            if (groups_[i].phaseSequence.empty()) continue;

            if (groups_[i].currentPhaseIndex < 0 || groups_[i].currentPhaseIndex >= (int)groups_[i].phaseSequence.size()) {
                groups_[i].currentPhaseIndex = 0;
            }

            if (groups_[i].phaseTimer <= 0.0f && !groups_[i].phaseDurations.empty()) {
                groups_[i].phaseTimer = groups_[i].phaseDurations[groups_[i].currentPhaseIndex];
                if (groups_[i].phaseTimer <= 0.0f) groups_[i].phaseTimer = 0.001f;
            }

            applyPhaseToSignals(groups_[i]);
        }
    }

    void TrafficSystem::applyPhaseToSignals(TrafficSignalGroup& g) {
        if (g.phaseSequence.empty()) return;

        SignalPhase ph = g.phaseSequence[g.currentPhaseIndex];
        g.pedestrianWalk = (ph == SignalPhase::PedCrossing);

        for (size_t i = 0; i < g.bindings.size(); ++i) {
            std::unordered_map<int, size_t>::const_iterator it = signalIndexById_.find(g.bindings[i].signalId);
            if (it == signalIndexById_.end()) continue;

            TrafficSignal& sig = signals_[it->second];
            sig.state = resolveLightForRole(ph, g.bindings[i].role);
        }

        if (g.bindings.empty()) {
            for (size_t i = 0; i < g.signalIds.size(); ++i) {
                std::unordered_map<int, size_t>::const_iterator it = signalIndexById_.find(g.signalIds[i]);
                if (it == signalIndexById_.end()) continue;

                TrafficSignal& sig = signals_[it->second];
                sig.state = resolveLightForRole(ph, SignalRole::AllVehicle);
            }
        }
    }

    SignalPhase TrafficSystem::parsePhaseToken(const std::string& token) {
        std::string t = toUpperCopy(trim(token));

        if (t == "G" || t == "MG" || t == "MAINGREEN") return SignalPhase::MainGreen;
        if (t == "Y" || t == "MY" || t == "MAINYELLOW") return SignalPhase::MainYellow;
        if (t == "L" || t == "LG" || t == "LEFT" || t == "LEFTTURNGREEN") return SignalPhase::LeftTurnGreen;
        if (t == "LY" || t == "LEFTYELLOW" || t == "LEFTTURNYELLOW") return SignalPhase::LeftTurnYellow;
        if (t == "R" || t == "AR" || t == "ALLRED") return SignalPhase::AllRed;
        if (t == "P" || t == "PED" || t == "PEDCROSSING") return SignalPhase::PedCrossing;

        return SignalPhase::MainGreen;
    }

    SignalRole TrafficSystem::parseRoleToken(const std::string& token) {
        std::string t = toUpperCopy(trim(token));

        if (t == "MAIN" || t == "STRAIGHT" || t == "MAINSTRAIGHT") return SignalRole::MainStraight;
        if (t == "LEFT" || t == "LEFTTURN") return SignalRole::LeftTurn;
        if (t == "PED" || t == "PEDESTRIAN") return SignalRole::Pedestrian;
        if (t == "ALL" || t == "ALLVEHICLE") return SignalRole::AllVehicle;

        return SignalRole::AllVehicle;
    }

    TrafficLight TrafficSystem::resolveLightForRole(SignalPhase phase, SignalRole role) {
        switch (phase) {
        case SignalPhase::MainGreen:
            if (role == SignalRole::MainStraight) return TrafficLight::Green;
            if (role == SignalRole::LeftTurn) return TrafficLight::Red;
            if (role == SignalRole::Pedestrian) return TrafficLight::Red;
            return TrafficLight::Green;

        case SignalPhase::MainYellow:
            if (role == SignalRole::MainStraight) return TrafficLight::Yellow;
            if (role == SignalRole::LeftTurn) return TrafficLight::Red;
            if (role == SignalRole::Pedestrian) return TrafficLight::Red;
            return TrafficLight::Yellow;

        case SignalPhase::LeftTurnGreen:
            if (role == SignalRole::MainStraight) return TrafficLight::Red;
            if (role == SignalRole::LeftTurn) return TrafficLight::LeftArrow;
            if (role == SignalRole::Pedestrian) return TrafficLight::Red;
            return TrafficLight::Red;

        case SignalPhase::LeftTurnYellow:
            if (role == SignalRole::MainStraight) return TrafficLight::Red;
            if (role == SignalRole::LeftTurn) return TrafficLight::Yellow;
            if (role == SignalRole::Pedestrian) return TrafficLight::Red;
            return TrafficLight::Yellow;

        case SignalPhase::AllRed:
            return TrafficLight::Red;

        case SignalPhase::PedCrossing:
            if (role == SignalRole::Pedestrian) return TrafficLight::Green;
            return TrafficLight::Red;
        }

        return TrafficLight::Red;
    }

} // namespace bestdriver