#pragma once

#include <cassert>
#include <list>
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
typedef std::list<TaskHandle>::const_iterator TaskPos;

struct Occupy;
typedef std::shared_ptr<Occupy> OccupyHandle;

class Schedule;
typedef std::shared_ptr<Schedule> ScheduleHandle;

struct Operand {
    // Can be shared by other schedules
    size_t size;

    // For temporary use
    bool exist = false;
    bool has_generated = false;
    TaskPos generate;

    explicit Operand(size_t size): size(size) {}
};

struct Occupy {
    TaskPos generate, use;
    // TODO: the operands to dealloc are more than one
    OperandHandle to_dealloc;
    double msps;

    static OccupyHandle create(const TaskPos &generate, const TaskPos &use, const OperandHandle &to_dealloc,
            uint64_t time, size_t peak, size_t saved, size_t limit) {
        auto occupy = std::make_shared<Occupy>();
        occupy->generate = generate;
        occupy->use = use;
        occupy->to_dealloc = to_dealloc;
        occupy->setMSPS(time, peak, saved, limit);
        return occupy;
    }

    void setMSPS(uint64_t time, size_t peak, size_t saved, size_t limit) {
        if (time == 0) {
            msps = 1e9;
        } else {
            saved = (peak - saved < limit) ? (peak - limit) : saved;
            msps = static_cast<double>(saved) / time;
        }
    }

    bool operator < (const Occupy &another) const {
        auto g = reinterpret_cast<size_t>(generate->get());
        auto u = reinterpret_cast<size_t>(use->get());
        auto a_g = reinterpret_cast<size_t>(another.generate->get());
        auto a_u = reinterpret_cast<size_t>(another.use->get());
        return g == a_g ? u < a_u : g < a_g;
    }
};

struct Task {
    // Can be shared by other schedules
    std::string name = "none";
    size_t workspace = 0;
    std::vector<OperandHandle> ins, outs;
    bool hash_calculated = false;
    size_t hash_value = 0;
    uint64_t time = 0;

    // For temporary use
    int time_point;
    size_t usage_during_execution;
    std::vector<OccupyHandle> queries;

    static TaskHandle dealloc(const std::vector<OperandHandle> &operands) {
        auto task = std::make_shared<Task>();
        task->name = ".dealloc";
        task->outs = operands;
        return task;
    }

    TaskHandle copy() const {
        auto task = std::make_shared<Task>();
        task->name = name;
        task->workspace = workspace;
        task->ins = ins;
        task->outs = outs;
        task->hash_calculated = hash_calculated;
        task->hash_value = hash_value;
        task->time = time;
        return task;
    }

    bool allExist(bool isIn) const {
        const auto &vec = isIn ? ins : outs;
        for (auto &operand: vec) {
            if (not operand->exist) {
                return false;
            }
        }
        return true;
    }

    size_t outsAmount() const {
        size_t total = 0;
        for (auto &operand: outs) {
            total += operand->size;
        }
        return total;
    }

    size_t hash() {
        if (hash_calculated) {
            return hash_value;
        }
        hash_calculated = true;
        hash_value = std::hash<std::string>()(name);
        hash_value = hash_value * 131ull + workspace;
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
        task->time = Unit::us(static_cast<double>(json[4]));

        // TODO: JSON file has .host2device-like operators
        assert(not task->isForbidden());
        return task;
    }
};

class Schedule {
    // Structure
    std::vector<OperandHandle> operands;
    std::list<TaskHandle> schedule;

    // Statistics
    bool calculated = false;
    size_t peak_memory = 0;
    uint64_t total_time = 0;

    // Hash
    bool hash_calculated = false;
    size_t hash_value = 0;

public:
    ScheduleHandle copy() const {
        ScheduleHandle copy = std::make_shared<Schedule>();
        copy->operands = operands;
        copy->schedule = schedule;
        return copy;
    }

    void apply(const OccupyHandle &occupy) {
        auto iterator = occupy->use;
        for (-- iterator; iterator != occupy->generate; -- iterator) {
            bool stop = false;
            for (auto &operand: (*iterator)->ins) {
                if (operand == occupy->to_dealloc) {
                    stop = true;
                    break;
                }
            }
            if (stop) {
                break;
            }
        }
        schedule.insert(++ iterator, Task::dealloc({occupy->to_dealloc}));
        printf("To dealloc: %p\n", occupy->to_dealloc.get());
        printf("Regenerate: [%s (%p), %s]\n", (*occupy->generate)->name.c_str(), (*occupy->generate).get(),
                (*occupy->use)->name.c_str());
        schedule.insert(occupy->use, (*occupy->generate)->copy());
    }

    OccupyHandle analyze_essential(size_t limit) {
        // TODO: estimate minimal usage
        std::vector<OccupyHandle> essential;
        statistics(true);
        for (const auto &operand: operands) {
            operand->has_generated = false;
        }
        int count = 0;
        int peak_time_point = -1;
        for (const auto &task: schedule) {
            task->time_point = ++ count;
            if (task->usage_during_execution == peak_memory) {
                peak_time_point = count;
            }
            task->queries.clear();
        }
        assert(peak_time_point != -1);

        for (auto iterator = schedule.begin(); iterator != schedule.end(); ++ iterator) {
            auto &task = *iterator;
            if (task->isDealloc()) {
                continue;
            }
            for (const auto &operand: task->ins) {
                if (operand->has_generated) {
                    auto generate_point = operand->generate;
                    if ((*generate_point)->time_point < peak_time_point and peak_time_point < (*iterator)->time_point) {
                        auto occupy = Occupy::create(generate_point, iterator, operand,
                                                     (*generate_point)->time, peak_memory, operand->size, limit);
                        essential.push_back(occupy);
                        // Query whether all the inputs of `generate_point` live at `iterator`
                        task->queries.push_back(occupy);
                    }
                }
            }
            for (const auto &operand: task->outs) {
                operand->has_generated = true;
                operand->generate = iterator;
            }
        }
        for (auto &operand: operands) {
            operand->exist = false;
            operand->has_generated = false;
        }
        for (auto &task: schedule) {
            for (auto &operand: task->ins) {
                if (not operand->has_generated) {
                    operand->exist = true;
                    operand->has_generated = true;
                }
            }
            for (auto &operand: task->outs) {
                operand->has_generated = true;
            }
        }
        for (auto &task: schedule) {
            for (auto &query: task->queries) {
                for (auto &operand: (*query->generate)->ins) {
                    if (not operand->exist) {
                        query->msps = 0;
                        break;
                    }
                }
            }
            bool not_dealloc = not task->isDealloc();
            for (auto &operand: task->outs) {
                operand->exist = not_dealloc;
            }
        }
        double best_msps = 0;
        OccupyHandle best = nullptr;
        for (auto &occupy: essential) {
            if (occupy->msps > best_msps) {
                best_msps = occupy->msps;
                const auto &task = *occupy->generate;
                best = occupy;
            }
        }
        return best;
    }

    std::pair<size_t, uint64_t> statistics(bool force=false) {
        if (not calculated or force) {
            calculated = true;
            total_time = 0;

            // Simulation for time
            for (auto &task: schedule) {
                total_time += task->time;
            }

            // Simulation for memory
            size_t current_memory = 0;
            for (auto &operand: operands) {
                operand->exist = false;
                operand->has_generated = false;
            }
            for (auto &task: schedule) {
                for (auto &operand: task->ins) {
                    if (not operand->has_generated) {
                        operand->exist = true;
                        operand->has_generated = true;
                        current_memory += operand->size;
                    }
                }
                for (auto &operand: task->outs) {
                    operand->has_generated = true;
                }
            }
            peak_memory = current_memory;
            for (auto &task: schedule) {
                printf("Task: %s\n", task->name.c_str());
                printf("Inputs:\n");
                for (auto &operand: task->ins) {
                    printf(" > %p\n", operand.get());
                }
                printf("Outputs:\n");
                for (auto &operand: task->outs) {
                    printf(" > %p\n", operand.get());
                }
                if (task->isDealloc()) {
                    assert(task->ins.empty());
                    assert(task->allExist(false));
                    for (auto &operand: task->outs) {
                        operand->exist = false;
                    }
                    current_memory -= task->outsAmount();
                } else {
                    if (not task->allExist(true)) {
                        printf("Error: task %s (%p)\n", task->name.c_str(), task.get());
                        for (auto &operand: task->ins) {
                            printf(" > %p\n", operand.get());
                        }
                    }
                    assert(task->allExist(true));
                    bool share = task->isShare();
                    for (auto &operand: task->outs) {
                        if (not operand->exist) {
                            operand->exist = true;
                            current_memory += share ? 0 : operand->size;
                        }
                    }
                }
                size_t execution_memory = current_memory + task->workspace;
                task->usage_during_execution = execution_memory;
                peak_memory = std::max(peak_memory, execution_memory);
            }
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
        auto &operands = schedule->operands;
        for (auto &item: json["operands"]) {
            int id = item[0];
            size_t size = item[1];
            if (id >= operands.size()) {
                operands.resize(id + 1);
            }
            operands[id] = std::make_shared<Operand>(size);
        }

        // Records
        for (auto &item: json["records"]) {
            schedule->schedule.push_back(Task::fromJson(operands, item));
        }

        return schedule;
    }

    std::string info() {
        statistics();
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
        for (auto &task: schedule) {
            hash_value = hash_value * 131ull + task->hash();
        }
        return hash_value;
    }

    struct Comparator {
        uint64_t origin_time;
        size_t limit;

        static constexpr double MEMORY_FACTOR = 0.5;
        static constexpr double TIME_FACTOR = 1 - MEMORY_FACTOR;
        static constexpr double RECONSIDER_RATIO = 1.05;
        static constexpr double TIME_REQUIREMENT_RATIO = 1.01;

        double score(const ScheduleHandle &s) const {
            size_t memory;
            uint64_t time;
            std::tie(memory, time) = s->statistics();

            // Using exceeded ratio to compare, lower is better
            double exceeded_memory_ratio = memory > limit ? (static_cast<double>(memory - limit) / limit) : 0;
            double exceeded_time_ratio = static_cast<double>(time - origin_time) / origin_time;
            return MEMORY_FACTOR * exceeded_memory_ratio + TIME_FACTOR * exceeded_time_ratio;
        }

        bool operator () (const ScheduleHandle &s1, const ScheduleHandle &s2) const {
            // Whether reaching limit
            if ((s1->statistics().first <= limit) != (s2->statistics().first <= limit)) {
                return s2->statistics().first <= limit;
            }

            // Compare both time and memory
            return score(s1) > score(s2);
        }

        bool satisfy(const ScheduleHandle &s) const {
            size_t memory;
            uint64_t time;
            std::tie(memory, time) = s->statistics();
            return memory <= limit && time <= TIME_REQUIREMENT_RATIO * origin_time;
        }

        bool considerable(const ScheduleHandle &s1, const ScheduleHandle &s2) const {
            // Return whether `s1` is considerable comparing to `s2` (possibly the best)
            return score(s1) < score(s2) * RECONSIDER_RATIO;
        }
    };
};
