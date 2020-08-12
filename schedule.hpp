#pragma once

#include <cassert>
#include <memory>
#include <set>

#include "json.hpp"
#include "utils.hpp"

static constexpr size_t PCIE_GBPS = Unit::GiB(12);
static constexpr size_t PCIE_GBPNS = PCIE_GBPS / 1000000000ull;
static constexpr uint64_t PCIE_LATENCY = Unit::ms(0.02);

enum Placement {
    ABSTRACT,
    DEVICE,
    HOST
};

struct Operand {
    size_t size;
    Placement placement = ABSTRACT;

    explicit Operand(size_t size): size(size) {}
};

typedef std::shared_ptr<Operand> OprandHandle;

struct Task;
typedef std::shared_ptr<Task> TaskHandle;

struct Task {
    std::string name = "none";
    uint64_t time = 0, finish = 0;
    size_t workspace = 0;
    TaskHandle reference;
    std::vector<OprandHandle> ins, outs;

    bool hash_calculated = false;
    size_t hash_value = 0;

    bool allOn(Placement placement, bool isIn) const {
        const auto &vec = isIn ? ins : outs;
        for (auto &operand: vec) {
            if (operand->placement != placement) {
                return false;
            }
        }
        return true;
    }

    size_t inAmount(bool should_on_device=false) const {
        size_t total = 0;
        for (auto &operand: ins) {
            total += ((not should_on_device) or operand->placement == DEVICE) ? operand->size : 0;
        }
        return total;
    }

    size_t outAmount(bool should_on_device=false) const {
        size_t total = 0;
        for (auto &operand: outs) {
            total += ((not should_on_device) or operand->placement == DEVICE) ? operand->size : 0;
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
        if (reference) {
            hash_value = hash_value * 131ull + reference->hash();
        }
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

    // TODO: figure out how to sync a single transfer (just .dealloc?)
    bool isSync() const {
        return name == ".sync";
    }

    bool isHost2Device() const {
        return name == ".host2device";
    }

    bool isDevice2Host() const {
        return name == ".device2host";
    }

    static TaskHandle fromJson(const std::vector<OprandHandle> &operands, const nlohmann::json &json) {
        auto task = std::make_shared<Task>();
        auto fill = [operands](std::vector<OprandHandle> &vec, const nlohmann::json &array) {
            vec.resize(array.size());
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
        assert(task->name != ".host2device");
        assert(task->name != ".device2host");
        return task;
    }
};

class Schedule;
typedef std::shared_ptr<Schedule> ScheduleHandle;

class Schedule {
    bool calculated = false;
    size_t peak_memory = 0;
    uint64_t total_time = 0;
    std::vector<OprandHandle> operands;
    std::vector<TaskHandle> schedule;

    bool hash_calculated = false;
    size_t hash_value = 0;

public:
    std::pair<size_t, uint64_t> statistics() {
        if (not calculated) {
            calculated = true;
            total_time = 0;
            uint64_t next_available_transfer_time = 0;

            // Simulation for time
            for (auto &task: schedule) {
                uint64_t duration = 0;
                if (task->isSync()) {
                    // NOTE: `finish` and `total_time` are unsigned
                    if (task->reference->finish > total_time) {
                        duration = std::max(duration, task->reference->finish - total_time);
                    }
                } else if (task->isDevice2Host() or task->isHost2Device()) {
                    uint64_t start_time = std::max(total_time, next_available_transfer_time);
                    task->finish = start_time + task->inAmount() / PCIE_GBPNS + PCIE_LATENCY;
                    next_available_transfer_time = task->finish;
                } else {
                    duration += task->time;
                }
                total_time += duration;
            }

            // Simulation for memory
            size_t current_memory = 0;
            for (auto &operand: operands) {
                operand->placement = ABSTRACT;
            }
            std::set<OprandHandle> exists;
            for (auto &task: schedule) {
                for (auto &operand: task->ins) {
                    if (not exists.count(operand)) {
                        operand->placement = DEVICE;
                        exists.insert(operand);
                        current_memory += operand->size;
                    }
                }
                for (auto &operand: task->outs) {
                    exists.insert(operand);
                }
            }
            peak_memory = current_memory;
            for (auto &task: schedule) {
                size_t execution_memory = 0;
                if (task->isSync()) {
                    // TODO: specify operands to be synced
                    assert(task->ins.empty() && task->outs.empty());
                    assert(task->reference->isHost2Device() or task->reference->isDevice2Host());
                    Placement placement = task->reference->isHost2Device() ? DEVICE : HOST;
                    Placement origin = placement == DEVICE ? HOST : DEVICE;
                    // NOTE: there's no implicit .dealloc
                    for (auto &operand: task->reference->outs) {
                        assert(operand->placement == origin);
                        operand->placement = placement;
                    }
                } else if (task->isHost2Device() or task->isDevice2Host()) {
                    // NOTE: not generate operand placement until .sync but the space is pre-allocated
                    assert(task->inAmount() == task->outAmount());
                    if (task->isHost2Device()) {
                        assert(task->allOn(HOST, true));
                        assert(task->allOn(DEVICE, false));
                        current_memory += task->outAmount();
                    } else {
                        assert(task->allOn(DEVICE, true));
                        assert(task->allOn(HOST, false));
                    }
                } else if (task->isDealloc()) {
                    assert(task->ins.empty());
                    current_memory -= task->outAmount(true);
                } else {
                    assert(task->allOn(DEVICE, true));
                    for (auto &operand: task->outs) {
                        assert(operand->placement != HOST);
                        if (operand->placement == ABSTRACT) {
                            operand->placement = DEVICE;
                            current_memory += operand->size;
                        }
                    }
                    execution_memory = current_memory + task->workspace;
                }
                peak_memory = std::max(peak_memory, std::max(execution_memory, current_memory));
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

    void show() {
        statistics();
        std::cout << " > Peak memory usage: " << prettyBytes(peak_memory) << std::endl;
        std::cout << " > Total time: " << prettyNanoseconds(total_time) << std::endl;
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

        bool considerable(const ScheduleHandle &s1, const ScheduleHandle &s2) const {
            // Return whether `s1` is considerable comparing to `s2` (possibly the best)
            return score(s1) < score(s2) * RECONSIDER_RATIO;
        }
    };
};
