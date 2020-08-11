#pragma once

#include <sstream>

#include "schedule.hpp"
#include "utils.hpp"

class Optimizer {
    size_t limit;
    bool recompute_on, swap_on;

public:
    Optimizer(size_t limit, bool recompute_on, bool swap_on) {
        this->limit = limit;
        this->recompute_on = recompute_on;
        this->swap_on = swap_on;
    }

    std::string name() const {
        std::stringstream ss;
        ss << "optimizer (limit ";
        ss << prettyBytes(limit) << ", ";
        ss << "recompute " << (recompute_on ? "on" : "off") << ", ";
        ss << "swap " << (swap_on ? "on" : "off") << ")";
        return ss.str();
    }

    void optimize(const ScheduleHandle &origin) const {

    }
};