#pragma once

#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "common/metric.h"
#include "common/string_utils.h"
#include "common/task_system/task.h"
#include "spdlog/fmt/fmt.h"

namespace kuzu {
namespace common {

class TaskProfileInfo {

private:
    std::shared_ptr<Task> task;
    // Record the total time of the stage, not the total time of all tasks in this stage
    common::TimeMetric stageTimeMetric;

    // The time of task in this stage
    std::vector<std::shared_ptr<common::TimeMetric>> taskTimeMetrics;

    bool enable;

    std::mutex mtx;

public:
    TaskProfileInfo(std::shared_ptr<Task> task, bool enable)
        : task{task}, stageTimeMetric{enable}, enable{enable} {}

    void stageTimeMetricStart() { stageTimeMetric.start(); }
    void stageTimeMetricStop() { stageTimeMetric.stop(); }

    std::shared_ptr<common::TimeMetric> appendTaskTimeMetrics() {
        std::lock_guard<std::mutex> lck(mtx);
        taskTimeMetrics.push_back(std::make_shared<common::TimeMetric>(enable));
        return taskTimeMetrics.back();
    }

private:
    static std::string format(std::string value, size_t size) {
        std::ostringstream oss;
        oss << std::setw(size) << value;
        return oss.str();
    }

public:
    static std::vector<std::string> metricNames(std::vector<size_t> formatVector) {
        KU_ASSERT(formatVector.size() == 6);
        std::vector<std::string> names;
        names.push_back(format("TaskID", formatVector[0]));
        names.push_back(format("MinTaskTime", formatVector[1]));
        names.push_back(format("MaxTaskTime", formatVector[2]));
        names.push_back(format("TotalTaskTime", formatVector[3]));
        names.push_back(format("StageTime", formatVector[4]));
        names.push_back(format("TaskParallelism", formatVector[5]));
        return names;
    }

    std::vector<std::string> metricValueString(std::vector<size_t> formatVector) const {
        KU_ASSERT(formatVector.size() == 6);
        std::vector<std::string> values;
        auto [minTaskTime, maxTaskTime, totalTaskTime] = getAllTaskTime();
        values.push_back(format(task->profilePrintString(), formatVector[0]));
        values.push_back(format(fmt::format("{:.2f}ms", minTaskTime), formatVector[1]));
        values.push_back(format(fmt::format("{:.2f}ms", maxTaskTime), formatVector[2]));
        values.push_back(format(fmt::format("{:.2f}ms", totalTaskTime), formatVector[3]));
        values.push_back(
            format(fmt::format("{:.2f}ms", stageTimeMetric.accumulatedTime), formatVector[4]));
        values.push_back(format(std::to_string(task->getNumThreadsRegistered()), formatVector[5]));
        return values;
    }

private:
    std::tuple<double, double, double> getAllTaskTime() const {
        double total = 0, max = 0, min = std::numeric_limits<double>::max();
        for (const auto& taskTimeMetric : taskTimeMetrics) {
            auto time = taskTimeMetric->accumulatedTime;
            if (time > max) {
                max = time;
            }
            if (min > time) {
                min = time;
            }
            total += time;
        }
        return {min, max, total};
    }
};

class Profiler {

public:
    TimeMetric* registerTimeMetric(const std::string& key);

    NumericMetric* registerNumericMetric(const std::string& key);

    double sumAllTimeMetricsWithKey(const std::string& key);

    uint64_t sumAllNumericMetricsWithKey(const std::string& key);

    std::shared_ptr<TaskProfileInfo> addProfileTask(std::shared_ptr<Task> profileTask);

    std::string printTaskProfileInfo() {
        std::string planInString;
        std::vector<size_t> defaultLengthVector;
        std::vector<size_t> lengthVector;
        defaultLengthVector.resize(6, 0);
        lengthVector.resize(6, 0);
        auto titleNames = TaskProfileInfo::metricNames(defaultLengthVector);
        for (auto i = 0u; i < titleNames.size(); ++i) {
            lengthVector[i] = titleNames[i].size();
        }
        for (const auto& [_, taskProfileInfo] : taskProfileInfos) {
            auto values = taskProfileInfo->metricValueString(defaultLengthVector);
            for (auto i = 0u; i < titleNames.size(); ++i) {
                lengthVector[i] = std::max(lengthVector[i], values[i].size());
            }
        }
        titleNames = TaskProfileInfo::metricNames(lengthVector);
        auto title = common::StringUtils::join(titleNames, "|");
        planInString += std::string(title.size() + 2, '-') + "\n";
        planInString += "|" + title + "|\n";
        for (const auto& [_, taskProfileInfo] : taskProfileInfos) {
            planInString +=
                "|" +
                common::StringUtils::join(taskProfileInfo->metricValueString(lengthVector), "|") +
                "|\n";
        }
        planInString += std::string(title.size() + 2, '-');
        return planInString;
    }

private:
    void addMetric(const std::string& key, std::unique_ptr<Metric> metric);

public:
    std::mutex mtx;
    bool enabled = false;
    std::unordered_map<std::string, std::vector<std::unique_ptr<Metric>>> metrics;
    // Key:task id
    std::map<uint64_t, std::shared_ptr<TaskProfileInfo>> taskProfileInfos;
};

} // namespace common
} // namespace kuzu
