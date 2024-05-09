#pragma once

#include <set>

#include "common/type_utils.h"
#include "common/types/types.h"
#include "common/vector/value_vector.h"

namespace kuzu {
namespace function {

template<typename T>
struct CollectionContains {
    static inline void operation(common::list_entry_t& left, common::list_entry_t& right,
        uint8_t& result, common::ValueVector& leftVector, common::ValueVector& rightVector,
        common::ValueVector& /*resultVector*/) {
        if (common::ListType::getChildType(leftVector.dataType) !=
            common::ListType::getChildType(rightVector.dataType)) {
            result = 0;
            return;
        }
        auto leftDataVector = common::ListVector::getDataVector(&leftVector);
        std::multiset<T> leftSet;
        for (auto i = 0u; i < left.size; i++) {
            if (!leftDataVector->isNull(left.offset + i)) {
                leftSet.insert(leftDataVector->getValue<T>(left.offset + i));
            }
        }
        auto rightDataVector = common::ListVector::getDataVector(&rightVector);
        std::multiset<T> rightSet;
        for (auto i = 0u; i < right.size; i++) {
            if (!rightDataVector->isNull(right.offset + i)) {
                rightSet.insert(rightDataVector->getValue<T>(right.offset + i));
            }
        }
        result = std::includes(leftSet.begin(), leftSet.end(), rightSet.begin(), rightSet.end());
    }
};

} // namespace function
} // namespace kuzu