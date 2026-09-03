#ifndef CLAIM_H
#define CLAIM_H

#include <string>
#include <map>
#include <vector>

enum class ClaimStatus { PENDING, UNDER_REVIEW, APPROVED, REJECTED, RETURNED, CANCELLED };

inline std::string claimStatusToString(ClaimStatus s) {
    switch (s) {
        case ClaimStatus::PENDING:      return "Pending";
        case ClaimStatus::UNDER_REVIEW: return "Under Review";
        case ClaimStatus::APPROVED:     return "Approved";
        case ClaimStatus::REJECTED:     return "Rejected";
        case ClaimStatus::RETURNED:     return "Returned";
        case ClaimStatus::CANCELLED:    return "Cancelled";
    }
    return "Pending";
}

inline ClaimStatus claimStatusFromString(const std::string& s) {
    if (s == "Under Review") return ClaimStatus::UNDER_REVIEW;
    if (s == "Approved") return ClaimStatus::APPROVED;
    if (s == "Rejected") return ClaimStatus::REJECTED;
    if (s == "Returned") return ClaimStatus::RETURNED;
    if (s == "Cancelled") return ClaimStatus::CANCELLED;
    return ClaimStatus::PENDING;
}

// Enforces the workflow: Pending -> Under Review -> Approved/Rejected -> Returned,
// with Cancelled reachable from Pending/Under Review only. Any other jump
// (e.g. Returned -> Pending) is rejected by the ClaimWorkflow service.
inline bool isValidClaimTransition(ClaimStatus from, ClaimStatus to) {
    static const std::map<ClaimStatus, std::vector<ClaimStatus>> allowed = {
        { ClaimStatus::PENDING,      { ClaimStatus::UNDER_REVIEW, ClaimStatus::CANCELLED, ClaimStatus::REJECTED } },
        { ClaimStatus::UNDER_REVIEW, { ClaimStatus::APPROVED, ClaimStatus::REJECTED, ClaimStatus::CANCELLED } },
        { ClaimStatus::APPROVED,     { ClaimStatus::RETURNED } },
        { ClaimStatus::REJECTED,     {} },
        { ClaimStatus::RETURNED,     {} },
        { ClaimStatus::CANCELLED,    {} }
    };
    if (from == to) return false;
    auto it = allowed.find(from);
    if (it == allowed.end()) return false;
    for (auto s : it->second) if (s == to) return true;
    return false;
}

class Claim {
public:
    std::string claimId;
    std::string lostItemId;
    std::string foundItemId;
    std::string claimantId;
    std::string claimDate;

    int matchScore = 0;
    int ownershipScore = -1; // -1 = not yet verified

    ClaimStatus status = ClaimStatus::PENDING;

    std::string administratorId;
    std::string decisionDate;
    std::string remarks;

    // OwnerVerify answers, stored as question -> answer
    std::map<std::string, std::string> verificationAnswers;
};

#endif // CLAIM_H
