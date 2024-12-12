#include "common/profiler.h"

namespace kuzu {
namespace common {

TimeMetric* Profiler::registerTimeMetric(const uint32_t id, const std::string& key, bool custom) {
    auto timeMetric = std::make_unique<TimeMetric>(enabled);
    auto metricPtr = timeMetric.get();
    addMetric(id, key, std::move(timeMetric), custom);
    return metricPtr;
}

NumericMetric* Profiler::registerNumericMetric(const uint32_t id, const std::string& key,
    bool custom) {
    auto numericMetric = std::make_unique<NumericMetric>(enabled);
    auto metricPtr = numericMetric.get();
    addMetric(id, key, std::move(numericMetric), custom);
    return metricPtr;
}

double Profiler::sumAllTimeMetricsWithKey(const uint32_t id, const std::string& key) {
    auto sum = 0.0;
    auto& metricsMap = metrics[id];
    if (!metricsMap.contains(key)) {
        return sum;
    }
    for (auto& metric : metricsMap.at(key)) {
        sum += ((TimeMetric*)metric.get())->getElapsedTimeMS();
    }
    return sum;
}

std::pair<std::unordered_map<std::string, double>, std::unordered_map<std::string, uint64_t>>
Profiler::sumAllCustomMetrics(const uint32_t id) {
    std::unordered_map<std::string, double> timeMetrics;
    std::unordered_map<std::string, uint64_t> numericMetrics;
    if (customMetrics.contains(id)) {
        for (auto& metricMap : customMetrics.at(id)) {
            if (metricMap.second.empty()) {
                continue;
            }
            auto time_metric = dynamic_cast<TimeMetric*>(metricMap.second.front().get()) != nullptr;
            if (time_metric) {
                auto sum = 0.0;
                for (auto& metric : metricMap.second) {
                    sum += ((TimeMetric*)metric.get())->getElapsedTimeMS();
                }
                timeMetrics.insert(std::make_pair(metricMap.first, sum));
            } else {
                auto sum = 0ul;
                for (auto& metric : metricMap.second) {
                    sum += ((NumericMetric*)metric.get())->accumulatedValue;
                }
                numericMetrics.insert(std::make_pair(metricMap.first, sum));
            }
        }
    }
    return {timeMetrics, numericMetrics};
}

uint64_t Profiler::sumAllNumericMetricsWithKey(const uint32_t id, const std::string& key) {
    auto sum = 0ul;
    auto& metricsMap = metrics[id];
    if (!metricsMap.contains(key)) {
        return sum;
    }
    for (auto& metric : metricsMap.at(key)) {
        sum += ((NumericMetric*)metric.get())->accumulatedValue;
    }
    return sum;
}

void Profiler::addMetric(const uint32_t id, const std::string& key, std::unique_ptr<Metric> metric,
    bool custom) {
    std::lock_guard<std::mutex> lck(mtx);
    auto& localMetrics = custom ? customMetrics : metrics;
    auto& metricsMap = localMetrics[id];
    metricsMap[key].push_back(std::move(metric));
}

} // namespace common
} // namespace kuzu
