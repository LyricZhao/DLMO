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

// TODO: support .share operator

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
    int id = 0;

    // Temporary uses
    bool on_device = false, occurred = false;

    explicit Operand(size_t size, int id): size(size), id(id) {}

    void clear() {
        on_device = occurred = false;
    }
};

struct OperandUsage {
    OperandHandle operand;

    // Temporary uses
    size_t version = 0;
    TaskHandle gen, next_gen, prev_use, next_use, last_use;

    bool operator < (const OperandUsage &another) const {
        return operand < another.operand;
    }
};

struct Task {
    // Can not be shared by other schedules
    std::string name = "none";
    size_t workspace = 0;
    std::vector<OperandUsage> ins, outs;
    bool hash_calculated = false;
    size_t hash_value = 0;
    uint64_t duration = 0;
    bool inplace = false;

    // Structure
    TaskHandle prev, next;

    // Temporary uses
    int time_stamp;
    size_t execution_memory;
    std::vector<OperandHandle> to_dealloc_after;

    TaskHandle copy() const {
        auto new_task = std::make_shared<Task>();
        new_task->name = name;
        new_task->workspace = workspace;
        new_task->ins = ins;
        new_task->outs = outs;
        new_task->hash_calculated = hash_calculated;
        new_task->hash_value = hash_value;
        new_task->duration = duration;
        new_task->inplace = inplace;
        return new_task;
    }

    nlohmann::json toJson() {
        auto usage_to_json = [](const std::vector<OperandUsage> &usages) {
            auto json = nlohmann::json::array();
            for (auto &usage: usages) {
                json.push_back(usage.operand->id);
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
        time_stamp = 0;
        execution_memory = 0;
        for (auto &usage: ins) {
            usage.version = usage.operand->id;
            usage.prev_use = usage.next_use = nullptr;
            usage.next_gen = usage.gen = usage.last_use = nullptr;
        }
        for (auto &usage: outs) {
            usage.version = usage.operand->id;
            usage.prev_use = usage.next_use = nullptr;
            usage.next_gen = usage.gen = usage.last_use = nullptr;
        }
        to_dealloc_after.clear();
    }

    bool contains(const OperandHandle &operand, bool is_out=true) const {
        auto vec = is_out ? outs : ins;
        for (auto &usage: vec) {
            if (usage.operand == operand) {
                return true;
            }
        }
        return false;
    }

    OperandUsage& find(const OperandHandle &operand, bool is_out=true) {
        auto &vec = is_out ? outs : ins;
        for (auto &usage: vec) {
            if (usage.operand == operand) {
                return usage;
            }
        }
        error("Not found");
        // Unreachable part
        return outs[0];
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

        // Detect inplace
        std::set<OperandHandle> ins;
        for (auto &usage: task->ins) {
            ins.insert(usage.operand);
        }
        task->inplace = false;
        for (auto &usage: task->outs) {
            if (ins.count(usage.operand)) {
                task->inplace = true;
                break;
            }
        }

        assert(not task->isForbidden());
        return task;
    }
};

struct Occupy {
    // It's different with comparator
    static constexpr double O1_MEMORY_FACTOR = 0.2;
    static constexpr double O1_TIME_FACTOR = 1 - O1_MEMORY_FACTOR;
    static constexpr double O2_MEMORY_FACTOR = 0.8;
    static constexpr double O2_TIME_FACTOR = 1 - O2_MEMORY_FACTOR;

    TaskHandle gen, use;
    std::vector<TaskHandle> re_gen;
    std::set<OperandUsage> re_gen_ins;

    bool move;
    double score1, score2;

    void calculate(int peak_time_stamp, size_t peak_memory, uint64_t origin_time) {
        // Maybe dead code
        move = true;
        for (auto &usage: gen->outs) {
            if (usage.next_use and usage.next_use->time_stamp < use->time_stamp) {
                move = false;
                break;
            }
        }

        // Time increased
        uint64_t time_increased = move ? 0 : gen->duration;
        for (auto &task: re_gen) {
            time_increased += task->duration;
        }

        // Memory increased
        // Use `signed long long` instead of `size_t`
        long long memory_increased = 0;
        // Prolong dealloc time (memory increased)
        for (auto &usage: re_gen_ins) {
            if ((not usage.last_use or usage.last_use->time_stamp < peak_time_stamp) and (not gen->contains(usage.operand))) {
                memory_increased += usage.operand->size;
            }
        }
        // Re-computation (memory decreased)
        for (auto &usage: use->ins) {
            // TODO: simplify
            if (gen->contains(usage.operand) and (not usage.prev_use or usage.prev_use->time_stamp < peak_time_stamp)) {
                memory_increased -= usage.operand->size;
            }
        }

        // Calculate score, lower is better
        score1 = static_cast<double>(memory_increased) / peak_memory * O1_MEMORY_FACTOR;
        score1 += static_cast<double>(time_increased) / origin_time * O1_TIME_FACTOR;
        score2 = static_cast<double>(memory_increased) / peak_memory * O2_MEMORY_FACTOR;
        score2 += static_cast<double>(time_increased) / origin_time * O2_TIME_FACTOR;
        // size_t signed_memory = memory_increased > 0 ? memory_increased : -memory_increased;
        // printf("[%s, %s] memory: %c%s, time: %s, score=%.6lf\n", gen->name.c_str(), use->name.c_str(),
        //        memory_increased > 0 ? '+' : '-', prettyBytes(signed_memory).c_str(),
        //        prettyNanoseconds(time_increased).c_str(), score);
    }

    bool operator < (const Occupy &another) const {
        // We only compare the generation time, because we only accept the first usage after peak
        return gen < another.gen;
    }
};

struct Common {
    std::vector<OperandHandle> operands;
    std::set<OperandHandle> already_on;
    std::set<OperandHandle> not_dealloc;

    static constexpr int O1_OCCUPIES_LIMIT = 2;
    static constexpr int O2_OCCUPIES_LIMIT = 2;
    static constexpr int RANDOM_LIMIT = 2;

    static CommonHandle fromJson(const nlohmann::json &json) {
        auto common = std::make_shared<Common>();
        auto &operands = common->operands;
        for (auto &item: json) {
            int id = item[0];
            size_t size = item[1];
            if (id >= operands.size()) {
                operands.resize(id + 1);
            }
            operands[id] = std::make_shared<Operand>(size, id);
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

    void analyzePlacement(TaskHandle &head) {
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

    void analyzeTopology(TaskHandle &head) const {
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
            size_t version = 0;
            for (auto &usage: task->ins) {
                usage.gen = gen[usage.operand];
                usage.prev_use = prev_use[usage.operand];
                prev_use[usage.operand] = task;
                // Set the previous' next to current task
                if (usage.prev_use) {
                    auto &prev_usage = usage.prev_use->find(usage.operand, false);
                    prev_usage.next_use = task;
                }
                if (usage.gen) {
                    auto &gen_usage = usage.gen->find(usage.operand);
                    usage.version = gen_usage.version;
                    if (not gen_usage.next_use) {
                        gen_usage.next_use = task;
                    }
                }
                version = version * 131ull + usage.version;
            }
            for (auto &usage: task->outs) {
                usage.version = version * 131ull + usage.operand->id;
                usage.gen = gen[usage.operand] = task;
                usage.prev_use = prev_use[usage.operand] = nullptr;
            }
        }

        // Analyze operands to dealloc
        TaskHandle tail;
        LOOP(task, head) {
            for (auto &usage: task->ins) {
                if (not usage.next_use and not not_dealloc.count(usage.operand) and not task->contains(usage.operand)) {
                    task->to_dealloc_after.push_back(usage.operand);
                }
            }
            for (auto &usage: task->outs) {
                if (not usage.next_use and not not_dealloc.count(usage.operand)) {
                    task->to_dealloc_after.push_back(usage.operand);
                }
            }
            tail = task;
        }

        // Analyze next generation
        gen.clear();
        LOOP_BACK(task, tail) {
            for (auto &usage: task->outs) {
                usage.next_gen = gen[usage.operand];
                gen[usage.operand] = task;
            }
            for (auto &usage: task->ins) {
                usage.next_gen = gen[usage.operand];
            }
        }

        // Analyze last use
        LOOP_BACK(task, tail) {
            for (auto &usage: task->ins) {
                if (usage.next_use) {
                    auto &next_use = usage.next_use->find(usage.operand, false);
                    usage.last_use = next_use.last_use ? next_use.last_use : usage.next_use;
                } else {
                    usage.last_use = nullptr;
                }
            }
        }
    }

    static uint64_t analyzeTime(TaskHandle &head) {
        uint64_t total_time = 0;
        LOOP(task, head) {
            total_time += task->duration;
        }
        return total_time;
    }

    size_t analyzeMemory(TaskHandle &head) const {
        // Analyze topology
        analyzeTopology(head);

        // Operands already on device
        size_t current_memory = 0;
        for (auto &operand: operands) {
            operand->on_device = false;
        }
        for (auto &operand: already_on) {
            operand->on_device = true;
            current_memory += operand->size;
        }
        size_t peak_memory = current_memory;

        // Loop all tasks
        LOOP(task, head) {
            for (auto &usage: task->ins) {
                assert(usage.operand->on_device);
            }
            for (auto &usage: task->outs) {
                if (not usage.operand->on_device) {
                    usage.operand->on_device = true;
                    current_memory += usage.operand->size;
                }
            }
            task->execution_memory = current_memory + task->workspace;
            peak_memory = std::max(peak_memory, task->execution_memory);
            // printf("@%s: %s\n", task->name.c_str(), prettyBytes(task->execution_memory).c_str());

            for (auto &operand: task->to_dealloc_after) {
                operand->on_device = false;
                current_memory -= operand->size;
            }
        }
        return peak_memory;
    }

    static std::vector<Occupy> analyzeOccupies(TaskHandle &head, size_t peak_memory, uint64_t origin_time) {
        // Run this function after running analyzeTopology and analyzeMemory
        // Mark tasks and get the peak one
        int time_stamp = 0, peak_time_stamp = 0;
        LOOP(task, head) {
            task->time_stamp = ++ time_stamp;
            if (task->execution_memory == peak_memory) {
                peak_time_stamp = time_stamp;
            }
        }
        assert(peak_time_stamp > 0);

        // Check
        auto append = [](Occupy &occupy) -> bool {
            auto &gen = occupy.gen;
            auto &use = occupy.use;
            occupy.re_gen_ins.insert(gen->ins.begin(), gen->ins.end());

            // We're going to put `gen` before `use`, so we must ensure the inputs of `gen` will not change
            // printf("Begin appending\n");
            static constexpr int RE_GEN_TASK_LIMIT = 3;
            for (int i = -1; i < RE_GEN_TASK_LIMIT; ++ i) {
                bool found = false;
                OperandUsage bad_usage;
                // printf("Loop: %d\n", i);
                for (auto &usage: occupy.re_gen_ins) {
                    auto last_gen_before_re_gen = usage.next_gen;
                    while (last_gen_before_re_gen) {
                        auto &re_gen = last_gen_before_re_gen->find(usage.operand);
                        if (re_gen.next_gen and re_gen.next_gen->time_stamp < use->time_stamp) {
                            last_gen_before_re_gen = re_gen.next_gen;
                        } else {
                            break;
                        }
                    }
                    if (last_gen_before_re_gen and last_gen_before_re_gen->time_stamp < use->time_stamp) {
                        auto &re_gen = last_gen_before_re_gen->find(usage.operand);
                        if (re_gen.version != usage.version) {
                            found = true;
                            bad_usage = usage;
                            break;
                        }
                    }
                }
                if (found) {
                    // printf("Bad point (erase): %d %p\n", bad_usage.operand->id, bad_usage.gen.get());
                    occupy.re_gen.push_back(bad_usage.gen);
                    // printf("Pushing %s\n", bad_usage.gen->name.c_str());
                    // printf("Before:\n");
                    // for (auto &de: occupy.re_gen_ins) {
                    //     printf(" > %d\n", de.operand->id);
                    // }
                    occupy.re_gen_ins.erase(bad_usage);
                    // printf("After:\n");
                    // for (auto &de: occupy.re_gen_ins) {
                    //     printf(" > %d\n", de.operand->id);
                    // }
                    occupy.re_gen_ins.insert(bad_usage.gen->ins.begin(), bad_usage.gen->ins.end());
                    // for (auto &de: bad_usage.gen->ins) {
                    //     // occupy.re_gen_ins.insert(de);
                    //     printf("> Insert: %d@%s\n", de.operand->id, bad_usage.gen->name.c_str());
                    // }
                    // printf("After 2:\n");
                    // for (auto &de: occupy.re_gen_ins) {
                    //     printf(" > %d\n", de.operand->id);
                    // }
                } else {
                    return true;
                }
            }
            // printf("gen: %s\n", gen->name.c_str());
            return false;
        };

        // Get all occupying pairs
        std::set<Occupy> occupies;
        LOOP(task, head) {
            if (peak_time_stamp >= task->time_stamp) {
                continue;
            }
            for (auto &usage: task->ins) {
                if (usage.gen and usage.gen->time_stamp < peak_time_stamp) {
                    auto occupy = Occupy {usage.gen, task};
                    // .count is a must, because we only accept the first usage
                    if (not occupies.count(occupy) and append(occupy)) {
                        occupies.insert(occupy);
                    }
                }
            }
        }

        // Get scores
        std::vector<Occupy> occupies_vec;
        for (auto &occupy: occupies) {
            auto copied = occupy;
            copied.calculate(peak_time_stamp, peak_memory, origin_time);
            occupies_vec.push_back(copied);
        }

        // O1 Pruning
        std::set<Occupy> essentials;
        std::sort(occupies_vec.begin(), occupies_vec.end(), [](const Occupy &o1, const Occupy &o2) {
            return o1.score1 < o2.score1;
        });
        for (int i = 0, end = std::min<int>(O1_OCCUPIES_LIMIT, occupies_vec.size()); i < end; ++ i) {
            essentials.insert(occupies_vec[i]);
        }

        // O2 Pruning
        std::sort(occupies_vec.begin(), occupies_vec.end(), [](const Occupy &o1, const Occupy &o2) {
            return o1.score2 < o2.score2;
        });
        for (int i = 0, end = std::min<int>(O2_OCCUPIES_LIMIT, occupies_vec.size()); i < end; ++ i) {
            essentials.insert(occupies_vec[i]);
        }

        // Random
        if (not occupies_vec.empty()) {
            auto random = Random(0, occupies_vec.size());
            for (int i = 0, end = std::min<int>(RANDOM_LIMIT, occupies_vec.size()); i < end; ++ i) {
                essentials.insert(occupies_vec[random()]);
            }
        }

        // Debug print
        // for (auto &copied: essentials) {
        //     printf("[%s (%p), %s] occupies, score=%.6lf.\n", copied.gen->name.c_str(),
        //            copied.gen.get(), copied.use->name.c_str(), copied.score);
        // }
        return std::vector<Occupy>(essentials.begin(), essentials.end());
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
        analyzeTopology(head);

        // Insert .dealloc
        LOOP(task, head) {
            // Create new .dealloc
            if (not task->to_dealloc_after.empty()) {
                auto new_task = Task::dealloc(task->to_dealloc_after);
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
        for (auto &operand: operands) {
            auto operand_json = nlohmann::json::array({operand->id, operand->size});
            operands_json.push_back(operand_json);
        }

        // Push tasks
        json["records"] = nlohmann::json::array();
        auto &records_json = json["records"];
        LOOP(task, head) {
            records_json.push_back(task->toJson());
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
    std::vector<Occupy> occupies;

    // Hash
    bool hash_calculated = false;
    size_t hash_value = 0;

    std::pair<size_t, uint64_t> analyze() {
        if (not analyzed) {
            analyzed = true;
            total_time = common->analyzeTime(head);
            peak_memory = common->analyzeMemory(head);
            occupies = common->analyzeOccupies(head, peak_memory, total_time);
        }
        return std::make_pair(peak_memory, total_time);
    }

    ScheduleHandle apply(const Occupy &occupy) const {
        // Generate new
        auto new_schedule = std::make_shared<Schedule>();
        new_schedule->common = common;

        // Copy list and insert re-computation
        auto &new_head = new_schedule->head;
        TaskHandle tail;
        auto insert_back = [&new_head, &tail](const TaskHandle &task) {
            if (not tail) {
                new_head = task;
                task->prev = task->next = nullptr;
            } else {
                tail->next = task;
                task->prev = tail;
                task->next = nullptr;
            }
            tail = task;
        };
        LOOP(task, head) {
            if (task == occupy.use) {
                for (auto it = occupy.re_gen.rbegin(); it != occupy.re_gen.rend(); ++ it) {
                    insert_back((*it)->copy());
                }
                insert_back(occupy.gen->copy());
            }
            if (not (task == occupy.gen and occupy.move)) {
                insert_back(task->copy());
            }
        }

        return new_schedule;
    }

    static std::pair<ScheduleHandle, int> fromFile(const std::string &path) {
        // Read JSON
        std::ifstream file(path);
        nlohmann::json json;
        file >> json;

        // Operands
        auto schedule = std::make_shared<Schedule>();
        schedule->common = Common::fromJson(json["operands"]);

        // Records
        int count = 0;
        TaskHandle tail;
        for (auto &item: json["records"]) {
            ++ count;
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
        schedule->common->analyzePlacement(schedule->head);
        schedule->common->refactor(schedule->head);

        // Simple debugging checks
        // schedule->common->restore(schedule->head);
        // if (schedule->common->check(schedule->head)) {
        //     error("Check failed after restore.");
        // }
        // schedule->common->refactor(schedule->head);

        return std::make_pair(schedule, count);
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

    static constexpr double MEMORY_FACTOR = 0.5;
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
        size_t s1_peak_memory, s2_peak_memory;
        uint64_t s1_total_time, s2_total_time;
        std::tie(s1_peak_memory, s1_total_time) = s1->analyze();
        std::tie(s2_peak_memory, s2_total_time) = s2->analyze();
        if ((s1_peak_memory <= limit) != (s2_peak_memory <= limit)) {
            return s2_peak_memory <= limit;
        } else if (s1_peak_memory <= limit) {
            return s1_total_time > s2_total_time;
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
