#pragma once
#ifndef BESTDRIVER_EXAM_RESULT_H
#define BESTDRIVER_EXAM_RESULT_H

#include "common_types.h"
#include <string>
#include <vector>

namespace bestdriver {

    struct ExamResult {
        bool disqualified = false;
        bool passed = false;
        int totalPenalty = 0;
        int finalScore = 0;
        std::string finalStatus;
        std::string disqualifyReason;
    };

    inline bool isDisqualifyingPenalty(const Penalty& p) {
        return p.points >= 100;
    }

    inline ExamResult buildExamResult(const std::vector<Penalty>& penalties) {
        ExamResult result;

        for (const auto& p : penalties) {
            if (isDisqualifyingPenalty(p)) {
                if (!result.disqualified) {
                    result.disqualified = true;
                    result.disqualifyReason = p.description;
                }
            }
            else {
                result.totalPenalty += p.points;
            }
        }

        result.finalScore = result.disqualified ? 0 : (100 - result.totalPenalty);
        if (result.finalScore < 0) {
            result.finalScore = 0;
        }

        result.passed = (!result.disqualified && result.finalScore >= 70);

        if (result.disqualified) {
            result.finalStatus = "DISQUALIFIED";
        }
        else if (result.passed) {
            result.finalStatus = "PASS";
        }
        else {
            result.finalStatus = "FAIL";
        }

        return result;
    }

} // namespace bestdriver

#endif