#include <iostream>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "vendor/httplib.h"
#include "vendor/json.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION // prevent the implementation from being pasted again by any later #include of this header

#include "models/User.h"
#include "models/LostItem.h"
#include "models/FoundItem.h"
#include "models/Claim.h"
#include "models/Notification.h"
#include "models/HistoryRecord.h"
#include "models/MatchResult.h"

#include "utils/IDGenerator.h"
#include "utils/LocationGraph.h"
#include "utils/StringUtils.h"
#include "utils/ImageHash.h"
#include "utils/Base64.h"

#include "services/FileManager.h"
#include "services/AuthenticationManager.h"
#include "services/MatchingEngine.h"
#include "services/VerificationEngine.h"
#include "services/RecoveryPriority.h"

using json = nlohmann::json;

static std::mutex dataMutex; // single-writer guard: simple + correct for an academic project server

static std::string nowTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm* tmPtr = std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tmPtr, "%d %b %H:%M");
    return oss.str();
}

static std::string todayDateText() {
    std::time_t t = std::time(nullptr);
    std::tm* tmPtr = std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tmPtr, "%d-%m-%Y");
    return oss.str();
}

static int daysBetweenApprox(const std::string& dateText) {
    std::time_t t = std::time(nullptr);
    std::tm* tmPtr = std::localtime(&t);
    int curY = 1900 + tmPtr->tm_year, curM = tmPtr->tm_mon + 1, curD = tmPtr->tm_mday;
    int curDays = curY * 372 + curM * 31 + curD;

    int d = curD, m = curM, y = curY;
    if (dateText.size() >= 10 && dateText[2] == '-' && dateText[5] == '-') {
        try {
            d = std::stoi(dateText.substr(0, 2));
            m = std::stoi(dateText.substr(3, 2));
            y = std::stoi(dateText.substr(6, 4));
        } catch (...) {}
    }
    int itemDays = y * 372 + m * 31 + d;
    return std::max(0, curDays - itemDays);
}

// Decodes a data-URL/base64 image and computes its perceptual average-hash
// without writing anything to disk yet — used so duplicate-detection can
// compare the photo *before* we know whether this report will even be saved.
// Returns empty strings if imageBase64 is empty or undecodable (never
// throws — a bad photo upload should never fail the whole report).
struct DecodedImage { std::vector<unsigned char> bytes; std::string mime; std::string hashHex; bool valid = false; };

DecodedImage decodeAndHashImage(const std::string& imageBase64) {
    DecodedImage result;
    if (imageBase64.empty()) return result;
    std::string payload = Base64::stripDataUrlPrefix(imageBase64, result.mime);
    result.bytes = Base64::decode(payload);
    if (result.bytes.empty()) return result;
    uint64_t hash = 0;
    if (!ImageHash::computeFromMemory(result.bytes.data(), static_cast<int>(result.bytes.size()), hash)) {
        return result; // not a decodable image — skip silently
    }
    result.hashHex = ImageHash::toHex(hash);
    result.valid = true;
    return result;
}

// Persists already-decoded image bytes to data/images/<itemId><ext> now that
// the real item ID has been assigned. Returns the relative path.
std::string persistImage(const std::string& itemId, const DecodedImage& img) {
    if (!img.valid) return "";
    std::string ext = Base64::mimeToExt(img.mime);
    std::string relPath = "images/" + itemId + ext;
    std::ofstream out("data/" + relPath, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(img.bytes.data()), static_cast<std::streamsize>(img.bytes.size()));
    }
    return relPath;
}

// ---------------- Global in-memory state (mirrors the files) ----------------
FileManager fileManager("data");
IDGenerator idGen("data/counters.txt");
LocationGraph locationGraph;
MatchingEngine matchingEngine(locationGraph);
VerificationEngine verificationEngine(locationGraph);
AuthenticationManager authManager(fileManager);

std::vector<LostItem> lostItems;
std::vector<FoundItem> foundItems;
std::vector<Claim> claims;
std::vector<Notification> notifications;
std::vector<HistoryRecord> history;

// ---------------- Helpers ----------------

json attributeScoreToJson(const AttributeScore& a) {
    return json{ {"attribute", a.attribute}, {"scored", a.scored}, {"max", a.max}, {"matched", a.matched} };
}

json matchResultToJson(const MatchResult& r) {
    json attrs = json::array();
    for (auto& a : r.breakdown) attrs.push_back(attributeScoreToJson(a));
    return json{
        {"sourceId", r.sourceId}, {"targetId", r.targetId},
        {"score", r.score}, {"confidence", r.confidence},
        {"breakdown", attrs}, {"reasons", r.reasons}
    };
}

json lostItemToJson(const LostItem& it) {
    return json{
        {"itemId", it.itemId}, {"reporterId", it.reporterId}, {"itemName", it.itemName},
        {"category", it.category}, {"color", it.color}, {"brand", it.brand},
        {"location", it.location}, {"date", it.dateText}, {"time", it.timeText},
        {"description", it.description}, {"uniqueFeature", it.uniqueFeature},
        {"contactPreference", it.contactPreference}, {"status", it.status},
        {"createdAt", it.createdAt}, {"type", "LOST"},
        {"hasImage", !it.imagePath.empty()}
    };
}

json foundItemToJson(const FoundItem& it) {
    return json{
        {"itemId", it.itemId}, {"reporterId", it.reporterId}, {"itemName", it.itemName},
        {"category", it.category}, {"color", it.color}, {"brand", it.brand},
        {"location", it.location}, {"date", it.dateText}, {"time", it.timeText},
        {"description", it.description}, {"uniqueFeature", it.uniqueFeature},
        {"storageLocation", it.storageLocation}, {"status", it.status},
        {"createdAt", it.createdAt}, {"type", "FOUND"},
        {"hasImage", !it.imagePath.empty()}
    };
}

json claimToJson(const Claim& c) {
    json verif = json::object();
    for (auto& kv : c.verificationAnswers) verif[kv.first] = kv.second;
    return json{
        {"claimId", c.claimId}, {"lostItemId", c.lostItemId}, {"foundItemId", c.foundItemId},
        {"claimantId", c.claimantId}, {"claimDate", c.claimDate},
        {"matchScore", c.matchScore}, {"ownershipScore", c.ownershipScore},
        {"status", claimStatusToString(c.status)}, {"administratorId", c.administratorId},
        {"decisionDate", c.decisionDate}, {"remarks", c.remarks},
        {"verificationAnswers", verif}
    };
}

json notificationToJson(const Notification& n) {
    return json{
        {"notifId", n.notifId}, {"userId", n.userId}, {"title", n.title},
        {"message", n.message}, {"relatedId", n.relatedId},
        {"createdAt", n.createdAt}, {"isRead", n.isRead}
    };
}

json historyToJson(const HistoryRecord& h) {
    return json{
        {"recordId", h.recordId}, {"itemId", h.itemId}, {"relatedId", h.relatedId},
        {"eventType", h.eventType}, {"eventText", h.eventText}, {"timestamp", h.timestamp}
    };
}

void addHistory(const std::string& itemId, const std::string& relatedId,
                 const std::string& eventType, const std::string& text) {
    HistoryRecord h;
    h.recordId = "HST-" + std::to_string(history.size() + 1);
    h.itemId = itemId; h.relatedId = relatedId; h.eventType = eventType;
    h.eventText = text; h.timestamp = nowTimestamp();
    history.push_back(h);
    fileManager.saveHistory(h);
}

void addNotification(const std::string& userId, const std::string& title,
                      const std::string& message, const std::string& relatedId) {
    Notification n;
    n.notifId = idGen.nextNotifId();
    n.userId = userId; n.title = title; n.message = message;
    n.relatedId = relatedId; n.createdAt = nowTimestamp(); n.isRead = false;
    notifications.push_back(n);
    fileManager.saveNotification(n);
}

LostItem* findLost(const std::string& id) {
    for (auto& it : lostItems) if (it.itemId == id) return &it;
    return nullptr;
}
FoundItem* findFound(const std::string& id) {
    for (auto& it : foundItems) if (it.itemId == id) return &it;
    return nullptr;
}
Claim* findClaim(const std::string& id) {
    for (auto& c : claims) if (c.claimId == id) return &c;
    return nullptr;
}

bool requireAdmin(const httplib::Request& req, httplib::Response& res) {
    std::string uid = req.get_header_value("X-User-Id");
    if (!authManager.isAdmin(uid)) {
        res.status = 403;
        res.set_content(json{{"error", "You are not authorized to perform this action."}}.dump(), "application/json");
        return false;
    }
    return true;
}

void setJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

// ---------------- main ----------------

int main() {
    // Ensure the images subfolder exists (FileManager creates data/ itself).
#ifdef _WIN32
    mkdir("data/images");
#else
    mkdir("data/images", 0755);
#endif

    // Load persisted state on startup (Test Case 9: data survives restart)
    lostItems = fileManager.loadLostItems();
    foundItems = fileManager.loadFoundItems();
    claims = fileManager.loadClaims();
    notifications = fileManager.loadNotifications();
    history = fileManager.loadHistory();

    std::cout << "CampusFind AI backend starting...\n"
              << "Loaded " << lostItems.size() << " lost items, "
              << foundItems.size() << " found items, "
              << claims.size() << " claims.\n";

    httplib::Server svr;

    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, X-User-Id"}
    });
    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    svr.set_mount_point("/", "../frontend");
    svr.set_mount_point("/uploads", "data/images"); // serves saved lost/found photos directly by filename

    // -------- AUTH --------
    svr.Post("/api/login", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }
        std::string email = body.value("email", "");
        std::string password = body.value("password", "");
        if (email.empty() || password.empty()) { setJson(res, {{"error","Please enter email and password."}}, 400); return; }
        std::string uid = authManager.login(email, password);
        if (uid.empty()) { setJson(res, {{"error","Invalid credentials."}}, 401); return; }
        User* u = authManager.findById(uid);

        // If the login page specifies which portal it is (student-login.html
        // vs admin-login.html), enforce that server-side rather than trusting
        // the frontend — an admin account can't slip through the student
        // portal and vice versa.
        std::string expectedRole = body.value("expectedRole", "");
        if (!expectedRole.empty()) {
            bool isAdminAccount = (u->role == Role::ADMIN);
            bool wantsAdmin = (expectedRole == "ADMIN");
            if (isAdminAccount != wantsAdmin) {
                std::string msg = wantsAdmin
                    ? "This account is not an administrator. Use the Student/Staff sign-in instead."
                    : "This is an administrator account. Use the Administrator sign-in instead.";
                setJson(res, {{"error", msg}}, 403);
                return;
            }
        }

        setJson(res, {{"userId", uid}, {"name", u->name}, {"role", roleToString(u->role)}, {"email", u->email}});
    });

    svr.Post("/api/register", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }
        std::string name = body.value("name", "");
        std::string email = body.value("email", "");
        std::string password = body.value("password", "");
        std::string roleStr = body.value("role", "STUDENT");
        if (name.empty() || email.empty() || password.empty()) {
            setJson(res, {{"error","All fields are required."}}, 400); return;
        }
        if (authManager.userExists(email)) { setJson(res, {{"error","An account with this email already exists."}}, 400); return; }
        std::string uid = authManager.registerUser(name, email, password, roleFromString(roleStr),
                                                     []() { return idGen.nextUserId(); });
        setJson(res, {{"userId", uid}});
    });

    // -------- LOST ITEMS --------
    svr.Post("/api/lost", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }

        std::string userId = req.get_header_value("X-User-Id");
        if (userId.empty()) userId = body.value("userId", "");

        std::vector<std::string> required = {"itemName","category","location"};
        for (auto& f : required) {
            if (body.value(f, "").empty()) { setJson(res, {{"error", "Please enter the " + f + "."}}, 400); return; }
        }

        LostItem candidate;
        candidate.reporterId = userId;
        candidate.itemName = body.value("itemName", "");
        candidate.category = body.value("category", "");
        candidate.color = body.value("color", "");
        candidate.brand = body.value("brand", "");
        candidate.location = body.value("location", "");
        candidate.dateText = body.value("date", todayDateText());
        candidate.timeText = body.value("time", "");
        candidate.description = body.value("description", "");
        candidate.uniqueFeature = body.value("uniqueFeature", "");
        candidate.contactPreference = body.value("contactPreference", "In-app");
        candidate.status = "ACTIVE";
        candidate.createdAt = nowTimestamp();

        // Decode + hash the photo (if any) up front so duplicate detection can
        // weigh visual similarity too; the file itself is only written to disk
        // once we know the report is actually being saved (below).
        DecodedImage decodedImg = decodeAndHashImage(body.value("imageBase64", ""));
        candidate.imageHash = decodedImg.hashHex;

        // Duplicate detection: compare against existing ACTIVE lost items
        json duplicateWarning = nullptr;
        for (auto& existing : lostItems) {
            if (existing.status != "ACTIVE") continue;
            MatchResult dup = matchingEngine.compareLostToLost(candidate, existing);
            if (dup.score >= 75) {
                duplicateWarning = json{
                    {"existingReportId", existing.itemId},
                    {"similarity", dup.score},
                    {"message", "The system found an existing report with very similar attributes."}
                };
                break;
            }
        }
        if (duplicateWarning != nullptr && !body.value("forceCreate", false)) {
            setJson(res, {{"duplicate", true}, {"warning", duplicateWarning}}, 200);
            return;
        }

        candidate.itemId = idGen.nextLostId();
        candidate.imagePath = persistImage(candidate.itemId, decodedImg);
        lostItems.push_back(candidate);
        fileManager.saveLostItem(candidate);
        addHistory(candidate.itemId, "", "LOST_REPORTED", "Lost report created for " + candidate.itemName);

        // Run CampusMatch immediately against existing found items
        auto ranked = matchingEngine.rankMatches(candidate, foundItems);
        if (!ranked.empty() && ranked.front().score >= 40) {
            addNotification(candidate.reporterId, "New Possible Match",
                candidate.itemName + " has a " + std::to_string(ranked.front().score) + "% match.",
                candidate.itemId);
            addHistory(candidate.itemId, ranked.front().targetId, "MATCH_DETECTED",
                std::to_string(ranked.front().score) + "% match detected");
        }

        json matches = json::array();
        for (auto& m : ranked) matches.push_back(matchResultToJson(m));

        setJson(res, {{"item", lostItemToJson(candidate)}, {"duplicate", false}, {"matches", matches}}, 201);
    });

    svr.Get("/api/lost", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json arr = json::array();
        std::string userId = req.get_param_value("userId");
        for (auto& it : lostItems) {
            if (!userId.empty() && it.reporterId != userId) continue;
            arr.push_back(lostItemToJson(it));
        }
        setJson(res, arr);
    });

    svr.Get(R"(/api/lost/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        LostItem* it = findLost(req.matches[1]);
        if (!it) { setJson(res, {{"error","Not found"}}, 404); return; }
        setJson(res, lostItemToJson(*it));
    });

    // -------- FOUND ITEMS --------
    svr.Post("/api/found", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }

        std::string userId = req.get_header_value("X-User-Id");
        if (userId.empty()) userId = body.value("userId", "");

        std::vector<std::string> required = {"itemName","category","location"};
        for (auto& f : required) {
            if (body.value(f, "").empty()) { setJson(res, {{"error", "Please enter the " + f + "."}}, 400); return; }
        }

        FoundItem it;
        it.itemId = idGen.nextFoundId();
        DecodedImage decodedImg = decodeAndHashImage(body.value("imageBase64", ""));
        it.imageHash = decodedImg.hashHex;
        it.imagePath = persistImage(it.itemId, decodedImg);
        it.reporterId = userId;
        it.itemName = body.value("itemName", "");
        it.category = body.value("category", "");
        it.color = body.value("color", "");
        it.brand = body.value("brand", "");
        it.location = body.value("location", "");
        it.dateText = body.value("date", todayDateText());
        it.timeText = body.value("time", "");
        it.description = body.value("description", "");
        it.uniqueFeature = body.value("uniqueFeature", "");
        it.storageLocation = body.value("storageLocation", "Lost & Found Office");
        it.status = "ACTIVE";
        it.createdAt = nowTimestamp();

        foundItems.push_back(it);
        fileManager.saveFoundItem(it);
        addHistory(it.itemId, "", "FOUND_REGISTERED", "Found item registered: " + it.itemName);

        // Run CampusMatch against all active lost items
        json matches = json::array();
        int bestScore = 0;
        std::string bestLostId;
        for (auto& lost : lostItems) {
            if (lost.status != "ACTIVE") continue;
            MatchResult m = matchingEngine.compare(lost, it);
            if (m.score >= 40) {
                matches.push_back(matchResultToJson(m));
                if (m.score > bestScore) { bestScore = m.score; bestLostId = lost.itemId; }
            }
        }
        if (bestScore > 0) {
            LostItem* bl = findLost(bestLostId);
            addNotification(bl ? bl->reporterId : "", "New Possible Match",
                "A found item matches your report at " + std::to_string(bestScore) + "%.", it.itemId);
            addHistory(it.itemId, bestLostId, "MATCH_DETECTED", std::to_string(bestScore) + "% match detected");
        }

        setJson(res, {{"item", foundItemToJson(it)}, {"matches", matches}}, 201);
    });

    svr.Get("/api/found", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json arr = json::array();
        std::string userId = req.get_param_value("userId");
        for (auto& it : foundItems) {
            if (!userId.empty() && it.reporterId != userId) continue;
            arr.push_back(foundItemToJson(it));
        }
        setJson(res, arr);
    });

    svr.Get(R"(/api/found/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        FoundItem* it = findFound(req.matches[1]);
        if (!it) { setJson(res, {{"error","Not found"}}, 404); return; }
        setJson(res, foundItemToJson(*it));
    });

    // -------- MATCHES --------
    svr.Get(R"(/api/matches/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        LostItem* lost = findLost(req.matches[1]);
        if (!lost) { setJson(res, {{"error","Lost item not found"}}, 404); return; }
        auto ranked = matchingEngine.rankMatches(*lost, foundItems);
        json arr = json::array();
        for (auto& m : ranked) arr.push_back(matchResultToJson(m));
        setJson(res, arr);
    });

    // -------- CLAIMS --------
    svr.Post("/api/claims", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }

        std::string claimantId = req.get_header_value("X-User-Id");
        if (claimantId.empty()) claimantId = body.value("claimantId", "");
        std::string lostId = body.value("lostItemId", "");
        std::string foundId = body.value("foundItemId", "");

        LostItem* lost = findLost(lostId);
        FoundItem* found = findFound(foundId);
        if (!lost || !found) { setJson(res, {{"error","Lost or found item not found."}}, 404); return; }

        for (auto& c : claims) {
            if (c.lostItemId == lostId && c.claimantId == claimantId &&
                (c.status == ClaimStatus::PENDING || c.status == ClaimStatus::UNDER_REVIEW)) {
                setJson(res, {{"error","You already have an active claim for this item."}}, 400);
                return;
            }
        }

        MatchResult m = matchingEngine.compare(*lost, *found);

        Claim c;
        c.claimId = idGen.nextClaimId();
        c.lostItemId = lostId; c.foundItemId = foundId; c.claimantId = claimantId;
        c.claimDate = todayDateText();
        c.matchScore = m.score;
        c.ownershipScore = -1;
        c.status = ClaimStatus::PENDING;

        if (body.contains("verificationAnswers") && body["verificationAnswers"].is_object()) {
            for (auto& [k, v] : body["verificationAnswers"].items()) {
                c.verificationAnswers[k] = v.get<std::string>();
            }
            VerificationEngine::VerificationResult vr = verificationEngine.evaluate(*found, c.verificationAnswers);
            c.ownershipScore = vr.score;
        }

        claims.push_back(c);
        fileManager.saveClaim(c);
        lost->status = "MATCHED"; found->status = "MATCHED";
        fileManager.rewriteLostItems(lostItems);
        fileManager.rewriteFoundItems(foundItems);

        addHistory(lostId, c.claimId, "CLAIM_SUBMITTED", "Claim submitted for " + lost->itemName);
        setJson(res, {{"claim", claimToJson(c)}}, 201);
    });

    svr.Get("/api/claims", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json arr = json::array();
        std::string userId = req.get_param_value("userId");
        std::string status = req.get_param_value("status");
        for (auto& c : claims) {
            if (!userId.empty() && c.claimantId != userId) continue;
            if (!status.empty() && claimStatusToString(c.status) != status) continue;
            arr.push_back(claimToJson(c));
        }
        setJson(res, arr);
    });

    svr.Get(R"(/api/claims/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        Claim* c = findClaim(req.matches[1]);
        if (!c) { setJson(res, {{"error","Not found"}}, 404); return; }
        setJson(res, claimToJson(*c));
    });

    auto transitionHandler = [](ClaimStatus target) {
        return [target](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            if (!requireAdmin(req, res)) return;
            Claim* c = findClaim(req.matches[1]);
            if (!c) { setJson(res, {{"error","Not found"}}, 404); return; }
            if (!isValidClaimTransition(c->status, target)) {
                setJson(res, {{"error","Invalid status transition."}}, 400); return;
            }
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            c->status = target;
            c->administratorId = req.get_header_value("X-User-Id");
            c->decisionDate = todayDateText();
            c->remarks = body.value("remarks", c->remarks);
            fileManager.rewriteClaims(claims);

            LostItem* lost = findLost(c->lostItemId);
            FoundItem* found = findFound(c->foundItemId);
            if (target == ClaimStatus::RETURNED) {
                if (lost) lost->status = "RETURNED";
                if (found) found->status = "RETURNED";
                fileManager.rewriteLostItems(lostItems);
                fileManager.rewriteFoundItems(foundItems);
            } else if (target == ClaimStatus::REJECTED) {
                if (lost) lost->status = "ACTIVE";
                if (found) found->status = "ACTIVE";
                fileManager.rewriteLostItems(lostItems);
                fileManager.rewriteFoundItems(foundItems);
            }

            addHistory(c->lostItemId, c->claimId, "CLAIM_" + claimStatusToString(target),
                       "Claim " + c->claimId + " -> " + claimStatusToString(target));
            addNotification(c->claimantId, "Claim Update",
                             "Your claim is now: " + claimStatusToString(target), c->claimId);

            setJson(res, claimToJson(*c));
        };
    };

    svr.Put(R"(/api/claims/([^/]+)/review)", transitionHandler(ClaimStatus::UNDER_REVIEW));
    svr.Put(R"(/api/claims/([^/]+)/approve)", transitionHandler(ClaimStatus::APPROVED));
    svr.Put(R"(/api/claims/([^/]+)/reject)", transitionHandler(ClaimStatus::REJECTED));
    svr.Put(R"(/api/claims/([^/]+)/return)", transitionHandler(ClaimStatus::RETURNED));

    svr.Put(R"(/api/claims/([^/]+)/verify)", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        Claim* c = findClaim(req.matches[1]);
        if (!c) { setJson(res, {{"error","Not found"}}, 404); return; }
        FoundItem* found = findFound(c->foundItemId);
        if (!found) { setJson(res, {{"error","Found item missing"}}, 404); return; }
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }
        std::map<std::string, std::string> answers;
        if (body.contains("answers") && body["answers"].is_object()) {
            for (auto& [k, v] : body["answers"].items()) answers[k] = v.get<std::string>();
        }
        c->verificationAnswers = answers;
        VerificationEngine::VerificationResult vr = verificationEngine.evaluate(*found, answers);
        c->ownershipScore = vr.score;
        fileManager.rewriteClaims(claims);
        addHistory(c->lostItemId, c->claimId, "OWNERSHIP_VERIFIED",
                   "Ownership score: " + std::to_string(vr.score) + " (" + vr.decision + ")");
        json comp = json::object();
        for (auto& kv : vr.componentScores) comp[kv.first] = kv.second;
        setJson(res, {{"ownershipScore", vr.score}, {"decision", vr.decision}, {"components", comp}});
    });

    // -------- ITEM STATUS --------
    svr.Put(R"(/api/items/([^/]+)/status)", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (!requireAdmin(req, res)) return;
        json body;
        try { body = json::parse(req.body); } catch (...) { setJson(res, {{"error","Invalid JSON"}}, 400); return; }
        std::string newStatus = body.value("status", "");
        std::string id = req.matches[1];
        LostItem* l = findLost(id);
        FoundItem* f = findFound(id);
        if (l) { l->status = newStatus; fileManager.rewriteLostItems(lostItems); }
        else if (f) { f->status = newStatus; fileManager.rewriteFoundItems(foundItems); }
        else { setJson(res, {{"error","Not found"}}, 404); return; }
        addHistory(id, "", "STATUS_UPDATED", "Status changed to " + newStatus);
        setJson(res, {{"ok", true}});
    });

    // -------- SEARCH --------
    svr.Get("/api/search", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto p = [&](const char* k) { return req.get_param_value(k); };
        std::string typeFilter = p("type"); // LOST / FOUND / "" (both)
        std::string category = p("category"), color = p("color"), brand = p("brand");
        std::string location = p("location"), status = p("status"), q = p("q");
        int minScore = p("minScore").empty() ? 0 : std::stoi(p("minScore"));
        std::string sortBy = p("sortBy").empty() ? "date" : p("sortBy");

        auto matchesFilters = [&](const std::string& itemName, const std::string& cat, const std::string& col,
                                   const std::string& br, const std::string& loc, const std::string& st) {
            if (!category.empty() && StringUtils::normalize(cat).find(StringUtils::normalize(category)) == std::string::npos) return false;
            if (!color.empty() && StringUtils::normalize(col).find(StringUtils::normalize(color)) == std::string::npos) return false;
            if (!brand.empty() && StringUtils::normalize(br).find(StringUtils::normalize(brand)) == std::string::npos) return false;
            if (!location.empty() && StringUtils::normalize(loc).find(StringUtils::normalize(location)) == std::string::npos) return false;
            if (!status.empty() && st != status) return false;
            if (!q.empty() && StringUtils::normalize(itemName).find(StringUtils::normalize(q)) == std::string::npos) return false;
            return true;
        };

        json results = json::array();
        if (typeFilter.empty() || typeFilter == "LOST") {
            for (auto& it : lostItems) {
                if (matchesFilters(it.itemName, it.category, it.color, it.brand, it.location, it.status))
                    results.push_back(lostItemToJson(it));
            }
        }
        if (typeFilter.empty() || typeFilter == "FOUND") {
            for (auto& it : foundItems) {
                if (matchesFilters(it.itemName, it.category, it.color, it.brand, it.location, it.status))
                    results.push_back(foundItemToJson(it));
            }
        }
        (void)minScore; // score-range filtering applies at the match level via /api/matches
        setJson(res, results);
    });

    // -------- NOTIFICATIONS --------
    svr.Get("/api/notifications", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        std::string userId = req.get_param_value("userId");
        json arr = json::array();
        for (auto& n : notifications) {
            if (!userId.empty() && n.userId != userId) continue;
            arr.push_back(notificationToJson(n));
        }
        setJson(res, arr);
    });

    svr.Put(R"(/api/notifications/([^/]+)/read)", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        for (auto& n : notifications) {
            if (n.notifId == req.matches[1].str()) {
                n.isRead = true;
                fileManager.rewriteNotifications(notifications);
                setJson(res, {{"ok", true}});
                return;
            }
        }
        setJson(res, {{"error","Not found"}}, 404);
    });

    // -------- HISTORY / TIMELINE --------
    svr.Get(R"(/api/history/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        std::string itemId = req.matches[1];
        json arr = json::array();
        for (auto& h : history) {
            if (h.itemId == itemId || h.relatedId == itemId) arr.push_back(historyToJson(h));
        }
        setJson(res, arr);
    });

    svr.Get("/api/history", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json arr = json::array();
        for (auto& h : history) arr.push_back(historyToJson(h));
        setJson(res, arr);
    });

    // -------- RECOVERY PRIORITY --------
    svr.Get("/api/priority", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        json arr = json::array();
        for (auto& lost : lostItems) {
            if (lost.status != "ACTIVE") continue;
            auto ranked = matchingEngine.rankMatches(lost, foundItems);
            RecoveryPriority::PriorityInput in;
            in.bestMatchScore = ranked.empty() ? 0 : ranked.front().score;
            in.hasUniqueFeature = !lost.uniqueFeature.empty();
            in.daysSinceReported = daysBetweenApprox(lost.dateText);
            in.location = lost.location;
            in.category = lost.category;
            in.hasCandidateFound = !ranked.empty();
            int score = RecoveryPriority::score(in);
            arr.push_back({
                {"itemId", lost.itemId}, {"itemName", lost.itemName},
                {"priority", score}, {"label", RecoveryPriority::label(score)},
                {"bestMatchScore", in.bestMatchScore}
            });
        }
        std::sort(arr.begin(), arr.end(), [](const json& a, const json& b) {
            return a["priority"].get<int>() > b["priority"].get<int>();
        });
        setJson(res, arr);
    });

    // -------- DASHBOARD --------
    svr.Get("/api/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        std::string userId = req.get_param_value("userId");

        int totalLost = 0, totalFound = 0, activeMatches = 0, pendingClaims = 0,
            approvedClaims = 0, returnedItems = 0, unclaimedItems = 0;
        double scoreSum = 0; int scoreCount = 0;

        for (auto& it : lostItems) {
            if (!userId.empty() && it.reporterId != userId) continue;
            totalLost++;
            if (it.status == "ACTIVE") unclaimedItems++;
            if (it.status == "RETURNED") returnedItems++;
        }
        for (auto& it : foundItems) {
            if (!userId.empty() && it.reporterId != userId) continue;
            totalFound++;
        }
        for (auto& c : claims) {
            if (!userId.empty() && c.claimantId != userId) continue;
            if (c.status == ClaimStatus::PENDING || c.status == ClaimStatus::UNDER_REVIEW) pendingClaims++;
            if (c.status == ClaimStatus::APPROVED) approvedClaims++;
            if (c.status == ClaimStatus::RETURNED) activeMatches++; // counted below too
            scoreSum += c.matchScore; scoreCount++;
        }
        activeMatches = 0;
        for (auto& lost : lostItems) {
            if (!userId.empty() && lost.reporterId != userId) continue;
            if (lost.status == "ACTIVE") {
                auto ranked = matchingEngine.rankMatches(lost, foundItems);
                if (!ranked.empty() && ranked.front().score >= 60) activeMatches++;
            }
        }

        double recoveryRate = totalLost > 0 ? (100.0 * returnedItems / totalLost) : 0.0;
        double avgScore = scoreCount > 0 ? (scoreSum / scoreCount) : 0.0;

        setJson(res, {
            {"totalLost", totalLost}, {"totalFound", totalFound},
            {"activeMatches", activeMatches}, {"pendingClaims", pendingClaims},
            {"approvedClaims", approvedClaims}, {"returnedItems", returnedItems},
            {"unclaimedItems", unclaimedItems},
            {"averageMatchScore", std::round(avgScore * 10) / 10.0},
            {"recoveryRate", std::round(recoveryRate * 10) / 10.0}
        });
    });

    // -------- ANALYTICS --------
    svr.Get("/api/analytics", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        std::map<std::string,int> lostByCategory, foundByCategory, byLocation, lostByMonth, foundByMonth;
        for (auto& it : lostItems) {
            lostByCategory[it.category]++;
            byLocation[it.location]++;
            std::string month = it.dateText.size() >= 7 ? it.dateText.substr(3,2) + "-" + it.dateText.substr(6,4) : "unknown";
            lostByMonth[month]++;
        }
        for (auto& it : foundItems) {
            foundByCategory[it.category]++;
            byLocation[it.location]++;
            std::string month = it.dateText.size() >= 7 ? it.dateText.substr(3,2) + "-" + it.dateText.substr(6,4) : "unknown";
            foundByMonth[month]++;
        }
        int approved = 0, rejected = 0, totalDecided = 0;
        long totalRecoveryDays = 0; int recoveredCount = 0;
        for (auto& c : claims) {
            if (c.status == ClaimStatus::APPROVED || c.status == ClaimStatus::RETURNED) { approved++; totalDecided++; }
            if (c.status == ClaimStatus::REJECTED) { rejected++; totalDecided++; }
            if (c.status == ClaimStatus::RETURNED) recoveredCount++;
        }
        double claimApprovalRate = totalDecided > 0 ? (100.0 * approved / totalDecided) : 0.0;
        double matchSuccessRate = 0;
        int withMatch = 0;
        for (auto& lost : lostItems) {
            auto ranked = matchingEngine.rankMatches(lost, foundItems);
            if (!ranked.empty() && ranked.front().score >= 60) withMatch++;
        }
        matchSuccessRate = lostItems.empty() ? 0 : (100.0 * withMatch / lostItems.size());

        auto mapToJson = [](std::map<std::string,int>& m) {
            json arr = json::array();
            for (auto& kv : m) arr.push_back({{"label", kv.first}, {"count", kv.second}});
            return arr;
        };

        setJson(res, {
            {"lostByCategory", mapToJson(lostByCategory)},
            {"foundByCategory", mapToJson(foundByCategory)},
            {"byLocation", mapToJson(byLocation)},
            {"lostByMonth", mapToJson(lostByMonth)},
            {"foundByMonth", mapToJson(foundByMonth)},
            {"claimApprovalRate", std::round(claimApprovalRate * 10) / 10.0},
            {"matchSuccessRate", std::round(matchSuccessRate * 10) / 10.0},
            {"recoveredCount", recoveredCount}
        });
    });

    // -------- IMAGES --------
    // Looks the item up by ID and serves its stored photo directly, so the
    // frontend never needs to know the file extension we chose on upload.
    svr.Get(R"(/api/image/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        std::string id = req.matches[1];
        std::string relPath;
        LostItem* l = findLost(id);
        FoundItem* f = findFound(id);
        if (l && !l->imagePath.empty()) relPath = l->imagePath;
        else if (f && !f->imagePath.empty()) relPath = f->imagePath;
        if (relPath.empty()) { res.status = 404; return; }

        std::ifstream in("data/" + relPath, std::ios::binary);
        if (!in.is_open()) { res.status = 404; return; }
        std::ostringstream buf; buf << in.rdbuf();
        std::string content = buf.str();

        std::string mime = "image/jpeg";
        if (relPath.size() > 4) {
            std::string ext = relPath.substr(relPath.size() - 4);
            if (ext == ".png") mime = "image/png";
            else if (ext == ".bmp") mime = "image/bmp";
            else if (ext.substr(1) == "gif") mime = "image/gif";
        }
        res.set_content(content, mime.c_str());
    });

    // -------- LOCATIONS (for dropdowns) --------
    svr.Get("/api/locations", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (auto& kv : locationGraph.allZones()) {
            json subs = json::array();
            for (auto& s : kv.second) subs.push_back(s);
            arr.push_back({{"zone", kv.first}, {"subLocations", subs}});
        }
        setJson(res, arr);
    });

    int port = 8080;
    std::cout << "CampusFind AI backend listening on http://localhost:" << port << "\n";
    std::cout << "Frontend served from ../frontend (open http://localhost:" << port << "/ )\n";
    svr.listen("0.0.0.0", port);
    return 0;
}
