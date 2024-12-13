#pragma once

#include "common/timer.h"

namespace kuzu {
namespace common {

/**
 * Note that metrics are not thread safe.
 */
class Metric {

public:
    explicit Metric(bool enabled) : enabled{enabled} {}

    virtual ~Metric() = default;

public:
    bool enabled;
};

class KUZU_API TimeMetric : public Metric {

public:
    explicit TimeMetric(bool enable);

    void start();
    void stop();

    double getElapsedTimeMS() const;

public:
    double accumulatedTime;
    bool isStarted;
    Timer timer;
};

class KUZU_API NumericMetric : public Metric {

public:
    explicit NumericMetric(bool enable);

    void increase(uint64_t value);

    void incrementByOne();

public:
    uint64_t accumulatedValue;
};

} // namespace common
} // namespace kuzu
