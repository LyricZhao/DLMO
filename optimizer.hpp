#pragma once

#include <queue>
#include <sstream>

#include "schedule.hpp"
#include "timer.hpp"
#include "utils.hpp"

class Optimizer {
    static constexpr int QUEUE_SIZE_LIMIT = 100000;
    static constexpr int SEARCH_LIMIT = 1;

    size_t limit;

public:
    explicit Optimizer(size_t limit) {
        this->limit = limit;
    }

    std::string name() const {
        return "optimizer (limit " + prettyBytes(limit) + ")";
    }

    static std::vector<ScheduleHandle> generateSubstitutions(const ScheduleHandle &schedule) {
        // Analyze schedule
        schedule->analyze();

        // Re-generate graph
        printf(" @ Generating substitutions (count: %zu, peak: %s, time: %s) ...\n", schedule->occupies.size(),
               prettyBytes(schedule->peak_memory).c_str(), prettyNanoseconds(schedule->total_time).c_str());
        std::vector<ScheduleHandle> substitutions;
        for (auto &occupy: schedule->occupies) {
            printf("   @ [%s, %s] occupies (score=%.6lf)\n", occupy.gen->name.c_str(), occupy.use->name.c_str(), occupy.score);
            substitutions.push_back(schedule->apply(occupy));
        }
        return substitutions;
    };

    void optimize(const ScheduleHandle &origin) const {
        ScheduleHandle best = origin;
        auto comparator = Comparator{origin->analyze().second, limit};
        std::set<size_t> hash_set;
        std::priority_queue<ScheduleHandle, std::vector<ScheduleHandle>, Comparator> queue(comparator);

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
            std::vector<ScheduleHandle> substitutions = generateSubstitutions(top);

            // Insert and check
            for (auto &substitution: substitutions) {
                if (queue.size() == QUEUE_SIZE_LIMIT) {
                    warning("Reaching searching queue size limit %d\n", QUEUE_SIZE_LIMIT);
                    break;
                }
                if (hash_set.count(substitution->hash())) {
                    continue;
                }
                if (comparator.considerable(best, substitution)) {
                    queue.push(substitution);
                    hash_set.insert(substitution->hash());
                }
                if (comparator(best, substitution)) {
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
        printf("   > Satisfy: %s\n", comparator.satisfy(best) ? "true" : "false");
    }
};