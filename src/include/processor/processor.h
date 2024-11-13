#pragma once

#include "common/task_system/task_scheduler.h"
#include "processor/physical_plan.h"
#include "processor/processor_task.h"
#include "processor/result/factorized_table.h"

namespace kuzu {
namespace processor {

class QueryProcessor {

public:
    explicit QueryProcessor(uint64_t numThreads);

    inline common::TaskScheduler* getTaskScheduler() { return taskScheduler.get(); }

    std::shared_ptr<FactorizedTable> execute(PhysicalPlan* physicalPlan, ExecutionContext* context);

private:
    void decomposePlanIntoTask(PhysicalOperator* op, processor::ProcessorTask* task,
        ExecutionContext* context, uint64_t& ID);

    void initTask(common::Task* task);

    void appendTaskProfileInfo(std::shared_ptr<ProcessorTask> task, ExecutionContext* context);

private:
    std::unique_ptr<common::TaskScheduler> taskScheduler;
};

} // namespace processor
} // namespace kuzu
