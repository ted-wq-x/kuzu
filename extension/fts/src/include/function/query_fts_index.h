#pragma once

#include "function/function.h"
#include "function/table/bind_data.h"
#include "function/table/call_functions.h"
#include "function/table_functions.h"

namespace kuzu {
namespace fts_extension {

struct QueryFTSFunction : function::CallFunction {
    static constexpr const char* name = "QUERY_FTS_INDEX";

    static function::function_set getFunctionSet();
};

} // namespace fts_extension
} // namespace kuzu