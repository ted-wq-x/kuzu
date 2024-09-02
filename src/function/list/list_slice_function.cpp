
#include "function/list/functions/list_slice_function.h"

#include "function/list/vector_list_functions.h"
#include "function/scalar_function.h"

using namespace kuzu::common;

namespace kuzu {
namespace function {

static std::unique_ptr<FunctionBindData> bindFunc(ScalarBindFuncInput input) {
    KU_ASSERT(input.arguments.size() == 3);
    std::vector<LogicalType> paramTypes;
    paramTypes.push_back(input.arguments[0]->getDataType().copy());
    paramTypes.push_back(LogicalType(input.definition->parameterTypeIDs[1]));
    paramTypes.push_back(LogicalType(input.definition->parameterTypeIDs[2]));
    return std::make_unique<FunctionBindData>(std::move(paramTypes),
        input.arguments[0]->getDataType().copy());
}

function_set ListSliceFunction::getFunctionSet() {
    function_set result;
    result.push_back(std::make_unique<ScalarFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::LIST, LogicalTypeID::INT64, LogicalTypeID::INT64},
        LogicalTypeID::LIST,
        ScalarFunction::TernaryExecListStructFunction<list_entry_t, int64_t, int64_t, list_entry_t,
            ListSlice>,
        nullptr /* selectFunc */, bindFunc));
    result.push_back(std::make_unique<ScalarFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING, LogicalTypeID::INT64,
            LogicalTypeID::INT64},
        LogicalTypeID::STRING,
        ScalarFunction::TernaryExecListStructFunction<ku_string_t, int64_t, int64_t, ku_string_t,
            ListSlice>,
        nullptr /* selectFunc */, bindFunc));
    return result;
}

static std::unique_ptr<FunctionBindData> ListLastBindFunc(ScalarBindFuncInput input) {
    KU_ASSERT(input.arguments.size() == 1);
    std::vector<LogicalType> paramTypes;
    paramTypes.push_back(input.arguments[0]->getDataType().copy());
    return std::make_unique<FunctionBindData>(std::move(paramTypes),
        input.arguments[0]->getDataType().copy());
}

function_set ListTailFunction::getFunctionSet() {
    function_set result;
    result.push_back(std::make_unique<ScalarFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::LIST}, LogicalTypeID::LIST,
        ScalarFunction::UnaryExecListStructFunction<list_entry_t, list_entry_t, ListTail>, nullptr,
        ListLastBindFunc));
    return result;
}

} // namespace function
} // namespace kuzu
