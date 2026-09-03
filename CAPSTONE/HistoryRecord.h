#ifndef HISTORY_RECORD_H
#define HISTORY_RECORD_H

#include <string>

class HistoryRecord {
public:
    std::string recordId;
    std::string itemId;      // lost or found item id this event belongs to
    std::string relatedId;   // optional secondary id (e.g. claimId)
    std::string eventType;   // e.g. "LOST_REPORTED", "FOUND_REGISTERED", "MATCH_DETECTED"
    std::string eventText;   // human-readable line for the timeline
    std::string timestamp;
};

#endif // HISTORY_RECORD_H
