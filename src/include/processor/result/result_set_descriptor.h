#pragma once

#include "common/types/types.h"

namespace kuzu {
namespace planner {
class Schema;
} // namespace planner

namespace processor {

struct DataChunkDescriptor {
    bool isSingleState;
    std::vector<common::LogicalType> logicalTypes;
    bool isFlat;

    explicit DataChunkDescriptor(bool isSingleState, bool isFlat)
        : isSingleState{isSingleState}, isFlat{isFlat} {}
    DataChunkDescriptor(const DataChunkDescriptor& other)
        : isSingleState{other.isSingleState}, logicalTypes(other.logicalTypes),
          isFlat(other.isFlat) {}

    inline std::unique_ptr<DataChunkDescriptor> copy() const {
        return std::make_unique<DataChunkDescriptor>(*this);
    }

    std::string toString() const {
        std::string ss;
        if (isFlat) {
            ss = "flat";
        } else {
            ss = "unflat";
        }
        return ss + "[" + std::to_string(logicalTypes.size()) + "]";
    }
};

struct ResultSetDescriptor {
    std::vector<std::unique_ptr<DataChunkDescriptor>> dataChunkDescriptors;

    ResultSetDescriptor() = default;
    explicit ResultSetDescriptor(
        std::vector<std::unique_ptr<DataChunkDescriptor>> dataChunkDescriptors)
        : dataChunkDescriptors{std::move(dataChunkDescriptors)} {}
    explicit ResultSetDescriptor(planner::Schema* schema);

    std::unique_ptr<ResultSetDescriptor> copy() const;
};

} // namespace processor
} // namespace kuzu
