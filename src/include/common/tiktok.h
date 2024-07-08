#pragma once

#include <chrono>
#include <string>

#include "common/assert.h"
#include "exception/exception.h"

namespace kuzu {
namespace common {

class TikTok {

public:
    void tik() {
        finished = false;
        startTime = std::chrono::high_resolution_clock::now();
    }

    void tok() {
        auto stopTime = std::chrono::high_resolution_clock::now();
        auto duration = stopTime - startTime;
        runningTime +=
            (double)std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        finished = true;
    }

    void add(TikTok& other) { runningTime += other.runningTime; }

    double getElapsedTimeInMS() const {
        KU_ASSERT(finished);
        return runningTime / 1e6;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    bool finished = true;
    double runningTime = 0;
};

} // namespace common
} // namespace kuzu
