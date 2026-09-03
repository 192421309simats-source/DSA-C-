#ifndef ITEM_H
#define ITEM_H

#include <string>

// Common attributes shared by lost and found items. LostItem and FoundItem
// both inherit from this so the MatchingEngine can be written generically
// against `Item&` where useful.
class Item {
public:
    std::string itemId;
    std::string reporterId;      // userId of reporter/finder
    std::string itemName;
    std::string category;
    std::string color;
    std::string brand;
    std::string location;
    std::string dateText;        // DD-MM-YYYY
    std::string timeText;        // hh:mm AM/PM
    std::string description;
    std::string uniqueFeature;
    std::string status;          // ACTIVE, MATCHED, CLAIMED, RETURNED, CLOSED
    std::string createdAt;       // ISO-ish timestamp for sorting/freshness
    std::string imagePath;       // relative path under data/images/, empty if no photo
    std::string imageHash;       // 16-hex-char perceptual average-hash, empty if no photo

    Item() = default;
    virtual ~Item() = default;

    virtual std::string typeLabel() const = 0; // "LOST" or "FOUND"
};

#endif // ITEM_H
