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
        auto schedule = Schedule::fromFile(path);
        auto optimizer = Optimizer(limit);
        printf("Running case %s with %s ... \n", path.c_str(), optimizer.name().c_str());
        optimizer.optimize(schedule);
    }
};
