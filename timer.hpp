#pragma once

#include <chrono>

class Timer {
    typedef std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> NanoTimePoint;

    NanoTimePoint lastTimePoint_;

public:
    Timer() {
        lastTimePoint_ = std::chrono::system_clock::now();
    }

    uint64_t tik() {
        NanoTimePoint timePoint = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint - lastTimePoint_);
        lastTimePoint_ = timePoint;
        return duration.count();
    }
};