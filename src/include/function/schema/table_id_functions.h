#pragma once

#include "common/types/internal_id_t.h"

namespace kuzu {
namespace function {

struct TableID {

    static void operation(common::internalID_t& input, int64_t& result) { result = input.tableID; }
};

} // namespace function
} // namespace kuzu
