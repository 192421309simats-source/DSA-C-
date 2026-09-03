#ifndef LOST_ITEM_H
#define LOST_ITEM_H

#include "Item.h"

class LostItem : public Item {
public:
    std::string contactPreference; // e.g. "Email", "Phone", "In-app"

    std::string typeLabel() const override { return "LOST"; }
};

#endif // LOST_ITEM_H
