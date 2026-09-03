#ifndef VERIFICATION_ENGINE_H
#define VERIFICATION_ENGINE_H

#include <string>
#include <map>
#include "../models/LostItem.h"
#include "../models/FoundItem.h"
#include "../utils/StringUtils.h"
#include "../utils/LocationGraph.h"

// VerificationEngine ("OwnerVerify")
// -----------------------------------
// Separate from CampusMatch. A high item-attribute match score only means
// two *reports* look alike - it says nothing about whether the person
// filing the claim actually owns the item. OwnerVerify instead scores how
// well the claimant's own answers (given blind, without seeing the found
// item's public listing) line up with the found item's real record.
//
// Weighting (spec section 19):
//   Item Details          30%
//   Location Knowledge    20%
//   Time/Date Knowledge   15%
//   Unique Feature        25%
//   User Information      10%
class VerificationEngine {
public:
    struct Weights {
        int itemDetails = 30;
        int locationKnowledge = 20;
        int timeDateKnowledge = 15;
        int uniqueFeature = 25;
        int userInformation = 10;
    };

    explicit VerificationEngine(LocationGraph& locGraph) : locations(locGraph) {}

    Weights& weights() { return w; }

    // answers keys expected (any missing key scores 0 for its component):
    //   "itemDetails"        - claimant's free-text description of the item
    //   "location"           - claimant's stated lost/found location
    //   "timeDate"           - claimant's stated approximate time/date
    //   "uniqueFeature"      - claimant's stated unique identifying feature
    //   "userInfo"           - claimant's supporting personal/contextual info
    struct VerificationResult {
        int score = 0;
        std::string decision; // RECOMMENDED APPROVAL / MANUAL REVIEW / LOW CONFIDENCE
        std::map<std::string, int> componentScores; // out of each component's weight
    };

    VerificationResult evaluate(const FoundItem& found,
                                 const std::map<std::string, std::string>& answers) const {
        VerificationResult r;

        double itemDetailsFrac = fieldFraction(answers, "itemDetails",
            found.description + " " + found.itemName + " " + found.category + " " + found.brand + " " + found.color);
        double locationFrac = locations.scoreLocations(getOr(answers, "location"), found.location) / 100.0;
        double timeFrac = timeDateFraction(getOr(answers, "timeDate"), found.dateText, found.timeText);
        double featureFrac = StringUtils::similarity(getOr(answers, "uniqueFeature"), found.uniqueFeature);
        double userInfoFrac = getOr(answers, "userInfo").empty() ? 0.0 : 0.8; // presence + plausibility proxy

        int itemDetailsPts = static_cast<int>(std::round(itemDetailsFrac * w.itemDetails));
        int locationPts = static_cast<int>(std::round(locationFrac * w.locationKnowledge));
        int timeDatePts = static_cast<int>(std::round(timeFrac * w.timeDateKnowledge));
        int featurePts = static_cast<int>(std::round(featureFrac * w.uniqueFeature));
        int userInfoPts = static_cast<int>(std::round(userInfoFrac * w.userInformation));

        r.componentScores["itemDetails"] = itemDetailsPts;
        r.componentScores["location"] = locationPts;
        r.componentScores["timeDate"] = timeDatePts;
        r.componentScores["uniqueFeature"] = featurePts;
        r.componentScores["userInfo"] = userInfoPts;

        r.score = std::min(100, itemDetailsPts + locationPts + timeDatePts + featurePts + userInfoPts);
        r.decision = decisionForScore(r.score);
        return r;
    }

    static std::string decisionForScore(int score) {
        if (score >= 80) return "RECOMMENDED APPROVAL";
        if (score >= 60) return "MANUAL REVIEW";
        return "LOW CONFIDENCE";
    }

private:
    Weights w;
    LocationGraph& locations;

    static std::string getOr(const std::map<std::string, std::string>& m, const std::string& key) {
        auto it = m.find(key);
        return it == m.end() ? "" : it->second;
    }

    static double fieldFraction(const std::map<std::string, std::string>& answers,
                                 const std::string& key, const std::string& reference) {
        std::string val = getOr(answers, key);
        if (val.empty()) return 0.0;
        return StringUtils::similarity(val, reference);
    }

    static double timeDateFraction(const std::string& claimantTimeDate,
                                    const std::string& actualDate, const std::string& actualTime) {
        if (claimantTimeDate.empty()) return 0.0;
        std::string combinedActual = actualDate + " " + actualTime;
        return StringUtils::similarity(claimantTimeDate, combinedActual);
    }
};

#endif // VERIFICATION_ENGINE_H
