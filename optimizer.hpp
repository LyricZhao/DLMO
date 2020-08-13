#pragma once

#include <queue>
#include <sstream>

#include "schedule.hpp"
#include "timer.hpp"
#include "utils.hpp"

class Optimizer {
    static constexpr int QUEUE_SIZE_LIMIT = 100000;
    static constexpr int SEARCH_LIMIT = 1000;

    size_t limit;
    bool use_recompute, use_swap;

public:
    Optimizer(size_t limit, bool use_recompute, bool use_swap) {
        this->limit = limit;
        this->use_recompute = use_recompute;
        this->use_swap = use_swap;
    }

    std::string name() const {
        std::stringstream ss;
        ss << "optimizer (limit ";
        ss << prettyBytes(limit) << ", ";
        ss << "recompute " << (use_recompute ? "on" : "off") << ", ";
        ss << "swap " << (use_swap ? "on" : "off") << ")";
        return ss.str();
    }

    // TODO: finish coding
    static std::vector<ScheduleHandle> generateRecomputeSubstitution(const ScheduleHandle &schedule) {
        std::vector<ScheduleHandle> substitutions;
        return substitutions;
    };

    // TODO: finish coding
    static std::vector<ScheduleHandle> generateSwapSubstitution(const ScheduleHandle &schedule) {
        std::vector<ScheduleHandle> substitutions;
        return substitutions;
    };

    void optimize(const ScheduleHandle &origin) const {
        ScheduleHandle best = origin;
        auto comparator = Schedule::Comparator{origin->statistics().second, limit};
        std::set<size_t> hash_set;
        std::priority_queue<ScheduleHandle, std::vector<ScheduleHandle>, Schedule::Comparator> queue(comparator);

        // Source
        queue.push(origin);
        hash_set.insert(origin->hash());

        // Back-tracing search
        printf(" > Start back-tracing search from source (%s)\n", origin->info().c_str());
        Timer timer;
        int count = 0;
        while (not queue.empty()) {
            auto top = queue.top();
            queue.pop();
            ++ count;

            // Substitute
            std::vector<ScheduleHandle> substitutions;
            if (use_recompute) {
                auto vec = generateRecomputeSubstitution(top);
                substitutions.insert(substitutions.begin(), vec.begin(), vec.end());
            }
            if (use_swap) {
                auto vec = generateSwapSubstitution(top);
                substitutions.insert(substitutions.begin(), vec.begin(), vec.end());
            }

            // Insert and check
            for (auto &substitution: substitutions) {
                if (queue.size() == QUEUE_SIZE_LIMIT) {
                    warning("Reaching searching queue size limit %d\n", QUEUE_SIZE_LIMIT);
                    break;
                }
                if (hash_set.count(substitution->hash())) {
                    continue;
                }
                if (comparator.considerable(substitution, best)) {
                    queue.push(substitution);
                    hash_set.insert(substitution->hash());
                }
                if (comparator(substitution, best)) {
                    best = substitution;
                }
            }

            if (comparator.satisfy(best)) {
                printf(" > Already satisfy requirement, stop searching\n");
                break;
            }

            if (count == SEARCH_LIMIT) {
                printf(" > Reach search limit, stop searching\n");
                break;
            }
        }

        // Show best
        printf(" > Result:\n");
        printf("   > Schedules searched: %d\n", count);
        printf("   > Time used: %s\n", prettyNanoseconds(timer.tik()).c_str());
        printf("   > Best: {%s}\n", best->info().c_str());
    }
};