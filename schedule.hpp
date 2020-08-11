#pragma once

#include <cassert>
#include <memory>

#include "json.hpp"
#include "utils.hpp"

static constexpr size_t PCIE_GBPS = Unit::GiB(12);
static constexpr double PCIE_LATENCY = Unit::ms(0.02);

enum TaskType {
    Compute,
    Transfer,
    Sync
};

class Task;
typedef std::shared_ptr<Task> TaskHandle;

class Task {
    std::string name = "none";
    uint64_t time = 0;
    size_t workspace = 0;
    TaskType type = Compute;
    TaskHandle reference;
    std::vector<int> ins, outs;

public:
    static TaskHandle fromJson(const nlohmann::json &json) {
        auto task = std::make_shared<Task>();
        auto fill = [](std::vector<int> &vec, const nlohmann::json &array) {
            vec.resize(array.size());
            for (int i = 0; i < vec.size(); ++ i) {
                vec[i] = array[i];
            }
        };

        // Fill task
        task->type = Compute;
        task->name = json[0];
        fill(task->ins, json[1]);
        fill(task->outs, json[2]);
        task->workspace = json[3];
        task->time = Unit::ms(static_cast<double>(json[4]));

        // TODO: JSON file has .host2device-like operators
        assert(task->name != ".host2device");
        assert(task->name != ".device2host");
        return task;
    }
};

typedef std::vector<TaskHandle> TimePoint;

class Schedule;
typedef std::shared_ptr<Schedule> ScheduleHandle;

class Schedule {
    bool calculated = false;
    size_t peak_memory = 0;
    double total_time = 0;
    std::vector<size_t> operands;
    std::vector<TimePoint> schedule;

public:
    std::pair<size_t, double> statistics() {
        if (not calculated) {
            // TODO: calculate
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
            operands[id] = size;
        }

        // Records
        for (auto &item: json["records"]) {
            auto task = Task::fromJson(item);
            schedule->schedule.push_back(TimePoint{task});
        }

        return schedule;
    }
};
