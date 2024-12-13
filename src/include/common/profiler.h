#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/metric.h"

namespace kuzu {
namespace common {

class KUZU_API Profiler {
    using metrics_map = std::unordered_map<std::string, std::vector<std::unique_ptr<Metric>>>;

public:
    TimeMetric* registerTimeMetric(uint32_t id, const std::string& key, bool custom = false);

    NumericMetric* registerNumericMetric(uint32_t id, const std::string& key, bool custom = false);

    double sumAllTimeMetricsWithKey(uint32_t id, const std::string& key);

    uint64_t sumAllNumericMetricsWithKey(uint32_t id, const std::string& key);

    std::pair<std::unordered_map<std::string, double>, std::unordered_map<std::string, uint64_t>>
    sumAllCustomMetrics(uint32_t id);

private:
    void addMetric(uint32_t id, const std::string& key, std::unique_ptr<Metric> metric,
        bool custom);

public:
    std::mutex mtx;
    bool enabled = false;
    std::unordered_map<uint32_t, metrics_map> metrics;
    std::unordered_map<uint32_t, metrics_map> customMetrics;
};

} // namespace common
} // namespace kuzu
