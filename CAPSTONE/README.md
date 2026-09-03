# CampusFind AI
### Intelligent Campus Lost and Found Management System (C++ backend + web frontend)
**Find. Match. Verify. Return.**

CampusFind AI is not a CRUD lost-and-found list. It runs every new report through
**CampusMatch** (a weighted, explainable, location-aware multi-attribute matching engine)
and every claim through **OwnerVerify** (a separate claimant-knowledge ownership check),
so the system can explain *why* two reports were linked and *whether* the claimant
actually seems to own the item — two different questions, scored separately.

---

## 1. Abstract

Campus lost-and-found operations are usually a spreadsheet or a shelf: a found item sits
in an office until someone happens to ask about it. CampusFind AI actively compares every
lost report against every found item across eight weighted attributes, explains the score
attribute-by-attribute, flags duplicate reports before they're even saved, ranks cases by
recovery priority, and keeps ownership verification completely separate from item-matching
so a high similarity score alone can never return someone else's property.

## 2. Problem Statement

Manual lost-and-found processes rely on people remembering to check a physical desk or a
notice board. There is no systematic way to compare a new report against everything already
on file, no way to detect that two students filed near-identical lost reports for the same
item, and no structured way to confirm a claimant actually owns an item beyond "the name
sounds right."

## 3. Existing System

Most student lost-and-found projects are a single table (`items`) with a status column and
basic `WHERE name LIKE` search. Matching is exact-string only, there is no explanation of why
two records were linked, no ownership verification distinct from the match itself, and no
persistence guarantees beyond "the database is running."

## 4. Proposed System — CampusFind AI

- **CampusMatch Engine** — weighted 8-attribute comparison (name, category, color, brand,
  location, date, description, unique feature) with case-insensitive, partial, keyword and
  edit-distance string comparison, and a location hierarchy that understands "Library" and
  "Library Entrance" are related.
- **OwnerVerify Engine** — claimant answers questions blind (without seeing the found item's
  public listing); their answers are compared to the real record across 5 weighted components.
- **Duplicate Report Detection** — Lost↔Lost comparison using the same similarity engine.
- **Recovery Priority Score** — triage score for administrators.
- **Full claim workflow** with an enforced state machine, notifications, and a per-item
  recovery timeline.
- **All of it backed by real C++ business logic and pipe-delimited file persistence** — no
  fake dashboard numbers, no dead buttons.

## 5. Objectives

1. Explainable, weighted, multi-attribute item matching (not string equality).
2. Location-aware scoring using a campus zone hierarchy.
3. Separate, evidence-based ownership verification.
4. Duplicate-report detection before a new report is even saved.
5. A recovery priority score to help administrators triage.
6. A full auditable claim workflow with an enforced state machine.
7. Real, persistent, restart-safe file storage — no database server required.
8. A working frontend wired to a real HTTP API, not mockups.

## 6. Unique Features (see spec section 3)

| # | Feature | Where implemented |
|---|---|---|
| 1 | Explainable attribute-based matching | `backend/services/MatchingEngine.h` |
| 2 | Campus location intelligence | `backend/utils/LocationGraph.h` |
| 3 | Unique feature fingerprint | Weighted attribute in `MatchingEngine` |
| 4 | Duplicate report detection | `POST /api/lost` (Lost↔Lost compare) |
| 5 | Ownership confidence scoring | `backend/services/VerificationEngine.h` |
| 6 | Item recovery timeline | `backend/models/HistoryRecord.h`, `history.html` |
| 7 | Recovery priority score | `backend/services/RecoveryPriority.h` |
| 8 | Smart notifications | `backend/models/Notification.h`, `notifications.html` |
| 9 | Advanced filtering | `GET /api/search`, `search.html` |
| 10 | Campus recovery analytics | `GET /api/analytics`, `analytics.html` |

## 7. System Architecture

```
STUDENT/STAFF  ADMIN
      \          /
     FRONTEND (HTML/CSS/JS, static, served by the C++ server)
              |
        REST API (JSON over HTTP, cpp-httplib)
              |
        C++ BACKEND (single process, mutex-guarded shared state)
     /        |            \
Lost/Found  CampusMatch   OwnerVerify
Management   Engine        Engine
     \        |            /
      File Storage (data/*.txt)
```

## 8. Module Description

- **models/** — `User` (Student/Staff/Admin via inheritance), `Item` base class with
  `LostItem`/`FoundItem`, `Claim` (+ state machine), `MatchResult`, `Notification`,
  `HistoryRecord`.
- **services/** — `FileManager` (persistence), `AuthenticationManager` (login/roles),
  `MatchingEngine` (CampusMatch), `VerificationEngine` (OwnerVerify), `RecoveryPriority`.
- **utils/** — `StringUtils` (normalization, Levenshtein edit distance, keyword/Jaccard
  overlap, combined similarity), `LocationGraph` (campus zone hierarchy), `IDGenerator`
  (restart-safe sequential IDs).
- **main.cpp** — wires everything into a REST API with `cpp-httplib`, serves the frontend
  as static files, and holds the in-memory mirror of the file-backed data.

## 9. Data Model (class design)

```
User
 ├── Student
 ├── Staff
 └── Admin

Item (abstract)
 ├── LostItem   (+ contactPreference)
 └── FoundItem  (+ storageLocation)

Claim            — status machine: Pending → Under Review → Approved/Rejected → Returned
MatchResult      — score, confidence label, per-attribute breakdown, explanation lines
Notification
HistoryRecord
```

## 10. Matching Algorithm & Formula

Default weights (configurable in `MatchingEngine::Weights`, sums to 100):

```
Item Name        20      Location         15
Category         15      Date             10
Color            10      Description      10
Brand            15      Unique Feature    5
```

Each attribute is scored 0.0–1.0 by a comparator suited to that field:

- **Item Name / Description / Unique Feature** — `StringUtils::similarity`: exact match → 1.0,
  substring containment → 0.9, otherwise a blend of normalized Levenshtein edit-distance
  similarity (40%) and Jaccard keyword overlap (60%).
- **Category / Color / Brand** — exact/case-insensitive → 1.0, substring → 0.85, else fuzzy
  similarity discounted ×0.7 (categorical fields shouldn't get full credit for loose wording).
- **Location** — `LocationGraph::scoreLocations`: same string → 100%, same campus zone but
  different sub-location → 70%, textual containment → 70%, general similarity ≥0.5 → 50%,
  ≥0.25 → 20%, else 0%.
- **Date** — same day → 1.0, 1 day apart → 0.7, ≤3 days → 0.4, ≤7 days → 0.15, else 0.

`FinalScore = Σ(attributeFraction × attributeWeight)`, rounded, capped at 100.

Confidence bands: **90–100 VERY STRONG**, **75–89 STRONG**, **60–74 POSSIBLE**,
**40–59 WEAK**, **0–39 UNLIKELY**.

Duplicate detection reuses the identical logic as a Lost↔Lost comparison; a score ≥75
surfaces a duplicate warning with `[Continue Anyway] / [Cancel]` before the report saves.

## 11. Ownership Verification Algorithm (OwnerVerify)

```
Item Details          30%   — claimant's free-text description vs. the found record
Location Knowledge    20%   — claimant's stated location vs. LocationGraph score
Time/Date Knowledge   15%   — claimant's stated time/date vs. the found record
Unique Feature        25%   — claimant's stated unique mark vs. the found record
User Information      10%   — presence/plausibility of supporting context
```

`OwnershipScore = Σ(componentFraction × componentWeight)`.

Decision bands: **80–100 RECOMMENDED APPROVAL**, **60–79 MANUAL REVIEW**, **0–59 LOW
CONFIDENCE**. The system never auto-returns an item — an administrator always makes the
final call (`PUT /api/claims/{id}/approve|reject`).

## 12. Claim Workflow (enforced state machine)

```
Pending → Under Review → Approved → Returned
   ↓            ↓
Cancelled    Rejected
```

Invalid transitions (e.g. `Returned → Pending`) are rejected server-side by
`isValidClaimTransition()` in `backend/models/Claim.h`.

## 13. File Structure

```
CampusFindAI/
├── backend/
│   ├── main.cpp                 REST API + server wiring
│   ├── models/                  User, Item, LostItem, FoundItem, Claim, MatchResult,
│   │                            Notification, HistoryRecord
│   ├── services/                FileManager, AuthenticationManager, MatchingEngine,
│   │                            VerificationEngine, RecoveryPriority
│   ├── utils/                   StringUtils, LocationGraph, IDGenerator
│   ├── vendor/                  cpp-httplib, nlohmann/json (header-only, vendored)
│   └── data/                    lost_items.txt, found_items.txt, claims.txt,
│                                 notifications.txt, history.txt, users.txt, counters.txt
├── frontend/
│   ├── index.html (login) · dashboard.html · report-lost.html · register-found.html
│   ├── smart-matching.html · match-details.html · claims.html · search.html
│   ├── my-reports.html · history.html · notifications.html · analytics.html · admin.html
│   ├── css/style.css
│   └── js/app.js                API client, auth/session, shared page shell
├── CMakeLists.txt
└── README.md
```

## 14. Installation & Compilation

**Requirements:** a C++17 compiler (g++ 9+ / clang / MSVC), CMake ≥3.10 (optional — a
direct g++ command also works). No database, no npm install — the frontend is static
HTML/CSS/JS served directly by the C++ binary.

### Option A — direct g++ (fastest)
```bash
cd CampusFindAI/backend
g++ -std=c++17 -O2 -pthread main.cpp -o campusfind_server
./campusfind_server
```

### Option B — CMake
```bash
cd CampusFindAI
cmake -B build
cmake --build build
cp build/campusfind_server backend/          # run it from backend/ so data/ & ../frontend resolve
cd backend && ./campusfind_server
```

### Windows notes
cpp-httplib and nlohmann/json are vendored as headers in `backend/vendor/` — no separate
install needed. Compile with MSVC (`cl /std:c++17 /EHsc main.cpp /Fe:campusfind_server.exe`)
or MinGW g++ with the same command as Option A; on MSVC, link `ws2_32.lib` for sockets
(handled automatically by the provided `CMakeLists.txt`).

## 15. Running

```bash
cd backend
./campusfind_server
# CampusFind AI backend listening on http://localhost:8080
```

Open **http://localhost:8080/** in a browser — the C++ server itself serves the frontend
(`svr.set_mount_point("/", "../frontend")` in `main.cpp`), so there is nothing extra to
start. On first run it seeds two accounts:

| Role | Email | Password |
|---|---|---|
| Admin | admin@campusfind.edu | admin123 |
| Student | aditi@campusfind.edu | student123 |

Data is written to `backend/data/*.txt` immediately on every create/update, so stopping and
restarting the server preserves every report, claim, and ID counter.

## 16. API Reference

```
POST /api/login                          POST /api/register
POST /api/lost            GET /api/lost           GET /api/lost/{id}
POST /api/found           GET /api/found          GET /api/found/{id}
GET  /api/matches/{lostId}
POST /api/claims          GET /api/claims         GET /api/claims/{id}
PUT  /api/claims/{id}/review|approve|reject|return
PUT  /api/claims/{id}/verify              (submits OwnerVerify answers, returns score)
PUT  /api/items/{id}/status               (admin only)
GET  /api/search   GET /api/dashboard   GET /api/analytics
GET  /api/notifications   PUT /api/notifications/{id}/read
GET  /api/history   GET /api/history/{itemId}
GET  /api/priority                        (Recovery Priority ranking)
GET  /api/locations                       (campus zone hierarchy)
```
Admin-only endpoints check the `X-User-Id` header against the Admin role and return
`403 { "error": "You are not authorized to perform this action." }` otherwise.

## 17. C++ Concepts Demonstrated

Classes & objects, encapsulation, inheritance (`User`→`Student/Staff/Admin`,
`Item`→`LostItem/FoundItem`), polymorphism (`Item::typeLabel()`,
`User::canPerformAdminActions()`), constructors, static/free functions, `std::vector`,
`std::map`, `std::string` processing, structs (`AttributeScore`), sorting
(`std::sort` for match ranking), searching (linear ID lookups), file handling (`fstream`
throughout `FileManager`), exception handling (`try/catch` around JSON parsing and numeric
conversions), and modular header-per-responsibility architecture.

## 18. Test Cases (see spec section 44)

| # | Case | Expected | How to verify |
|---|---|---|---|
| 1 | Exact match | Very Strong Match | Sample data below → 91%+ |
| 2 | Same item, different brand | Reduced score | Change `brand` on one side |
| 3 | Same item/color, different category | Possible/Weak | Change `category` |
| 4 | Completely unrelated items | Unlikely Match | Two unrelated reports |
| 5 | Duplicate lost report | Duplicate warning | Submit the same lost report twice |
| 6 | Strong match, poor ownership answers | Manual Review / Low Confidence | Submit vague `verificationAnswers` |
| 7 | Strong match + strong ownership answers | Recommended Approval | See §20 sample flow |
| 8 | Unauthorized student → admin action | Access denied (403) | Call `/approve` without an admin `X-User-Id` |
| 9 | Restart application | Data survives | Stop and restart `campusfind_server`; `GET /api/lost` still returns prior reports |

## 19. Sample Data & Expected Output

```
Lost:  Black Samsung Backpack · Bags · Library · 18-08-2026 · "One broken zipper on the front pocket."
Found: Black Samsung Backpack · Bags · Library Entrance · 18-08-2026 · "broken zipper"

Item Name       20/20   Category   15/15   Color   10/10   Brand   15/15
Location        11/15   Date       10/10   Description  5/10   Unique Feature  5/5
--------------------------------------------------------------
TOTAL: 91% — VERY STRONG MATCH
```
(Verified live against the running server — see §18 Test Case 1.)

## 20. Limitations

- Single-process, mutex-guarded in-memory + file store — fine for a lab/demo deployment,
  not a concurrent multi-writer production database.
- Text similarity is deliberately classical (edit distance + keyword overlap), not an ML
  embedding model — this is an explicit, stated design choice for academic transparency
  (see spec §9: "Do not claim to use advanced AI/ML unless actually implemented").
- Password hashing uses `std::hash` with a static salt — adequate for a student project,
  not for production authentication.

## 21. Future Enhancements (not implemented — by design, per spec §49)

Image-based item recognition, QR codes, campus map integration, email notifications, a
mobile app, a real database migration, ML-based similarity, and RFID integration are
deliberately left as documented future work rather than faked.
