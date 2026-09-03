#ifndef LOCATION_GRAPH_H
#define LOCATION_GRAPH_H

#include <string>
#include <map>
#include <vector>
#include "StringUtils.h"

// LocationGraph
// -------------
// Models the campus as a set of parent zones, each containing sub-locations.
// This lets the matching engine understand that "Library" and
// "Library Entrance" are related, even though the text strings differ.
// New campus locations/zones can be registered at runtime via addLocation,
// so the system is not hard-coded to a single example campus.
class LocationGraph {
public:
    LocationGraph() {
        seedDefaultCampus();
    }

    // Registers a sub-location under a parent zone, e.g.
    // addLocation("Library", "Reading Hall")
    void addLocation(const std::string& zone, const std::string& subLocation) {
        std::string z = StringUtils::normalize(zone);
        zoneOf[StringUtils::normalize(subLocation)] = z;
        zones[z].push_back(subLocation);
        // the zone itself is also a valid location belonging to itself
        zoneOf[z] = z;
    }

    // Returns 0-100 location match score between two free-text location
    // strings entered by users.
    int scoreLocations(const std::string& locA, const std::string& locB) const {
        std::string a = StringUtils::normalize(locA);
        std::string b = StringUtils::normalize(locB);
        if (a.empty() || b.empty()) return 0;

        if (a == b) return 100; // same location string

        std::string zoneA = resolveZone(a);
        std::string zoneB = resolveZone(b);

        if (!zoneA.empty() && zoneA == zoneB) {
            // same parent zone, different sub-location -> "related"
            return 70;
        }

        // textual containment, e.g. "library" is contained in "library entrance"
        if (StringUtils::containsPartial(a, b)) return 70;

        // fall back to generic text similarity for "nearby" wording,
        // e.g. "near library" vs "library gate"
        double sim = StringUtils::similarity(a, b);
        if (sim >= 0.5) return 50;      // nearby / loosely related
        if (sim >= 0.25) return 20;     // weak relation
        return 0;                        // different location
    }

    std::map<std::string, std::vector<std::string>> allZones() const {
        return zones;
    }

private:
    // sub-location(normalized) -> zone(normalized)
    std::map<std::string, std::string> zoneOf;
    // zone(normalized) -> list of raw sub-location names (for display/admin use)
    std::map<std::string, std::vector<std::string>> zones;

    std::string resolveZone(const std::string& normalizedLocation) const {
        auto it = zoneOf.find(normalizedLocation);
        if (it != zoneOf.end()) return it->second;
        return "";
    }

    void seedDefaultCampus() {
        addLocation("Academic Block", "Ground Floor");
        addLocation("Academic Block", "First Floor");
        addLocation("Academic Block", "Second Floor");

        addLocation("Library", "Entrance");
        addLocation("Library", "Reading Hall");
        addLocation("Library", "First Floor");

        addLocation("Hostel", "Main Gate");
        addLocation("Hostel", "Block A");
        addLocation("Hostel", "Block B");

        addLocation("Cafeteria", "Cafeteria");
        addLocation("Parking Area", "Parking Area");
        addLocation("Auditorium", "Auditorium");
        addLocation("Sports Ground", "Sports Ground");
        addLocation("Bus Stop", "Bus Stop");
        addLocation("Main Gate", "Main Gate");
        addLocation("Laboratory", "Laboratory");
        addLocation("Administrative Block", "Administrative Block");
    }
};

#endif // LOCATION_GRAPH_H
