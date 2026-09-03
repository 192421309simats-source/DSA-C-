#ifndef USER_H
#define USER_H

#include <string>

enum class Role { STUDENT, STAFF, ADMIN };

inline std::string roleToString(Role r) {
    switch (r) {
        case Role::STUDENT: return "STUDENT";
        case Role::STAFF:   return "STAFF";
        case Role::ADMIN:   return "ADMIN";
    }
    return "STUDENT";
}

inline Role roleFromString(const std::string& s) {
    if (s == "ADMIN") return Role::ADMIN;
    if (s == "STAFF") return Role::STAFF;
    return Role::STUDENT;
}

// Base User class. Student/Staff/Admin specialize permissions via
// virtual canPerformAdminActions(). Passwords are stored as a simple
// (non-reversible-in-UI) hash — never sent back to the frontend.
class User {
public:
    std::string userId;
    std::string name;
    std::string email;
    std::string passwordHash;
    Role role;

    User() = default;
    User(std::string id, std::string n, std::string e, std::string pwHash, Role r)
        : userId(std::move(id)), name(std::move(n)), email(std::move(e)),
          passwordHash(std::move(pwHash)), role(r) {}

    virtual ~User() = default;

    virtual bool canPerformAdminActions() const { return false; }
};

class Student : public User {
public:
    using User::User;
    bool canPerformAdminActions() const override { return false; }
};

class Staff : public User {
public:
    using User::User;
    bool canPerformAdminActions() const override { return false; }
};

class Admin : public User {
public:
    using User::User;
    bool canPerformAdminActions() const override { return true; }
};

#endif // USER_H
