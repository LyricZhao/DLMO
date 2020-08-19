#pragma once

#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include "json.hpp"
#include "utils.hpp"

struct Operand;
typedef std::shared_ptr<Operand> OperandHandle;

struct Task;
typedef std::shared_ptr<Task> TaskHandle;

struct Common;
typedef std::shared_ptr<Common> CommonHandle;

struct Schedule;
typedef std::shared_ptr<Schedule> ScheduleHandle;

// All schedules share a common set of operands
struct Operand {
    // Can be shared by other schedules
    size_t size;

    // Temporary uses
    TaskHandle prev_gen, prev_use;

    explicit Operand(size_t size): size(size) {}
};

struct Task {
    // Can be shared by other schedules
    std::string name = "none";
    size_t workspace = 0;
    std::vector<OperandHandle> ins, outs;
    bool hash_calculated = false;
    size_t hash_value = 0;
    uint64_t duration = 0;

    // Structure
    TaskHandle prev, next;

    // Temporary uses

    size_t hash() {
        if (hash_calculated) {
            return hash_value;
        }
        hash_calculated = true;
        hash_value = std::hash<std::string>()(name);
        hash_value = hash_value * 131ull + workspace;
        // Pointer as hash
        for (auto &operand: ins) {
            hash_value = hash_value * 131ull + reinterpret_cast<size_t>(operand.get());
        }
        for (auto &operand: outs) {
            hash_value = hash_value * 131ull + reinterpret_cast<size_t>(operand.get());
        }
        return hash_value;
    }

    bool isDealloc() const {
        return name == ".dealloc";
    }

    bool isShare() const {
        return name == ".share";
    }

    bool isForbidden() const {
        return name == ".host2device" or name == ".device2host" or name == ".sync" or name == ".alloc";
    }

    static TaskHandle fromJson(const std::vector<OperandHandle> &operands, const nlohmann::json &json) {
        auto task = std::make_shared<Task>();
        auto fill = [operands](std::vector<OperandHandle> &vec, const nlohmann::json &array) {
            vec.resize(array.size());
            // Python processor has already assumed `arch == "CUDA"`
            for (int i = 0; i < vec.size(); ++ i) {
                vec[i] = operands[static_cast<int>(array[i])];
            }
        };

        // Fill task
        task->name = json[0];
        fill(task->ins, json[1]);
        fill(task->outs, json[2]);
        task->workspace = json[3];
        task->duration = Unit::us(static_cast<double>(json[4]));

        // TODO: JSON file has .host2device-like operators
        assert(not task->isForbidden());
        return task;
    }
};

struct Common {
    std::vector<OperandHandle> operands;
};

struct Schedule {
    // Structure
    CommonHandle common;
    TaskHandle head;

    // Statistics
    bool analyzed = false;
    size_t peak_memory = 0;
    uint64_t total_time = 0;

    // Hash
    bool hash_calculated = false;
    size_t hash_value = 0;

    std::pair<size_t, uint64_t> analyze() {
        if (not analyzed) {
            analyzed = true;
            total_time = 0;

            // Simulation for time
            LOOP(task, head) {
                total_time += task->duration;
            }

            // TODO: Simulation for memory
        }
        return std::make_pair(peak_memory, total_time);
    }

    static ScheduleHandle fromFile(const std::string &path) {
        // Read JSON
        std::ifstream file(path);
        nlohmann::json json;
        file >> json;

        // Operands
        auto schedule = std::make_shared<Schedule>();
        auto common = std::make_shared<Common>();
        auto &operands = common->operands;
        for (auto &item: json["operands"]) {
            int id = item[0];
            size_t size = item[1];
            if (id >= operands.size()) {
                operands.resize(id + 1);
            }
            operands[id] = std::make_shared<Operand>(size);
        }
        schedule->common = common;

        // Records
        TaskHandle tail;
        for (auto &item: json["records"]) {
            auto task = Task::fromJson(operands, item);
            if (not tail) {
                schedule->head = task;
                tail = task;
            } else {
                tail->next = task;
                task->prev = tail;
                tail = task;
            }
        }

        return schedule;
    }

    std::string info() {
        analyze();
        std::stringstream ss;
        ss << "peak memory: " << prettyBytes(peak_memory) << ", ";
        ss << "total time: " << prettyNanoseconds(total_time);
        return ss.str();
    }

    size_t hash() {
        if (hash_calculated) {
            return hash_value;
        }
        hash_calculated = true;
        hash_value = 0;
        LOOP(task, head) {
            hash_value = hash_value * 131ull + task->hash();
        }
        return hash_value;
    }
};

struct Comparator {
    uint64_t origin_time;
    size_t limit;

    static constexpr double MEMORY_FACTOR = 0.6;
    static constexpr double TIME_FACTOR = 1 - MEMORY_FACTOR;
    static constexpr double RECONSIDER_RATIO = 2 ;
    static constexpr double TIME_REQUIREMENT_RATIO = 1.01;

    double score(const ScheduleHandle &s) const {
        size_t peak_memory;
        uint64_t total_time;
        std::tie(peak_memory, total_time) = s->analyze();

        // Using exceeded ratio to compare, lower is better
        double exceeded_memory_ratio = peak_memory > limit ? (static_cast<double>(peak_memory - limit) / limit) : 0;
        double exceeded_time_ratio = static_cast<double>(total_time - origin_time) / origin_time;
        return MEMORY_FACTOR * exceeded_memory_ratio + TIME_FACTOR * exceeded_time_ratio;
    }

    bool operator () (const ScheduleHandle &s1, const ScheduleHandle &s2) const {
        // Whether reaching limit
        size_t s1_peak_memory = s1->analyze().first;
        size_t s2_peak_memory = s2->analyze().first;
        if ((s1_peak_memory <= limit) != (s2_peak_memory <= limit)) {
            return s2_peak_memory <= limit;
        }

        // Compare both time and memory
        return score(s1) > score(s2);
    }

    bool satisfy(const ScheduleHandle &s) const {
        size_t peak_memory;
        uint64_t total_time;
        std::tie(peak_memory, total_time) = s->analyze();
        return peak_memory <= limit && total_time <= TIME_REQUIREMENT_RATIO * origin_time;
    }

    bool considerable(const ScheduleHandle &s1, const ScheduleHandle &s2) const {
        // Return whether `s2` is considerable comparing to `s1` (possibly the best)
        return score(s1) * RECONSIDER_RATIO > score(s2);
    }
};
