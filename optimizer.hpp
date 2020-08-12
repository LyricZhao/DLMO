#pragma once

#include <queue>
#include <sstream>

#include "schedule.hpp"
#include "utils.hpp"

class Optimizer {
    static constexpr int SEARCH_LIMIT = 1000;

    size_t limit;
    bool recompute_on, swap_on;

public:
    Optimizer(size_t limit, bool recompute_on, bool swap_on) {
        this->limit = limit;
        this->recompute_on = recompute_on;
        this->swap_on = swap_on;
    }

    std::string name() const {
        std::stringstream ss;
        ss << "optimizer (limit ";
        ss << prettyBytes(limit) << ", ";
        ss << "recompute " << (recompute_on ? "on" : "off") << ", ";
        ss << "swap " << (swap_on ? "on" : "off") << ")";
        return ss.str();
    }

    static std::vector<ScheduleHandle> generateRecomputeSubstitution(const ScheduleHandle &schedule) {
        std::vector<ScheduleHandle> substitutions;
        return substitutions;
    };

    static std::vector<ScheduleHandle> generateSwapSubstitution(const ScheduleHandle &schedule) {
        std::vector<ScheduleHandle> substitutions;
        return substitutions;
    };

    void optimize(const ScheduleHandle &origin) const {
        ScheduleHandle best = origin;
        auto comparator = Schedule::LimitComparator{limit};
        auto considerable = Schedule::ConsiderableComparator();
        std::set<size_t> hash_set;
        std::priority_queue<ScheduleHandle, std::vector<ScheduleHandle>, Schedule::LimitComparator> queue(comparator);

        // Source
        queue.push(origin);
        hash_set.insert(origin->hash());

        // Back-tracing search
        int count = 0;
        while (not queue.empty()) {
            auto top = queue.top();
            queue.pop();

            // Substitute
            std::vector<ScheduleHandle> substitutions;
            if (recompute_on) {
                auto vec = generateRecomputeSubstitution(top);
                substitutions.insert(substitutions.begin(), vec.begin(), vec.end());
            }
            if (swap_on) {
                auto vec = generateSwapSubstitution(top);
                substitutions.insert(substitutions.begin(), vec.begin(), vec.end());
            }

            // Insert and check
            for (auto &substitution: substitutions) {
                if (hash_set.count(substitution->hash())) {
                    continue;
                }
                if (considerable(substitution, best)) {
                    queue.push(substitution);
                    hash_set.insert(substitution->hash());
                }
                if (comparator(substitution, best)) {
                    best = substitution;
                }
            }

            if (++ count == SEARCH_LIMIT) {
                std::cout << "Reach search limit, stop searching" << std::endl;
                break;
            }
        }

        // Show best
        best->show();
    }
};