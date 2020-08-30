#pragma once

#include <iostream>
#include <string>
#include <utility>

#include "optimizer.hpp"
#include "schedule.hpp"
#include "utils.hpp"

class Runner {
    std::string path;
    size_t limit;

public:
    Runner(std::string path, size_t limit): path(std::move(path)), limit(limit) {}

    void run() {
        ScheduleHandle schedule;
        int count;
        std::tie(schedule, count) = Schedule::fromFile(path);
        auto optimizer = Optimizer(limit);
        printf("Running case %s (%d operators) with %s ... \n", path.c_str(), count, optimizer.name().c_str());
        optimizer.optimize(schedule);
    }
};
