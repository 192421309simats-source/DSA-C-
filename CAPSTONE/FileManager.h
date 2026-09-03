#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include "../models/User.h"
#include "../models/LostItem.h"
#include "../models/FoundItem.h"
#include "../models/Claim.h"
#include "../models/Notification.h"
#include "../models/HistoryRecord.h"

// FileManager
// -----------
// Handles all C++ file-handling persistence for the system. Each entity is
// stored as one line per record in a pipe-delimited text file under data/.
// Fields that may themselves contain '|' (free text) are escaped so records
// never desync. Handles: missing file (auto-create on first save), empty
// file, corrupt/short record lines (skipped with a warning instead of
// crashing), and file-open failures (reported, not thrown).
class FileManager {
public:
    explicit FileManager(const std::string& dataDir = "data")
        : dir(dataDir) {
        ensureDir(dir);
        usersFile          = dir + "/users.txt";
        lostItemsFile       = dir + "/lost_items.txt";
        foundItemsFile      = dir + "/found_items.txt";
        claimsFile          = dir + "/claims.txt";
        notificationsFile   = dir + "/notifications.txt";
        historyFile         = dir + "/history.txt";
        ensureFile(usersFile);
        ensureFile(lostItemsFile);
        ensureFile(foundItemsFile);
        ensureFile(claimsFile);
        ensureFile(notificationsFile);
        ensureFile(historyFile);
    }

    // ---------------- Users ----------------
    bool saveUser(const User& u) {
        std::vector<std::string> f = {
            u.userId, escape(u.name), escape(u.email), u.passwordHash, roleToString(u.role)
        };
        return appendLine(usersFile, join(f));
    }

    std::vector<User> loadUsers() {
        std::vector<User> out;
        for (auto& line : readLines(usersFile)) {
            auto f = split(line);
            if (f.size() < 5) { warnCorrupt(usersFile, line); continue; }
            out.push_back(User(f[0], unescape(f[1]), unescape(f[2]), f[3], roleFromString(f[4])));
        }
        return out;
    }

    bool rewriteUsers(const std::vector<User>& users) {
        std::ostringstream oss;
        for (auto& u : users) {
            std::vector<std::string> f = {
                u.userId, escape(u.name), escape(u.email), u.passwordHash, roleToString(u.role)
            };
            oss << join(f) << "\n";
        }
        return writeAll(usersFile, oss.str());
    }

    // ---------------- Lost Items ----------------
    bool saveLostItem(const LostItem& it) {
        return appendLine(lostItemsFile, serializeLost(it));
    }

    std::vector<LostItem> loadLostItems() {
        std::vector<LostItem> out;
        for (auto& line : readLines(lostItemsFile)) {
            LostItem it;
            if (deserializeLost(line, it)) out.push_back(it);
            else warnCorrupt(lostItemsFile, line);
        }
        return out;
    }

    bool rewriteLostItems(const std::vector<LostItem>& items) {
        std::ostringstream oss;
        for (auto& it : items) oss << serializeLost(it) << "\n";
        return writeAll(lostItemsFile, oss.str());
    }

    // ---------------- Found Items ----------------
    bool saveFoundItem(const FoundItem& it) {
        return appendLine(foundItemsFile, serializeFound(it));
    }

    std::vector<FoundItem> loadFoundItems() {
        std::vector<FoundItem> out;
        for (auto& line : readLines(foundItemsFile)) {
            FoundItem it;
            if (deserializeFound(line, it)) out.push_back(it);
            else warnCorrupt(foundItemsFile, line);
        }
        return out;
    }

    bool rewriteFoundItems(const std::vector<FoundItem>& items) {
        std::ostringstream oss;
        for (auto& it : items) oss << serializeFound(it) << "\n";
        return writeAll(foundItemsFile, oss.str());
    }

    // ---------------- Claims ----------------
    bool saveClaim(const Claim& c) {
        return appendLine(claimsFile, serializeClaim(c));
    }

    std::vector<Claim> loadClaims() {
        std::vector<Claim> out;
        for (auto& line : readLines(claimsFile)) {
            Claim c;
            if (deserializeClaim(line, c)) out.push_back(c);
            else warnCorrupt(claimsFile, line);
        }
        return out;
    }

    bool rewriteClaims(const std::vector<Claim>& claims) {
        std::ostringstream oss;
        for (auto& c : claims) oss << serializeClaim(c) << "\n";
        return writeAll(claimsFile, oss.str());
    }

    // ---------------- Notifications ----------------
    bool saveNotification(const Notification& n) {
        std::vector<std::string> f = {
            n.notifId, n.userId, escape(n.title), escape(n.message),
            n.relatedId, n.createdAt, n.isRead ? "1" : "0"
        };
        return appendLine(notificationsFile, join(f));
    }

    std::vector<Notification> loadNotifications() {
        std::vector<Notification> out;
        for (auto& line : readLines(notificationsFile)) {
            auto f = split(line);
            if (f.size() < 7) { warnCorrupt(notificationsFile, line); continue; }
            Notification n;
            n.notifId = f[0]; n.userId = f[1]; n.title = unescape(f[2]);
            n.message = unescape(f[3]); n.relatedId = f[4]; n.createdAt = f[5];
            n.isRead = (f[6] == "1");
            out.push_back(n);
        }
        return out;
    }

    bool rewriteNotifications(const std::vector<Notification>& notifs) {
        std::ostringstream oss;
        for (auto& n : notifs) {
            std::vector<std::string> f = {
                n.notifId, n.userId, escape(n.title), escape(n.message),
                n.relatedId, n.createdAt, n.isRead ? "1" : "0"
            };
            oss << join(f) << "\n";
        }
        return writeAll(notificationsFile, oss.str());
    }

    // ---------------- History ----------------
    bool saveHistory(const HistoryRecord& h) {
        std::vector<std::string> f = {
            h.recordId, h.itemId, h.relatedId, h.eventType, escape(h.eventText), h.timestamp
        };
        return appendLine(historyFile, join(f));
    }

    std::vector<HistoryRecord> loadHistory() {
        std::vector<HistoryRecord> out;
        for (auto& line : readLines(historyFile)) {
            auto f = split(line);
            if (f.size() < 6) { warnCorrupt(historyFile, line); continue; }
            HistoryRecord h;
            h.recordId = f[0]; h.itemId = f[1]; h.relatedId = f[2];
            h.eventType = f[3]; h.eventText = unescape(f[4]); h.timestamp = f[5];
            out.push_back(h);
        }
        return out;
    }

private:
    std::string dir;
    std::string usersFile, lostItemsFile, foundItemsFile, claimsFile, notificationsFile, historyFile;

    static void ensureDir(const std::string& path) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) {
#ifdef _WIN32
            mkdir(path.c_str());
#else
            mkdir(path.c_str(), 0755);
#endif
        }
    }

    static void ensureFile(const std::string& path) {
        std::ifstream test(path);
        if (!test.good()) {
            std::ofstream create(path); // creates empty file if missing
        }
    }

    static void warnCorrupt(const std::string& file, const std::string& line) {
        std::cerr << "[FileManager] Skipping corrupt record in " << file
                  << ": \"" << line << "\"\n";
    }

    // Escape '|' and newlines within a free-text field so it never breaks
    // the pipe-delimited format.
    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '|') out += "\\p";
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        return out;
    }

    static std::string unescape(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                if (s[i + 1] == 'p') { out += '|'; i++; continue; }
                if (s[i + 1] == 'n') { out += '\n'; i++; continue; }
            }
            out += s[i];
        }
        return out;
    }

    static std::string join(const std::vector<std::string>& fields) {
        std::ostringstream oss;
        for (size_t i = 0; i < fields.size(); i++) {
            if (i) oss << "|";
            oss << fields[i];
        }
        return oss.str();
    }

    static std::vector<std::string> split(const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '|') {
                out.push_back(cur);
                cur.clear();
            } else {
                cur += line[i];
            }
        }
        out.push_back(cur);
        return out;
    }

    std::vector<std::string> readLines(const std::string& path) {
        std::vector<std::string> lines;
        std::ifstream in(path);
        if (!in.is_open()) {
            std::cerr << "[FileManager] Could not open " << path << " for reading.\n";
            return lines; // empty file / missing file handled gracefully
        }
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) lines.push_back(line);
        }
        return lines;
    }

    bool appendLine(const std::string& path, const std::string& line) {
        std::ofstream out(path, std::ios::app);
        if (!out.is_open()) {
            std::cerr << "[FileManager] Could not open " << path << " for writing.\n";
            return false;
        }
        out << line << "\n";
        return true;
    }

    bool writeAll(const std::string& path, const std::string& content) {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "[FileManager] Could not open " << path << " for rewriting.\n";
            return false;
        }
        out << content;
        return true;
    }

    // -------- entity (de)serialization --------
    static std::string serializeLost(const LostItem& it) {
        std::vector<std::string> f = {
            it.itemId, it.reporterId, escape(it.itemName), escape(it.category),
            escape(it.color), escape(it.brand), escape(it.location), it.dateText,
            it.timeText, escape(it.description), escape(it.uniqueFeature),
            escape(it.contactPreference), it.status, it.createdAt,
            escape(it.imagePath), it.imageHash
        };
        return join(f);
    }

    // Tolerant of both the original 14-field format and the newer 16-field
    // format (with image path/hash appended) so existing data files never
    // become unreadable after an upgrade.
    static bool deserializeLost(const std::string& line, LostItem& it) {
        auto f = split(line);
        if (f.size() < 14) return false;
        it.itemId = f[0]; it.reporterId = f[1]; it.itemName = unescape(f[2]);
        it.category = unescape(f[3]); it.color = unescape(f[4]); it.brand = unescape(f[5]);
        it.location = unescape(f[6]); it.dateText = f[7]; it.timeText = f[8];
        it.description = unescape(f[9]); it.uniqueFeature = unescape(f[10]);
        it.contactPreference = unescape(f[11]); it.status = f[12]; it.createdAt = f[13];
        it.imagePath = f.size() > 14 ? unescape(f[14]) : "";
        it.imageHash = f.size() > 15 ? f[15] : "";
        return true;
    }

    static std::string serializeFound(const FoundItem& it) {
        std::vector<std::string> f = {
            it.itemId, it.reporterId, escape(it.itemName), escape(it.category),
            escape(it.color), escape(it.brand), escape(it.location), it.dateText,
            it.timeText, escape(it.description), escape(it.uniqueFeature),
            escape(it.storageLocation), it.status, it.createdAt,
            escape(it.imagePath), it.imageHash
        };
        return join(f);
    }

    static bool deserializeFound(const std::string& line, FoundItem& it) {
        auto f = split(line);
        if (f.size() < 14) return false;
        it.itemId = f[0]; it.reporterId = f[1]; it.itemName = unescape(f[2]);
        it.category = unescape(f[3]); it.color = unescape(f[4]); it.brand = unescape(f[5]);
        it.location = unescape(f[6]); it.dateText = f[7]; it.timeText = f[8];
        it.description = unescape(f[9]); it.uniqueFeature = unescape(f[10]);
        it.storageLocation = unescape(f[11]); it.status = f[12]; it.createdAt = f[13];
        it.imagePath = f.size() > 14 ? unescape(f[14]) : "";
        it.imageHash = f.size() > 15 ? f[15] : "";
        return true;
    }

    static std::string serializeClaim(const Claim& c) {
        std::ostringstream verif;
        bool first = true;
        for (auto& kv : c.verificationAnswers) {
            if (!first) verif << ";;";
            verif << escape(kv.first) << "::" << escape(kv.second);
            first = false;
        }
        std::vector<std::string> f = {
            c.claimId, c.lostItemId, c.foundItemId, c.claimantId, c.claimDate,
            std::to_string(c.matchScore), std::to_string(c.ownershipScore),
            claimStatusToString(c.status), c.administratorId, c.decisionDate,
            escape(c.remarks), verif.str()
        };
        return join(f);
    }

    static bool deserializeClaim(const std::string& line, Claim& c) {
        auto f = split(line);
        if (f.size() < 12) return false;
        c.claimId = f[0]; c.lostItemId = f[1]; c.foundItemId = f[2]; c.claimantId = f[3];
        c.claimDate = f[4];
        try {
            c.matchScore = std::stoi(f[5]);
            c.ownershipScore = std::stoi(f[6]);
        } catch (...) { c.matchScore = 0; c.ownershipScore = -1; }
        c.status = claimStatusFromString(f[7]);
        c.administratorId = f[8]; c.decisionDate = f[9]; c.remarks = unescape(f[10]);

        if (!f[11].empty()) {
            std::stringstream ss(f[11]);
            std::string pair;
            while (std::getline(ss, pair, ';')) {
                if (pair.empty()) continue;
                if (!ss.eof() && pair.size() && f[11].find(";;") != std::string::npos) {
                    // handled below by manual split; fallback simple parse
                }
            }
            // manual split on ";;" since std::getline with char can't split multi-char delim
            std::string raw = f[11];
            size_t pos = 0;
            while (pos < raw.size()) {
                size_t next = raw.find(";;", pos);
                std::string chunk = (next == std::string::npos) ? raw.substr(pos) : raw.substr(pos, next - pos);
                size_t sep = chunk.find("::");
                if (sep != std::string::npos) {
                    std::string k = unescape(chunk.substr(0, sep));
                    std::string v = unescape(chunk.substr(sep + 2));
                    c.verificationAnswers[k] = v;
                }
                if (next == std::string::npos) break;
                pos = next + 2;
            }
        }
        return true;
    }
};

#endif // FILE_MANAGER_H
