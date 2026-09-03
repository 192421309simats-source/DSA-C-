#ifndef MATCH_RESULT_H
#define MATCH_RESULT_H

#include <string>
#include <vector>

struct AttributeScore {
    std::string attribute;
    int scored;   // points earned
    int max;      // points possible for this attribute
    bool matched; // whether it counts as a "hit" for the checklist explanation
};

// Result of comparing one Lost item against one Found item (or, for
// duplicate detection, one Lost item against another Lost item).
class MatchResult {
public:
    std::string sourceId; // e.g. lostId
    std::string targetId; // e.g. foundId (or another lostId for duplicates)
    int score = 0;         // 0-100
    std::string confidence; // VERY STRONG MATCH / STRONG MATCH / ...
    std::vector<AttributeScore> breakdown;
    std::vector<std::string> reasons; // human-readable checklist lines

    static std::string confidenceForScore(int score) {
        if (score >= 90) return "VERY STRONG MATCH";
        if (score >= 75) return "STRONG MATCH";
        if (score >= 60) return "POSSIBLE MATCH";
        if (score >= 40) return "WEAK MATCH";
        return "UNLIKELY MATCH";
    }
};

#endif // MATCH_RESULT_H
