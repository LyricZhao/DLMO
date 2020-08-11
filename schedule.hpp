#pragma once

#include <memory>

class Schedule;
typedef std::shared_ptr<Schedule> ScheduleHandle;

class Schedule {
public:
    static ScheduleHandle fromFile(const std::string &path) {
        return std::make_shared<Schedule>();
    }
};
