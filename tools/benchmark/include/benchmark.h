#pragma once

#include "benchmark_config.h"
#include "main/kuzu.h"

namespace kuzu {
namespace benchmark {

/**
 * Each benchmark represents a query to be executed against the system
 */
class Benchmark {

public:
    Benchmark(const std::string& benchmarkPath, main::Database* database, BenchmarkConfig& config);

    std::unique_ptr<main::QueryResult> run() const;
    std::unique_ptr<main::QueryResult> runWithProfile() const;
    void log(uint32_t runNum, main::QueryResult& queryResult) const;

private:
    void loadBenchmark(const std::string& benchmarkPath);
    void writeLogFile(std::ofstream& log, uint32_t runNum, const main::QuerySummary& querySummary,
        const std::vector<std::string>& actualOutput) const;
    void verify(const std::vector<std::string>& actualOutput) const;

public:
    BenchmarkConfig& config;
    std::unique_ptr<main::Connection> conn;
    std::string name;
    std::string preRun;
    std::string query;
    std::string postRun;
    std::vector<std::string> expectedOutput;
    bool compareResult;
    uint64_t expectedNumTuples;
};

} // namespace benchmark
} // namespace kuzu
