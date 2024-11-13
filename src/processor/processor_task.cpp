#include "processor/processor_task.h"

#include "main/settings.h"

using namespace kuzu::common;

namespace kuzu {
namespace processor {

ProcessorTask::ProcessorTask(Sink* sink, ExecutionContext* executionContext, uint64_t ID)
    : Task{executionContext->clientContext->getCurrentSetting(main::ThreadsSetting::name)
               .getValue<uint64_t>(),
          ID},
      sharedStateInitialized{false}, sink{sink}, executionContext{executionContext} {}

void ProcessorTask::run() {
    // We need the lock when cloning because multiple threads can be accessing to clone,
    // which is not thread safe
    lock_t lck{taskMtx};
    auto taskProfileInfo = executionContext->profiler->taskProfileInfos[ID];
    if (!sharedStateInitialized) {
        sink->initGlobalState(executionContext);
        sharedStateInitialized = true;
        taskProfileInfo->stageTimeMetricStart();
    }
    auto taskTimeMetric = taskProfileInfo->appendTaskTimeMetrics();
    taskTimeMetric->start();
    auto clonedPipelineRoot = sink->clone();
    lck.unlock();
    auto currentSink = (Sink*)clonedPipelineRoot.get();
    auto resultSet =
        populateResultSet(currentSink, executionContext->clientContext->getMemoryManager());
    currentSink->execute(resultSet.get(), executionContext);
    taskTimeMetric->stop();
}

void ProcessorTask::finalizeIfNecessary() {
    auto resultSet = populateResultSet(sink, executionContext->clientContext->getMemoryManager());
    sink->initLocalState(resultSet.get(), executionContext);
    executionContext->clientContext->getProgressBar()->finishPipeline(executionContext->queryID);
    sink->finalize(executionContext);
    executionContext->profiler->taskProfileInfos[ID]->stageTimeMetricStop();
}

std::string ProcessorTask::profilePrintString() const {
    auto msg = common::Task::profilePrintString();
    msg += "[" + std::to_string(sink->getOperatorID());
    auto root = sink->getChild(0);
    while (!root->isSink()) {
        msg += "-" + std::to_string(root->getOperatorID());
        if (root->getNumChildren() == 1) {
            root = root->getChild(0);
        } else {
            break;
        }
    }
    msg += "]";
    return msg;
}

std::unique_ptr<ResultSet> ProcessorTask::populateResultSet(Sink* op,
    storage::MemoryManager* memoryManager) {
    auto resultSetDescriptor = op->getResultSetDescriptor();
    if (resultSetDescriptor == nullptr) {
        // Some pipeline does not need a resultSet, e.g. OrderByMerge
        return nullptr;
    }
    return std::make_unique<ResultSet>(resultSetDescriptor, memoryManager);
}

} // namespace processor
} // namespace kuzu
