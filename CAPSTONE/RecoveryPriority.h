#ifndef RECOVERY_PRIORITY_H
#define RECOVERY_PRIORITY_H

#include <string>
#include <algorithm>
#include "../models/LostItem.h"

// RecoveryPriority
// ----------------
// Produces a 0-100 "Recovery Priority Score" for a lost item, used by
// admins to triage which cases need attention first. Factors:
//   - Best current match confidence (bestMatchScore, 0 if no match yet)
//   - Whether a unique identifying feature was provided
//   - Report freshness (days since reported)
//   - Location specificity (a precise sub-location scores higher than a vague one)
//   - Category importance (electronics/ID documents weighted higher than misc)
//   - Whether a corresponding found item exists at all (hasCandidateFound)
namespace RecoveryPriority {

    inline int categoryImportance(const std::string& category) {
        std::string c = category;
        std::transform(c.begin(), c.end(), c.begin(), ::tolower);
        if (c.find("electronic") != std::string::npos) return 100;
        if (c.find("id") != std::string::npos || c.find("document") != std::string::npos || c.find("card") != std::string::npos) return 100;
        if (c.find("wallet") != std::string::npos || c.find("jewel") != std::string::npos) return 90;
        if (c.find("bag") != std::string::npos || c.find("book") != std::string::npos) return 60;
        return 40;
    }

    inline int locationSpecificity(const std::string& location) {
        // A location with more than one word (e.g. "Library Reading Hall")
        // is treated as more specific than a bare zone name ("Library").
        int wordCount = 1;
        for (char c : location) if (c == ' ') wordCount++;
        if (location.empty()) return 0;
        return wordCount >= 2 ? 100 : 60;
    }

    inline int freshnessScore(int daysSinceReported) {
        if (daysSinceReported <= 1) return 100;
        if (daysSinceReported <= 3) return 80;
        if (daysSinceReported <= 7) return 55;
        if (daysSinceReported <= 14) return 30;
        return 10; // stale reports still matter, but less urgently
    }

    struct PriorityInput {
        int bestMatchScore = 0;       // 0-100
        bool hasUniqueFeature = false;
        int daysSinceReported = 0;
        std::string location;
        std::string category;
        bool hasCandidateFound = false;
    };

    inline int score(const PriorityInput& in) {
        double s = 0.0;
        s += in.bestMatchScore * 0.35;                                  // 35%
        s += (in.hasUniqueFeature ? 100.0 : 20.0) * 0.15;                // 15%
        s += freshnessScore(in.daysSinceReported) * 0.15;                // 15%
        s += locationSpecificity(in.location) * 0.10;                    // 10%
        s += categoryImportance(in.category) * 0.15;                     // 15%
        s += (in.hasCandidateFound ? 100.0 : 0.0) * 0.10;                 // 10%
        int result = static_cast<int>(s + 0.5);
        return std::min(100, std::max(0, result));
    }

    inline std::string label(int score) {
        if (score >= 75) return "HIGH";
        if (score >= 45) return "MEDIUM";
        return "LOW";
    }
}

#endif // RECOVERY_PRIORITY_H
