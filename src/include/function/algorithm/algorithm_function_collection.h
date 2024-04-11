#pragma once

#include "function/function.h"

namespace kuzu {
namespace function {

// Calculate the average degree for an input graph.
struct DemoAvgDegreeFunction {
    static constexpr const char* name = "DEMO_AVG_DEGREE";

    static function_set getFunctionSet();
};

struct VariableLengthPathFunction {
    static constexpr const char* name = "VARIABLE_LENGTH_PATH";

    static function_set getFunctionSet();
};

struct ShortestPathFunction {
    static constexpr const char* name = "SHORTEST_PATH";

    static function_set getFunctionSet();
};

struct PageRankFunction {
    static constexpr const char* name = "PAGE_RANK";

    static function_set getFunctionSet();
};

} // namespace function
} // namespace kuzu
