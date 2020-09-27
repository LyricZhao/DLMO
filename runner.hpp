#pragma once

#include <iostream>
#include <string>
#include <utility>

#include "optimizer.hpp"
#include "schedule.hpp"
#include "utils.hpp"

class Runner {
    std::string input, output;
    size_t limit;

public:
    Runner(const std::string &input, const std::string &output, size_t limit): input(input), output(output), limit(limit) {}

    void run() {
        ScheduleHandle schedule;
        int count;
        std::tie(schedule, count) = Schedule::fromFile(input);
        auto optimizer = Optimizer(limit);
        printf("Running case %s (%d operators) with %s ... \n", input.c_str(), count, optimizer.name().c_str());
        optimizer.optimize(schedule, output);
    }
};
