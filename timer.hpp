#pragma once

#include <chrono>

class Timer {
    typedef std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> NanoTimePoint;

    NanoTimePoint lastTimePoint;

public:
    Timer() {
        lastTimePoint = std::chrono::system_clock::now();
    }

    uint64_t tik() {
        NanoTimePoint timePoint = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint - lastTimePoint);
        lastTimePoint = timePoint;
        return duration.count();
    }
};