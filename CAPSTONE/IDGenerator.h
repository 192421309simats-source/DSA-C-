#ifndef ID_GENERATOR_H
#define ID_GENERATOR_H

#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <ctime>

// IDGenerator
// -----------
// Generates IDs like LST-2026-0001, FND-2026-0001, CLM-2026-0001,
// NTF-2026-0001. Counters are persisted to data/counters.txt so that IDs
// stay unique even after the application restarts (Test Case 9 in spec).
class IDGenerator {
public:
    explicit IDGenerator(const std::string& counterFile = "data/counters.txt")
        : counterFilePath(counterFile) {
        load();
    }

    std::string nextLostId()   { return nextId("LST"); }
    std::string nextFoundId()  { return nextId("FND"); }
    std::string nextClaimId()  { return nextId("CLM"); }
    std::string nextNotifId()  { return nextId("NTF"); }
    std::string nextUserId()   { return nextId("USR"); }

private:
    std::string counterFilePath;
    std::map<std::string, long> counters;

    static int currentYear() {
        std::time_t t = std::time(nullptr);
        std::tm* tmPtr = std::localtime(&t);
        return 1900 + tmPtr->tm_year;
    }

    void load() {
        std::ifstream in(counterFilePath);
        if (!in.is_open()) return;
        std::string prefix;
        long value;
        while (in >> prefix >> value) {
            counters[prefix] = value;
        }
    }

    void persist() {
        std::ofstream out(counterFilePath, std::ios::trunc);
        if (!out.is_open()) return;
        for (const auto& kv : counters) {
            out << kv.first << " " << kv.second << "\n";
        }
    }

    std::string nextId(const std::string& prefix) {
        long& counter = counters[prefix];
        counter += 1;
        persist();

        std::ostringstream oss;
        oss << prefix << "-" << currentYear() << "-"
            << std::setw(4) << std::setfill('0') << counter;
        return oss.str();
    }
};

#endif // ID_GENERATOR_H
