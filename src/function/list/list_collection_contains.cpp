#include "function/list/functions/list_collection_contains.h"

#include "function/list/vector_list_functions.h"
#include "function/scalar_function.h"

using namespace kuzu::common;

namespace kuzu {
namespace function {

static std::unique_ptr<FunctionBindData> bindFunc(const binder::expression_vector& arguments,
    Function* function) {
    auto scalarFunction = ku_dynamic_cast<Function*, ScalarFunction*>(function);
    common::TypeUtils::visit(
        ListType::getChildType(arguments[0]->dataType).getPhysicalType(),
        [&arguments, &scalarFunction]<ComparableTypes T>(T) {
            scalarFunction->execFunc = ScalarFunction::BinaryExecListStructFunction<list_entry_t,
                list_entry_t, uint8_t, CollectionContains<T>>;
        },
        [](auto) { KU_UNREACHABLE; });
    return FunctionBindData::getSimpleBindData(arguments, LogicalType::BOOL());
}

function_set ListCollectionContainsFunction::getFunctionSet() {
    function_set result;
    result.push_back(std::make_unique<ScalarFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::LIST, LogicalTypeID::LIST}, LogicalTypeID::BOOL,
        nullptr, nullptr, bindFunc));
    return result;
}

} // namespace function
} // namespace kuzu