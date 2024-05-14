#pragma once

#include "binder/expression/expression.h"
#include "function.h"

namespace kuzu {

namespace graph {
class Graph;
}
namespace processor {
class FactorizedTable;
}
namespace storage {
class MemoryManager;
}

namespace function {

// Algorithm function should implement this class to access more information at compile time.
struct AlgoFuncExtraBindData {
    virtual ~AlgoFuncExtraBindData() = default;

    virtual std::unique_ptr<AlgoFuncExtraBindData> copy() const = 0;

    template<class TARGET>
    TARGET* ptrCast() {
        return common::ku_dynamic_cast<AlgoFuncExtraBindData*, TARGET*>(this);
    }
};

// All algorithm functions need to implement algo_func_bind_t to create a AlgoFuncBindData. This
// is the minimal amount of information in order for us to compile a plan.
struct AlgoFuncBindData {
    // Output column names.
    std::vector<std::string> columnNames;
    // Output column types.
    std::vector<common::LogicalType> columnTypes;
    std::unique_ptr<AlgoFuncExtraBindData> extraData = nullptr;

    AlgoFuncBindData() = default;
    EXPLICIT_COPY_DEFAULT_MOVE(AlgoFuncBindData);

private:
    AlgoFuncBindData(const AlgoFuncBindData& other)
        : columnNames{other.columnNames}, columnTypes{other.columnTypes} {
        if (other.extraData != nullptr) {
            extraData = other.extraData->copy();
        }
    }
};

struct AlgoFuncLocalState {
    virtual ~AlgoFuncLocalState() = default;

    template<class TARGET>
    TARGET* ptrCast() {
        return common::ku_dynamic_cast<AlgoFuncLocalState*, TARGET*>(this);
    }
};

// Input to algorithm function
struct AlgoFuncInput {
    graph::Graph* graph;
    AlgoFuncBindData bindData;
    std::unique_ptr<AlgoFuncLocalState> localState;
};

using algo_func_bind_t = std::function<AlgoFuncBindData(const binder::expression_vector&)>;
using algo_func_init_local_state_t =
    std::function<std::unique_ptr<AlgoFuncLocalState>(storage::MemoryManager*)>;
using algo_func_exec_t = std::function<void(const AlgoFuncInput&, processor::FactorizedTable&)>;

struct AlgorithmFunction : public Function {
    algo_func_bind_t bindFunc;
    algo_func_init_local_state_t initLocalStateFunc;
    algo_func_exec_t execFunc;

    AlgorithmFunction(std::string name, std::vector<common::LogicalTypeID> parameterTypeIDs)
        : Function{std::move(name), std::move(parameterTypeIDs)} {}
    AlgorithmFunction(const AlgorithmFunction& other)
        : Function{other}, bindFunc{other.bindFunc}, initLocalStateFunc{other.initLocalStateFunc},
          execFunc{other.execFunc} {}

    std::unique_ptr<Function> copy() const override {
        return std::make_unique<AlgorithmFunction>(*this);
    }
};

} // namespace function
} // namespace kuzu
