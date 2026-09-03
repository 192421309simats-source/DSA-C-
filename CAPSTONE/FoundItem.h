#ifndef FOUND_ITEM_H
#define FOUND_ITEM_H

#include "Item.h"

class FoundItem : public Item {
public:
    std::string storageLocation; // where the found item is currently kept

    std::string typeLabel() const override { return "FOUND"; }
};

#endif // FOUND_ITEM_H
