#pragma once

#include <queue>
#include <sstream>

#include "schedule.hpp"
#include "timer.hpp"
#include "utils.hpp"

class Optimizer {
    static constexpr int QUEUE_SIZE_LIMIT = 40;
    static constexpr int SEARCH_LIMIT = 1000;

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
        // printf(" @ Generating substitutions (count: %zu, peak: %s, time: %s) ...\n", schedule->occupies.size(),
        //        prettyBytes(schedule->peak_memory).c_str(), prettyNanoseconds(schedule->total_time).c_str());
        std::vector<ScheduleHandle> substitutions;
        for (auto &occupy: schedule->occupies) {
            // printf("   @ [%s, %s] occupies (score=%.6lf)\n", occupy.gen->name.c_str(), occupy.use->name.c_str(), occupy.score);
            // printf("   @ Moving: %d\n", occupy.move);
            // for (auto &usage: occupy.gen->ins) {
            //     printf("     @ In: %d\n", usage.operand->id);
            // }
            // for (auto &usage: occupy.gen->outs) {
            //     printf("     @ Out: %d\n", usage.operand->id);
            // }
            // for (auto &task: occupy.re_gen) {
            //     printf("     @ Re-gen: %s\n", task->name.c_str());
            // }
            auto new_schedule = schedule->apply(occupy);
            substitutions.push_back(new_schedule);
            new_schedule->analyze();
            // printf("   @ Optimized to (peak: %s, memory: %s)\n", prettyBytes(new_schedule->peak_memory).c_str(),
            //        prettyNanoseconds(new_schedule->total_time).c_str());
        }
        return substitutions;
    };

    void optimize(const ScheduleHandle &origin) const {
        ScheduleHandle best = origin;
        auto comparator = Comparator{origin->analyze().second, limit};
        std::set<size_t> hash_set;
        std::set<ScheduleHandle, Comparator> queue(comparator);

        // Source
        queue.insert(origin);
        hash_set.insert(origin->hash());

        // Back-tracing search
        printf(" > Start back-tracing search from source (%s)\n", origin->info().c_str());
        Timer timer;
        int count = 0;
        bool first_warning = true;
        while (not queue.empty()) {
            auto top = *queue.rbegin();
            queue.erase(top);

            if (not comparator.considerable(best, top)) {
                continue;
            }

            ++ count;
            // printf(" > Progress: %s, %s\n", prettyBytes(top->peak_memory).c_str(), prettyNanoseconds(top->total_time).c_str());

            // Substitute
            std::vector<ScheduleHandle> substitutions = generateSubstitutions(top);

            // Insert and check
            for (auto &substitution: substitutions) {
                if (hash_set.count(substitution->hash())) {
                    continue;
                }
                if (comparator.considerable(best, substitution)) {
                    queue.insert(substitution);
                    hash_set.insert(substitution->hash());
                }
                if (comparator(best, substitution)) {
                    best = substitution;
                }
            }

            if (queue.size() > QUEUE_SIZE_LIMIT) {
                if (first_warning) {
                    printf(" > Reaching searching queue size limit %d\n", QUEUE_SIZE_LIMIT);
                    first_warning = false;
                    while (queue.size() > QUEUE_SIZE_LIMIT) {
                        queue.erase(*queue.begin());
                    }
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
        printf("   > Satisfy memory: %s\n", best->peak_memory <= limit ? "true" : "false");
    }
};