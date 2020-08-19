#pragma once

#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#include <fstream>

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
    size_t size = 0;

    // Temporary uses
    bool on_device = false, occurred = false;

    explicit Operand(size_t size): size(size) {}

    void clear() {
        on_device = occurred = false;
    }
};

struct OperandUsage {
    OperandHandle operand;
    TaskHandle gen, prev_use, next_use;
};

struct Task {
    // Can be shared by other schedules
    std::string name = "none";
    size_t workspace = 0;
    std::vector<OperandUsage> ins, outs;
    bool hash_calculated = false;
    size_t hash_value = 0;
    uint64_t duration = 0;

    // Structure
    TaskHandle prev, next;

    nlohmann::json toJson(std::map<OperandHandle, int> &operand_to_id) {
        auto usage_to_json = [&operand_to_id](const std::vector<OperandUsage> &usages) {
            auto json = nlohmann::json::array();
            for (auto &usage: usages) {
                json.push_back(operand_to_id[usage.operand]);
            }
            return json;
        };
        auto json = nlohmann::json::array();
        json.push_back(name);
        json.push_back(usage_to_json(ins));
        json.push_back(usage_to_json(outs));
        json.push_back(workspace);
        json.push_back(duration);
        return json;
    }

    static TaskHandle dealloc(const std::vector<OperandHandle> &operands) {
        auto task = std::make_shared<Task>();
        task->name = ".dealloc";
        for (auto &operand: operands) {
            task->outs.push_back(OperandUsage{ operand });
        }
        return task;
    }

    void clear() {
        for (auto &usage: ins) {
            usage.prev_use = usage.next_use = usage.gen = nullptr;
        }
        for (auto &usage: outs) {
            usage.prev_use = usage.next_use = usage.gen = nullptr;
        }
    }

    size_t hash() {
        if (hash_calculated) {
            return hash_value;
        }
        hash_calculated = true;
        hash_value = std::hash<std::string>()(name);
        hash_value = hash_value * 131ull + workspace;
        // Pointer as hash
        for (auto &usage: ins) {
            hash_value = hash_value * 131ull + reinterpret_cast<size_t>(usage.operand.get());
        }
        for (auto &usage: outs) {
            hash_value = hash_value * 131ull + reinterpret_cast<size_t>(usage.operand.get());
        }
        return hash_value;
    }

    bool isDealloc() const {
        return name == ".dealloc";
    }

    bool isForbidden() const {
        return name == ".host2device" or name == ".device2host" or name == ".sync" or name == ".alloc";
    }

    static TaskHandle fromJson(const std::vector<OperandHandle> &operands, const nlohmann::json &json) {
        auto task = std::make_shared<Task>();
        auto fill = [operands](std::vector<OperandUsage> &vec, const nlohmann::json &array) {
            vec.resize(array.size());
            // Python processor has already assumed `arch == "CUDA"`
            for (int i = 0; i < vec.size(); ++ i) {
                vec[i] = OperandUsage{ operands[static_cast<int>(array[i])] };
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
    std::set<OperandHandle> already_on;
    std::set<OperandHandle> not_dealloc;

    static CommonHandle fromJson(const nlohmann::json &json) {
        auto common = std::make_shared<Common>();
        auto &operands = common->operands;
        for (auto &item: json) {
            int id = item[0];
            size_t size = item[1];
            if (id >= operands.size()) {
                operands.resize(id + 1);
            }
            operands[id] = std::make_shared<Operand>(size);
        }
        return common;
    }

    bool check(TaskHandle &head) const {
        // Clear status
        for (auto &operand: operands) {
            operand->clear();
        }
        for (auto &operand: already_on) {
            operand->on_device = true;
        }

        // Simulate and check
        LOOP(task, head) {
            if (task->isDealloc()) {
                for (auto &usage: task->outs) {
                    if (not usage.operand->on_device) {
                        return false;
                    }
                    usage.operand->on_device = false;
                }
            } else {
                for (auto &usage: task->ins) {
                    if (not usage.operand->on_device) {
                        return false;
                    }
                }
                for (auto &usage: task->outs) {
                    usage.operand->on_device = true;
                }
            }
        }

        // Check final status
        for (auto &operand: operands) {
            if (operand->on_device and not not_dealloc.count(operand)) {
                return false;
            }
            if (not operand->on_device and not_dealloc.count(operand)) {
                return false;
            }
        }
        return true;
    }

    void analyze_placement(TaskHandle &head) {
        // Clear status
        not_dealloc.clear();
        already_on.clear();
        for (auto &operand: operands) {
            operand->clear();
        }

        // Analyze `already_on` and `not_dealloc`
        LOOP(task, head) {
            if (task->isDealloc()) {
                for (auto &usage: task->outs) {
                    usage.operand->on_device = false;
                }
            } else {
                for (auto &usage: task->ins) {
                    if (not usage.operand->on_device) {
                        already_on.insert(usage.operand);
                        usage.operand->on_device = true;
                    }
                }
                for (auto &usage: task->outs) {
                    usage.operand->on_device = true;
                }
            }
        }
        for (auto &operand: operands) {
            if (operand->on_device) {
                not_dealloc.insert(operand);
            }
        }
    }

    void analyze_topology(TaskHandle &head) const {
        // Clear status
        for (auto &operand: operands) {
            operand->clear();
        }
        LOOP(task, head) {
            task->clear();
        }

        // Analyze generating task and previous use
        std::map<OperandHandle, TaskHandle> prev_use;
        std::map<OperandHandle, TaskHandle> gen;
        LOOP(task, head) {
            assert(not task->isDealloc());
            // TODO: check no duplicate ins/outs
            for (auto &usage: task->ins) {
                usage.gen = gen[usage.operand];
                usage.prev_use = prev_use[usage.operand];
                prev_use[usage.operand] = task;
                // TODO: the operation is time consuming
                // Set the previous' next to current task
                if (usage.prev_use) {
                    for (auto &prev_usage: usage.prev_use->ins) {
                        if (prev_usage.operand == usage.operand) {
                            prev_usage.next_use = task;
                            break;
                        }
                    }
                }
            }
            for (auto &usage: task->outs) {
                gen[usage.operand] = task;
                prev_use[usage.operand] = nullptr;
            }
        }
    }

    static void refactor(TaskHandle &head) {
        TaskHandle loop_head = head, tail;
        head = nullptr;
        LOOP(task, loop_head) {
            if (not task->isDealloc()) {
                if (not tail) {
                    tail = head = task;
                    task->prev = nullptr;
                } else {
                    tail->next = task;
                    task->prev = tail;
                    tail = task;
                }
            }
        }
        tail->next = nullptr;
    }

    void restore(TaskHandle &head) const {
        // Analyze topology
        analyze_topology(head);

        // Insert .dealloc
        LOOP(task, head) {
            // Create new .dealloc
            std::vector<OperandHandle> to_dealloc;
            for (auto &usage: task->ins) {
                if (not usage.next_use and not not_dealloc.count(usage.operand)) {
                    bool inOuts = false;
                    // TODO: optimize the loop
                    for (auto &out_usage: task->outs) {
                        if (usage.operand == out_usage.operand) {
                            inOuts = true;
                            break;
                        }
                    }
                    if (not inOuts) {
                        to_dealloc.push_back(usage.operand);
                    }
                }
            }
            if (not to_dealloc.empty()) {
                auto new_task = Task::dealloc(to_dealloc);
                auto origin_next = task->next;
                task->next = new_task;
                new_task->prev = task;
                new_task->next = origin_next;
                if (origin_next) {
                    origin_next->prev = new_task;
                }
                task = new_task;
            }
        }
    }

    nlohmann::json toJson(TaskHandle &head) {
        nlohmann::json json;

        // Push operands
        json["operands"] = nlohmann::json::array();
        auto &operands_json = json["operands"];
        int id = 0;
        std::map<OperandHandle, int> operand_to_id;
        for (auto &operand: operands) {
            operand_to_id[operand] = id;
            auto operand_json = nlohmann::json::array({id ++, operand->size});
            operands_json.push_back(operand_json);
        }

        // Push tasks
        json["records"] = nlohmann::json::array();
        auto &records_json = json["records"];
        LOOP(task, head) {
            records_json.push_back(task->toJson(operand_to_id));
        }

        return json;
    }
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
        schedule->common = Common::fromJson(json["operands"]);

        // Records
        TaskHandle tail;
        for (auto &item: json["records"]) {
            auto task = Task::fromJson(schedule->common->operands, item);
            if (not tail) {
                schedule->head = task;
                tail = task;
            } else {
                tail->next = task;
                task->prev = tail;
                tail = task;
            }
        }
        tail->next = nullptr;
        if (schedule->common->check(schedule->head)) {
            error("Origin schedule in file %s check failed.", path.c_str());
        }

        // Analyze common elements and refactor to the format without .dealloc
        schedule->common->analyze_placement(schedule->head);
        schedule->common->refactor(schedule->head);

        return schedule;
    }

    void dumpToFile(const std::string &path, bool restore=true, bool not_change=true) {
        // Restore to the format with .dealloc
        if (restore) {
            common->restore(head);
            if (common->check(head)) {
                error("Check failed while dumping to file.\n");
            }
        }

        // Dump into json
        auto json = common->toJson(head);
        std::ofstream file(path);
        file << json.dump(4) << std::endl;

        // Recover to original
        if (restore and not_change) {
            common->refactor(head);
        }
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
    static constexpr double RECONSIDER_RATIO = 2;
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
