#pragma once

#include "common/task_system/task.h"
#include "processor/operator/sink.h"

namespace kuzu {
namespace processor {

class ProcessorTask : public common::Task {
    friend class QueryProcessor;

public:
    ProcessorTask(Sink* sink, ExecutionContext* executionContext, uint64_t ID);

    void run() override;
    void finalizeIfNecessary() override;
    std::string profilePrintString() const override;

    static std::unique_ptr<ResultSet> populateResultSet(Sink* op,
        storage::MemoryManager* memoryManager);

private:
    bool sharedStateInitialized;
    Sink* sink;
    ExecutionContext* executionContext;
};

} // namespace processor
} // namespace kuzu
