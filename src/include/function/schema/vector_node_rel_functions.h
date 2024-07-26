#pragma once

#include "function/function.h"

namespace kuzu {
namespace function {

struct TableIDFunction {
    static constexpr const char* name = "TABLE_ID";

    static function_set getFunctionSet();
};

struct OffsetFunction {
    static constexpr const char* name = "OFFSET";

    static function_set getFunctionSet();
};

struct IDFunction {
    static constexpr const char* name = "ID";

    static function_set getFunctionSet();
};

struct StartNodeFunction {
    static constexpr const char* name = "START_NODE";
};

struct EndNodeFunction {
    static constexpr const char* name = "END_NODE";
};

struct UIDFunction {
    static constexpr const char* name = "UID";

    static function_set getFunctionSet();
};

} // namespace function
} // namespace kuzu
