#pragma once

#include "common/types/types.h"
#include "function/hash/hash_functions.h"
#include "phmap.h"

namespace kuzu {
namespace common {

using internal_id_set_t = phmap::flat_hash_set<internalID_t, function::InternalIDHasher>;
using node_id_set_t = internal_id_set_t;
using rel_id_set_t = internal_id_set_t;
template<typename T>
using internal_id_map_t = phmap::flat_hash_map<internalID_t, T, function::InternalIDHasher>;
template<typename T>
using node_id_map_t = internal_id_map_t<T>;

} // namespace common
} // namespace kuzu
