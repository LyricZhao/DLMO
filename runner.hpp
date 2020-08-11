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
    bool recompute_on, swap_on;

public:
    explicit Runner(std::string path, size_t limit, bool recompute_on, bool swap_on):
        path(std::move(path)), limit(limit), recompute_on(recompute_on), swap_on(swap_on) {}

    void run() {
        auto schedule = Schedule::fromFile(path);
        auto optimizer = Optimizer(limit, recompute_on, swap_on);
        std::cout << "Running case " << path << " with " << optimizer.name() << " ... ";
        optimizer.optimize(schedule);
        std::cout << "done !" << std::endl;
    }
};

typedef std::shared_ptr<Runner> RunnerHandle;