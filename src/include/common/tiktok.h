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
        runningTime += std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        finished = true;
    }

//    double getDuration() const {
//        if (finished) {
//            auto duration = stopTime - startTime;
//            return (double)std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
//        }
//        throw Exception("Timer is still running.");
//    }

    uint64_t getElapsedTimeInMS() const {
        KU_ASSERT(finished);
        return runningTime;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    //    std::chrono::time_point<std::chrono::high_resolution_clock> stopTime;
    bool finished = true;
    uint64_t runningTime = 0;
};

} // namespace common
} // namespace kuzu
