#include "function/table/khop.h"

namespace kuzu {
namespace function {

static common::offset_t tableFunc(TableFuncInput& input, TableFuncOutput& output) {
    return rewriteTableFunc(input, output, 0);
}

static std::unique_ptr<TableFuncBindData> bindFunc(main::ClientContext* context,
    TableFuncBindInput* input) {
    return rewriteBindFunc(context, input, {"khop_num"});
}

function_set GraphBspKhopFunction::getFunctionSet() {
    function_set functionSet;
    functionSet.push_back(std::make_unique<TableFunction>(name, tableFunc, bindFunc,
        initSharedState, initEmptyLocalState,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::STRING, LogicalTypeID::INT64, LogicalTypeID::INT64, LogicalTypeID::INT64,
            LogicalTypeID::STRING, LogicalTypeID::STRING}));
    return functionSet;
}

} // namespace function
} // namespace kuzu
