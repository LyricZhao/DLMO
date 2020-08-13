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
    bool use_recompute, use_swap;

public:
    explicit Runner(std::string path, size_t limit, bool use_recompute, bool use_swap):
        path(std::move(path)), limit(limit), use_recompute(use_recompute), use_swap(use_swap) {}

    void run() {
        auto schedule = Schedule::fromFile(path);
        auto optimizer = Optimizer(limit, use_recompute, use_swap);
        printf("Running case %s with %s ... \n", path.c_str(), optimizer.name().c_str());
        optimizer.optimize(schedule);
    }
};
