#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <set>

// StringUtils
// -----------
// A collection of small, transparent (non-"black box") string comparison
// helpers used by the CampusMatch Engine. Everything here is intentionally
// simple enough to explain in a viva: normalization, Levenshtein edit
// distance, token/keyword overlap (Jaccard-like), and a combined similarity
// score that blends the two. No external NLP/ML libraries are used.
namespace StringUtils {

    // Lowercase + trim + collapse internal whitespace.
    inline std::string normalize(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        bool lastWasSpace = false;
        for (char c : input) {
            char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (std::isspace(static_cast<unsigned char>(lc))) {
                if (!lastWasSpace && !out.empty()) {
                    out.push_back(' ');
                    lastWasSpace = true;
                }
            } else if (std::isalnum(static_cast<unsigned char>(lc))) {
                out.push_back(lc);
                lastWasSpace = false;
            } else {
                // treat punctuation as a separator (space) so
                // "red-logo" and "red logo" behave the same way
                if (!lastWasSpace && !out.empty()) {
                    out.push_back(' ');
                    lastWasSpace = true;
                }
            }
        }
        // trim trailing space
        while (!out.empty() && out.back() == ' ') out.pop_back();
        // trim leading space
        size_t start = 0;
        while (start < out.size() && out[start] == ' ') start++;
        return out.substr(start);
    }

    inline std::vector<std::string> tokenize(const std::string& input) {
        std::string norm = normalize(input);
        std::vector<std::string> tokens;
        std::stringstream ss(norm);
        std::string tok;
        // simple English stopwords irrelevant to item description matching
        static const std::set<std::string> stopwords = {
            "a","an","the","with","and","of","in","on","at","is","was",
            "it","this","that","has","have","had","to","for","near","by"
        };
        while (ss >> tok) {
            if (tok.size() <= 1) continue;
            if (stopwords.count(tok)) continue;
            tokens.push_back(tok);
        }
        return tokens;
    }

    // Classic dynamic-programming edit distance.
    inline int levenshtein(const std::string& a, const std::string& b) {
        const size_t n = a.size(), m = b.size();
        if (n == 0) return static_cast<int>(m);
        if (m == 0) return static_cast<int>(n);

        std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
        for (size_t i = 0; i <= n; i++) dp[i][0] = static_cast<int>(i);
        for (size_t j = 0; j <= m; j++) dp[0][j] = static_cast<int>(j);

        for (size_t i = 1; i <= n; i++) {
            for (size_t j = 1; j <= m; j++) {
                int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                dp[i][j] = std::min({
                    dp[i - 1][j] + 1,       // deletion
                    dp[i][j - 1] + 1,       // insertion
                    dp[i - 1][j - 1] + cost // substitution
                });
            }
        }
        return dp[n][m];
    }

    // 0.0 - 1.0 similarity based on normalized edit distance.
    inline double editSimilarity(const std::string& a, const std::string& b) {
        std::string na = normalize(a), nb = normalize(b);
        if (na.empty() && nb.empty()) return 1.0;
        if (na.empty() || nb.empty()) return 0.0;
        int dist = levenshtein(na, nb);
        size_t maxLen = std::max(na.size(), nb.size());
        return 1.0 - (static_cast<double>(dist) / static_cast<double>(maxLen));
    }

    // Jaccard-style keyword overlap: |intersection| / |union| of tokens.
    inline double keywordOverlap(const std::string& a, const std::string& b) {
        std::vector<std::string> ta = tokenize(a);
        std::vector<std::string> tb = tokenize(b);
        if (ta.empty() && tb.empty()) return 1.0;
        if (ta.empty() || tb.empty()) return 0.0;

        std::set<std::string> sa(ta.begin(), ta.end());
        std::set<std::string> sb(tb.begin(), tb.end());

        int intersection = 0;
        for (const auto& w : sa) if (sb.count(w)) intersection++;

        std::set<std::string> unionSet = sa;
        unionSet.insert(sb.begin(), sb.end());

        if (unionSet.empty()) return 0.0;
        return static_cast<double>(intersection) / static_cast<double>(unionSet.size());
    }

    inline bool exactMatch(const std::string& a, const std::string& b) {
        return normalize(a) == normalize(b);
    }

    inline bool containsPartial(const std::string& a, const std::string& b) {
        std::string na = normalize(a), nb = normalize(b);
        if (na.empty() || nb.empty()) return false;
        return na.find(nb) != std::string::npos || nb.find(na) != std::string::npos;
    }

    // Combined 0.0 - 1.0 similarity score used for single-field comparisons
    // (item name, description, unique feature). Blends character-level
    // (edit distance) and word-level (keyword overlap) similarity so that
    // both typos ("Samsng") and paraphrases ("black bag" vs "dark backpack")
    // are captured. Exact and substring matches short-circuit to 1.0.
    inline double similarity(const std::string& a, const std::string& b) {
        if (exactMatch(a, b)) return 1.0;
        if (containsPartial(a, b)) return 0.9;

        double edit = editSimilarity(a, b);
        double keyword = keywordOverlap(a, b);
        // weight keyword overlap slightly higher for longer free-text fields
        return (edit * 0.4) + (keyword * 0.6);
    }
}

#endif // STRING_UTILS_H
