#pragma once

#include <queue>
#include <sstream>

#include "schedule.hpp"
#include "timer.hpp"
#include "utils.hpp"

class Optimizer {
    static constexpr int SEARCH_LIMIT = 1500;
    static constexpr int PRINT_FREQUENCY = 300;

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
            // printf("   @ [%s, %s] occupies (score1=%.6lf, score2=%.6lf)\n", occupy.gen->name.c_str(),
            //        occupy.use->name.c_str(), occupy.score1, occupy.score2);
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
            // printf("   @ Optimized to (peak: %s, memory: %s, s1: %.3lf, s2: %.3lf)\n", prettyBytes(new_schedule->peak_memory).c_str(),
            //        prettyNanoseconds(new_schedule->total_time).c_str(), occupy.score1, occupy.score2);
        }
        return substitutions;
    };

    void optimize(const ScheduleHandle &origin, const std::string &output_path) const {
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

            if (not comparator.considerable(best, top)) {
                continue;
            }

            ++ count;

            // Substitute
            std::vector<ScheduleHandle> substitutions = generateSubstitutions(top);

            // Insert and check
            for (auto &substitution: substitutions) {
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

            if (count % PRINT_FREQUENCY == 0) {
                printf(" > Progress (%d): %s, %s\n", count, prettyBytes(top->peak_memory).c_str(), prettyNanoseconds(top->total_time).c_str());
            }
        }

        // Show best
        printf(" > Result:\n");
        printf("   > Schedules searched: %d\n", count);
        printf("   > Time used: %s\n", prettyNanoseconds(timer.tik()).c_str());
        printf("   > Best: {%s}\n", best->info().c_str());
        printf("   > Satisfy memory: %s\n", best->peak_memory <= limit ? "true" : "false");

        // Write result
        printf(" > Writing result into path %s ... ", output_path.c_str());
        best->restoreAndDumpToFile(output_path);
        printf("OK!\n");
    }
};