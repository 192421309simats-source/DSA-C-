#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "../models/LostItem.h"
#include "../models/FoundItem.h"
#include "../models/MatchResult.h"
#include "../utils/StringUtils.h"
#include "../utils/LocationGraph.h"
#include "../utils/ImageHash.h"

// MatchingEngine ("CampusMatch Engine")
// --------------------------------------
// Compares a lost item and a found item across 8 weighted attributes and
// produces an explainable MatchResult. Weights are configurable at runtime
// (loaded from data/weights.txt if present) so they can be tuned without
// recompiling, as required by the spec.
class MatchingEngine {
public:
    struct Weights {
        int name = 18;
        int category = 12;
        int color = 8;
        int brand = 12;
        int location = 12;
        int date = 8;
        int description = 8;
        int feature = 7;
        int image = 15; // real perceptual-hash photo comparison, when both sides have a photo

        int total() const { return name + category + color + brand + location + date + description + feature + image; }
    };

    explicit MatchingEngine(LocationGraph& locGraph) : locations(locGraph) {}

    Weights& weights() { return w; }
    const Weights& weights() const { return w; }

    // Core comparison: Lost <-> Found
    MatchResult compare(const LostItem& lost, const FoundItem& found) const {
        MatchResult result;
        result.sourceId = lost.itemId;
        result.targetId = found.itemId;

        addAttr(result, "Item Name", nameScore(lost.itemName, found.itemName), w.name);
        addAttr(result, "Category", categoricalScore(lost.category, found.category), w.category);
        addAttr(result, "Color", categoricalScore(lost.color, found.color), w.color);
        addAttr(result, "Brand", categoricalScore(lost.brand, found.brand), w.brand);
        addAttr(result, "Location", locationScore(lost.location, found.location), w.location);
        addAttr(result, "Date", dateScore(lost.dateText, found.dateText), w.date);
        addAttr(result, "Description", textScore(lost.description, found.description), w.description);
        addAttr(result, "Unique Feature", textScore(lost.uniqueFeature, found.uniqueFeature), w.feature);
        addImageAttr(result, lost.imageHash, found.imageHash);

        finalizeScore(result);
        return result;
    }

    // Lost <-> Lost comparison, used for duplicate-report detection.
    // Reuses the same attribute logic (location field compared the same way,
    // no "found storage location" concept involved).
    MatchResult compareLostToLost(const LostItem& a, const LostItem& b) const {
        MatchResult result;
        result.sourceId = a.itemId;
        result.targetId = b.itemId;

        addAttr(result, "Item Name", nameScore(a.itemName, b.itemName), w.name);
        addAttr(result, "Category", categoricalScore(a.category, b.category), w.category);
        addAttr(result, "Color", categoricalScore(a.color, b.color), w.color);
        addAttr(result, "Brand", categoricalScore(a.brand, b.brand), w.brand);
        addAttr(result, "Location", locationScore(a.location, b.location), w.location);
        addAttr(result, "Date", dateScore(a.dateText, b.dateText), w.date);
        addAttr(result, "Description", textScore(a.description, b.description), w.description);
        addAttr(result, "Unique Feature", textScore(a.uniqueFeature, b.uniqueFeature), w.feature);
        addImageAttr(result, a.imageHash, b.imageHash);

        finalizeScore(result);
        return result;
    }

    // Ranks every found item against one lost item, descending by score.
    std::vector<MatchResult> rankMatches(const LostItem& lost, const std::vector<FoundItem>& candidates) const {
        std::vector<MatchResult> results;
        results.reserve(candidates.size());
        for (auto& f : candidates) {
            // Only compare items that are still available to be matched.
            if (f.status == "RETURNED" || f.status == "CLOSED") continue;
            results.push_back(compare(lost, f));
        }
        std::sort(results.begin(), results.end(),
                  [](const MatchResult& x, const MatchResult& y) { return x.score > y.score; });
        return results;
    }

private:
    Weights w;
    LocationGraph& locations;

    static void addAttr(MatchResult& r, const std::string& name, double fraction01, int maxPoints) {
        fraction01 = std::max(0.0, std::min(1.0, fraction01));
        int scored = static_cast<int>(std::round(fraction01 * maxPoints));
        AttributeScore a;
        a.attribute = name;
        a.scored = scored;
        a.max = maxPoints;
        a.matched = fraction01 >= 0.6; // counts as a "hit" for the checklist
        r.breakdown.push_back(a);
    }

    // Adds the "Image" attribute using real perceptual-hash comparison
    // (ImageHash::similarity, Hamming distance over a 64-bit average hash)
    // when both items have an uploaded photo. If either side has no photo,
    // the attribute is omitted entirely rather than scored as a mismatch —
    // finalizeScore() then renormalizes the total so items without photos
    // are judged fairly on the attributes that were actually available.
    void addImageAttr(MatchResult& r, const std::string& hashA, const std::string& hashB) const {
        if (hashA.empty() || hashB.empty()) return; // no photo on one/both sides — skip, don't penalize
        double sim = ImageHash::similarity(ImageHash::fromHex(hashA), ImageHash::fromHex(hashB));
        addAttr(r, "Image", sim, w.image);
    }

    // Sums the scored attributes and, if the Image attribute was skipped
    // (no photo available), rescales the total up to a 0-100 scale based on
    // only the attributes that were actually compared.
    void finalizeScore(MatchResult& r) const {
        int total = 0, maxPossible = 0;
        bool hasImageAttr = false;
        for (auto& a : r.breakdown) {
            total += a.scored;
            maxPossible += a.max;
            if (a.attribute == "Image") hasImageAttr = true;
        }
        if (!hasImageAttr && maxPossible > 0 && maxPossible < 100) {
            total = static_cast<int>(std::round(total * (100.0 / maxPossible)));
        }
        r.score = std::min(100, std::max(0, total));
        r.confidence = MatchResult::confidenceForScore(r.score);
        buildReasons(r);
    }

    // 0.0 - 1.0 fraction helpers -------------------------------------------------

    static double nameScore(const std::string& a, const std::string& b) {
        return StringUtils::similarity(a, b);
    }

    static double categoricalScore(const std::string& a, const std::string& b) {
        // Category/Color/Brand: exact/case-insensitive gets full credit,
        // partial containment gets high partial credit, otherwise fall back
        // to general similarity (handles typos like "Samsng").
        if (StringUtils::exactMatch(a, b)) return 1.0;
        if (a.empty() || b.empty()) return 0.0;
        if (StringUtils::containsPartial(a, b)) return 0.85;
        return StringUtils::similarity(a, b) * 0.7; // discount pure fuzzy text for categorical fields
    }

    double locationScore(const std::string& a, const std::string& b) const {
        return locations.scoreLocations(a, b) / 100.0;
    }

    static double dateScore(const std::string& a, const std::string& b) {
        if (a.empty() || b.empty()) return 0.0;
        if (a == b) return 1.0;
        int daysApart = std::abs(daysSinceEpochApprox(a) - daysSinceEpochApprox(b));
        if (daysApart <= 0) return 1.0;
        if (daysApart == 1) return 0.7;   // reported a day apart is still plausible
        if (daysApart <= 3) return 0.4;
        if (daysApart <= 7) return 0.15;
        return 0.0;
    }

    // Expects DD-MM-YYYY. Returns an approximate day count for delta
    // calculations only (not calendar-accurate for leap years, which is
    // acceptable for "how many days apart" scoring).
    static int daysSinceEpochApprox(const std::string& dateText) {
        int d = 1, m = 1, y = 2026;
        if (dateText.size() >= 10 && dateText[2] == '-' && dateText[5] == '-') {
            try {
                d = std::stoi(dateText.substr(0, 2));
                m = std::stoi(dateText.substr(3, 2));
                y = std::stoi(dateText.substr(6, 4));
            } catch (...) { /* fall back to defaults on parse failure */ }
        }
        return y * 372 + m * 31 + d; // 31-day months is fine for a relative delta
    }

    static double textScore(const std::string& a, const std::string& b) {
        return StringUtils::similarity(a, b);
    }

    static void buildReasons(MatchResult& r) {
        for (auto& a : r.breakdown) {
            if (a.matched) {
                r.reasons.push_back("Match on " + a.attribute);
            }
        }
    }
};

#endif // MATCHING_ENGINE_H
