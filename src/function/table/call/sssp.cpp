#include "function/table/sssp.h"

namespace kuzu {
namespace function {

static std::unique_ptr<TableFuncLocalState> initSsspLocalState(TableFunctionInitInput& input,
    TableFuncSharedState*, storage::MemoryManager*) {
    auto bindData = input.bindData->constPtrCast<SsspBindData>();
    auto resultType = bindData->resultType;
    if (resultType == "path" || resultType == "length_path") {
        return std::make_unique<SsspLocalState>();
    }
    return std::make_unique<TableFuncLocalState>();
}

static offset_t tableFunc(TableFuncInput& input, TableFuncOutput& output) {
    auto resultType = input.bindData->constPtrCast<SsspBindData>()->resultType;
    if (resultType == "length") {
        return lengthFunc(input, output);
    } else if (resultType == "count" || resultType == "length_count") {
        return countFunc(input, output);
    } else if (resultType == "path" || resultType == "length_path") {
        return pathFunc(input, output);
    } else { // single_path
        // todo why
        KU_ASSERT(false);
    }
}

static bool stringToBool(const std::string& str) {
    return str == "true" || str == "1";
}

static std::unique_ptr<TableFuncBindData> bindFunc(main::ClientContext* context,
    ScanTableFuncBindInput* input) {
    std::vector<std::string> returnColumnNames;
    std::vector<LogicalType> returnTypes;
    auto resultType = input->inputs[6].getValue<std::string>();
    if (resultType == "length") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(LogicalType::INT64());
    } else if (resultType == "count") {
        returnColumnNames.emplace_back("count");
        returnTypes.emplace_back(LogicalType::INT64());
    } else if (resultType == "path") {
        returnColumnNames.emplace_back("path");
        returnTypes.emplace_back(LogicalType::STRING());
    } else if (resultType == "path_single") {
        returnColumnNames.emplace_back("path_single");
        returnTypes.emplace_back(LogicalType::STRING());
    } else if (resultType == "length_count") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(LogicalType::INT64());
        returnColumnNames.emplace_back("count");
        returnTypes.emplace_back(LogicalType::INT64());
    } else if (resultType == "length_path") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(LogicalType::INT64());
        returnColumnNames.emplace_back("path");
        returnTypes.emplace_back(LogicalType::STRING());
    } else {
        throw BinderException("unknown mode, following modes are supported: "
                              "length, count , path , path_single , length_count , length_path");
    }
    auto direction = input->inputs[4].getValue<std::string>();
    if (direction != "in" && direction != "out" && direction != "both") {
        throw BinderException(
            "unknown direction, following directions are supported: in, out, both");
    }
    auto maxHop = input->inputs[5].getValue<int64_t>();
    KU_ASSERT(maxHop >= -1);
    maxHop = (maxHop == -1) ? INT64_MAX : maxHop;
    auto numThreads = input->inputs[7].getValue<int64_t>();
    KU_ASSERT(numThreads >= 0);
    // filter
    std::string nodeFilterStr = input->inputs[8].getValue<std::string>();
    std::string relFilterStr = input->inputs[9].getValue<std::string>();

    // 回溯先forward再backwoard.false时则是直接backward
    bool backTrackUsingFB = true;
    if (input->inputs.size() == 11) {
        backTrackUsingFB = input->inputs[10].getValue<bool>();
    }
    KU_ASSERT(nodeFilterStr.empty());

    // fixme wq 当前只支持边的属性过滤
    std::unique_ptr<evaluator::ExpressionEvaluator> relFilter = nullptr;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds = nullptr;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        relTableInfos = nullptr;
    computeRelFilter(context, relFilterStr, relFilter, relColumnTypeIds, relTableInfos);

    auto bindData = std::make_unique<SsspBindData>(context,
        input->inputs[0].getValue<std::string>(), input->inputs[1].getValue<std::string>(),
        input->inputs[2].getValue<std::string>(), input->inputs[3].getValue<std::string>(),
        direction, maxHop, resultType, numThreads, backTrackUsingFB, std::move(returnTypes),
        std::move(returnColumnNames), 1 /*  one line of results */, std::move(relFilter),
        std::move(relColumnTypeIds), std::move(relTableInfos));
    return bindData;
}

function_set GraphBspSsspFunction::getFunctionSet() {
    function_set functionSet;
    functionSet.push_back(std::make_unique<TableFunction>(name, tableFunc, bindFunc,
        initSharedState, initSsspLocalState,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::STRING, LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::INT64, LogicalTypeID::STRING, LogicalTypeID::INT64,
            LogicalTypeID::STRING, LogicalTypeID::STRING}));
    functionSet.push_back(std::make_unique<TableFunction>(name, tableFunc, bindFunc,
        initSharedState, initSsspLocalState,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::STRING, LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::INT64, LogicalTypeID::STRING, LogicalTypeID::INT64,
            LogicalTypeID::STRING, LogicalTypeID::STRING, LogicalTypeID::BOOL}));
    return functionSet;
}

} // namespace function
} // namespace kuzu