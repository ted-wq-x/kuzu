#pragma once

#include "binder/expression/expression.h"
#include "function/algorithm_function.h"
#include "graph/graph.h"
#include "processor/operator/sink.h"

namespace kuzu {
namespace processor {

struct AlgorithmCallSharedState {
    std::mutex mtx;
    std::shared_ptr<FactorizedTable> fTable;
    std::unique_ptr<graph::Graph> graph;

    explicit AlgorithmCallSharedState(std::shared_ptr<FactorizedTable> fTable)
        : fTable{std::move(fTable)} {}
};

struct AlgorithmCallInfo {
    function::AlgorithmFunction function;
    function::AlgoFuncBindData bindData;
    std::shared_ptr<binder::Expression> graphExpr;

    AlgorithmCallInfo(function::AlgorithmFunction function, function::AlgoFuncBindData bindData,
        std::shared_ptr<binder::Expression> graphExpr)
        : function{std::move(function)}, bindData{std::move(bindData)},
          graphExpr{std::move(graphExpr)} {}
    EXPLICIT_COPY_DEFAULT_MOVE(AlgorithmCallInfo);

private:
    AlgorithmCallInfo(const AlgorithmCallInfo& other)
        : function{other.function}, bindData{other.bindData.copy()}, graphExpr{other.graphExpr} {}
};

// TODO(Xiyang): add getProgress.
class AlgorithmCall : public Sink {
    static constexpr PhysicalOperatorType operatorType_ = PhysicalOperatorType::ALGORITHM;

public:
    AlgorithmCall(std::unique_ptr<ResultSetDescriptor> descriptor, AlgorithmCallInfo info,
        std::shared_ptr<AlgorithmCallSharedState> sharedState, uint32_t id,
        const std::string& paramsString)
        : Sink{std::move(descriptor), operatorType_, id, paramsString}, info{std::move(info)},
          sharedState{std::move(sharedState)} {}

    bool isSource() const override { return true; }

    bool canParallel() const override { return false; }

    void initLocalStateInternal(ResultSet*, ExecutionContext*) override;

    void initGlobalStateInternal(ExecutionContext*) override;

    void executeInternal(ExecutionContext* context) override;

    std::unique_ptr<PhysicalOperator> clone() override {
        return std::make_unique<AlgorithmCall>(resultSetDescriptor->copy(), info.copy(),
            sharedState, id, paramsString);
    }

private:
    AlgorithmCallInfo info;
    std::shared_ptr<AlgorithmCallSharedState> sharedState;
    function::AlgoFuncInput algoFuncInput;
};

} // namespace processor
} // namespace kuzu
