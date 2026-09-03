#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <string>

class Notification {
public:
    std::string notifId;
    std::string userId;     // recipient
    std::string title;
    std::string message;
    std::string relatedId;  // e.g. lostId, foundId, claimId
    std::string createdAt;
    bool isRead = false;
};

#endif // NOTIFICATION_H
