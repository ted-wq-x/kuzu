#include <future>
#include <thread>

#include "common/types/internal_id_util.h"
#include "function/table/basic.h"

namespace kuzu {
namespace function {

struct SsspLocalState : public TableFuncLocalState {
    int64_t length;
    std::vector<std::string> resultVector;
};

struct SsspBindData : public CallTableFuncBindData {
    SsspBindData(main::ClientContext* context, std::string srcPrimaryKey, std::string srcTableName,
        std::string dstPrimaryKey, std::string dstTableName, std::string direction, int64_t maxHop,
        std::string resultType, int64_t numThreads, bool isParameter,
        std::vector<LogicalType> returnTypes, std::vector<std::string> returnColumnNames,
        offset_t maxOffset, std::unique_ptr<evaluator::ExpressionEvaluator> relFilter,
        std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds,
        std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
            relTableInfos)
        : CallTableFuncBindData{std::move(returnTypes), std::move(returnColumnNames), maxOffset},
          context(context), srcPrimaryKey(srcPrimaryKey), srcTableName(srcTableName),
          dstPrimaryKey(dstPrimaryKey), dstTableName(dstTableName), direction(direction),
          maxHop(maxHop), resultType(resultType), numThreads(numThreads), isParameter(isParameter),
          relFilter(std::move(relFilter)), relColumnTypeIds(std::move(relColumnTypeIds)),
          relTableInfos(std::move(relTableInfos)) {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        std::unique_ptr<evaluator::ExpressionEvaluator> localRelFilter = nullptr;
        if (relFilter) {
            localRelFilter = relFilter->clone();
        }

        return std::make_unique<SsspBindData>(context, srcPrimaryKey, srcTableName, dstPrimaryKey,
            dstTableName, direction, maxHop, resultType, numThreads, isParameter,
            LogicalType::copy(columnTypes), columnNames, maxOffset, std::move(localRelFilter),
            relColumnTypeIds, relTableInfos);
    }
    main::ClientContext* context;
    std::string srcPrimaryKey, srcTableName, dstPrimaryKey, dstTableName, direction;
    int64_t maxHop;
    std::string resultType;
    int64_t numThreads;
    bool isParameter;

    // nullable
    std::unique_ptr<evaluator::ExpressionEvaluator> relFilter;
    // 边的输出列类型
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        relTableInfos;
};

class SsspSharedData {
public:
    explicit SsspSharedData(const SsspBindData* bindData, int64_t numThreads)
        : numThreads(numThreads) {
        context = bindData->context;
        relFilter = bindData->relFilter.get();
        relColumnTypeIds = bindData->relColumnTypeIds;
        reltables = bindData->relTableInfos;
    }

    std::unique_ptr<processor::ResultSet> createResultSet() {
        processor::ResultSet resultSet(1);
        auto columnType = *relColumnTypeIds.get();
        auto numValueVectors = columnType.size();
        auto dataChunk = std::make_shared<common::DataChunk>(numValueVectors);
        for (auto j = 0u; j < numValueVectors; ++j) {
            dataChunk->insert(j,
                std::make_shared<ValueVector>(columnType[j], context->getMemoryManager()));
        }
        resultSet.insert(0, dataChunk);
        return std::make_unique<processor::ResultSet>(resultSet);
    }

    std::unique_ptr<evaluator::ExpressionEvaluator> initEvaluator(
        const processor::ResultSet& resultSet) {
        if (relFilter) {
            auto evaluator = relFilter->clone();
            evaluator->init(resultSet, context);
            return evaluator;
        } else {
            return nullptr;
        }
    }

    int64_t numThreads;
    main::ClientContext* context;

    //    点个数的value
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        reltables;
    evaluator::ExpressionEvaluator* relFilter;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds;
};

class SharedDataWithValue : public SsspSharedData {
public:
    explicit SharedDataWithValue(const SsspBindData* bindData, int64_t numThreads)
        : SsspSharedData(bindData, numThreads) {
        threadBitSets = std::make_unique<std::shared_ptr<InternalIDCountBitSet>[]>(numThreads);
    }

    std::shared_ptr<InternalIDCountBitSet> getThreadBitSet(uint32_t threadId) {
        auto threadBitSet = threadBitSets[threadId];
        if (threadBitSet == nullptr) {
            auto localBitSet = std::make_shared<InternalIDCountBitSet>(context->getCatalog(),
                context->getStorageManager(), context->getTx());
            threadBitSets[threadId] = localBitSet;
            return localBitSet;
        } else {
            return threadBitSet;
        }
    }
    std::unique_ptr<std::shared_ptr<InternalIDCountBitSet>[]> threadBitSets;
};

class SharedDataWithoutValue : public SsspSharedData {
public:
    explicit SharedDataWithoutValue(const SsspBindData* bindData, int64_t numThreads)
        : SsspSharedData(bindData, numThreads) {
        threadBitSets = std::make_unique<std::shared_ptr<InternalIDBitSet>[]>(numThreads);
    }

    std::shared_ptr<InternalIDBitSet> getThreadBitSet(uint32_t threadId) {
        auto threadBitSet = threadBitSets[threadId];
        if (threadBitSet == nullptr) {
            auto localBitSet = std::make_shared<InternalIDBitSet>(context->getCatalog(),
                context->getStorageManager(), context->getTx());
            threadBitSets[threadId] = localBitSet;
            return localBitSet;
        } else {
            return threadBitSet;
        }
    }
    std::unique_ptr<std::shared_ptr<InternalIDBitSet>[]> threadBitSets;
};

offset_t countFunc(TableFuncInput& input, TableFuncOutput& output);
offset_t lengthFunc(TableFuncInput& input, TableFuncOutput& output);
offset_t pathFunc(TableFuncInput& input, TableFuncOutput& output);

} // namespace function
} // namespace kuzu