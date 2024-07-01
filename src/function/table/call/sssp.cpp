#include <future>
#include <thread>

//#include "common/tiktok.h"
#include "common/types/internal_id_util.h"
#include "function/table/basic.h"

namespace kuzu {
namespace function {

struct SsspBindData : public CallTableFuncBindData {
    SsspBindData(main::ClientContext* context, std::string srcPrimaryKey, std::string srcTableName,
        std::string dstPrimaryKey, std::string dstTableName, std::string direction, int64_t maxHop,
        std::string mode, int64_t numThreads, std::unordered_set<std::string> nodeFilter,
        std::unordered_set<std::string> relFilter, std::vector<LogicalType> returnTypes,
        std::vector<std::string> returnColumnNames, offset_t maxOffset)
        : CallTableFuncBindData{std::move(returnTypes), std::move(returnColumnNames), maxOffset},
          context(context), srcPrimaryKey(srcPrimaryKey), srcTableName(srcTableName),
          dstPrimaryKey(dstPrimaryKey), dstTableName(dstTableName), direction(direction),
          maxHop(maxHop), mode(mode), numThreads(numThreads), nodeFilter(std::move(nodeFilter)),
          relFilter(std::move(relFilter)) {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        return std::make_unique<SsspBindData>(context, srcPrimaryKey, srcTableName, dstPrimaryKey,
            dstTableName, direction, maxHop, mode, numThreads, nodeFilter, relFilter,
            common::LogicalType::copy(columnTypes), columnNames, maxOffset);
    }
    main::ClientContext* context;
    std::string srcPrimaryKey, srcTableName, dstPrimaryKey, dstTableName, direction;
    int64_t maxHop;
    std::string mode;
    int64_t numThreads;
    std::unordered_set<std::string> nodeFilter, relFilter;
};

struct UnionResult {
    uint64_t ans = 0;
    uint64_t nodeCount = 0;
    std::vector<std::vector<nodeID_t>> nextMission;
//    uint64_t s1;
//    uint64_t s2;
};

struct Value {
    uint64_t mark = 0;
    uint32_t count[64] = {0};

    inline void merge(Value& other) {
        mark |= other.mark;

        for (auto i = 0u; i < 64; ++i) {
            count[i] += other.count[i];
        }
    }

    inline void reset() {
        mark = 0;
        std::memset(&count, 0, sizeof(count));
    }
};

class InternalIDValueBitSet {
public:
    InternalIDValueBitSet(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto maxNodeTableID = *std::max_element(nodeTableIDs.begin(), nodeTableIDs.end()) + 1;
        nodeIDMark.resize(maxNodeTableID);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size);
        }
    }
    // fixme copy new one,skip calc some additional info

    inline bool isVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        return (nodeIDMark[nodeID.tableID][block].mark >> pos) & 1;
    }

    inline void markVisited(internalID_t& nodeID, uint32_t count) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        value.mark |= (1ULL << pos);
        value.count[pos] += count;
    }

    inline uint32_t getNodeValueCount(internalID_t& nodeID) const {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        return nodeIDMark[nodeID.tableID][block].count[pos];
    }

    inline Value& getNodeValue(uint32_t tableID, uint32_t pos) { return nodeIDMark[tableID][pos]; }

    inline uint32_t getTableNum() { return nodeIDMark.size(); }

    inline uint32_t getTableSize(uint32_t tableID) { return nodeIDMark[tableID].size(); }

private:
    // tableID==>blockID==>mark
    std::vector<std::vector<Value>> nodeIDMark;
};

class SsspSharedData {
public:
    explicit SsspSharedData(const SsspBindData* bindData, int64_t numThreads)
        : numThreads(numThreads) {
        context = bindData->context;
        tx = context->getTx();
        auto catalog = context->getCatalog();

        for (auto& nodeTableName : bindData->nodeFilter) {
            auto tableID = catalog->getTableID(context->getTx(), nodeTableName);
            nodeFilter.insert(tableID);
        }
        std::unordered_set<table_id_t> relFilter;
        for (auto& relTableName : bindData->relFilter) {
            auto tableID = catalog->getTableID(context->getTx(), relTableName);
            relFilter.insert(tableID);
        }
        hasNodeFilter = !nodeFilter.empty();
        bool hasRelFilter = !relFilter.empty();
        auto relTableIDs = catalog->getRelTableIDs(tx);
        auto storage = context->getStorageManager();
        for (auto& tableID : relTableIDs) {
            if (!hasRelFilter || relFilter.count(tableID)) {
                auto relTableInfo = std::make_unique<RelTableInfo>(tx, storage, catalog, tableID);
                reltables.emplace_back(tableID, std::move(relTableInfo));
            }
        }
        threadBitSets =
            std::make_unique<std::shared_ptr<InternalIDValueBitSet>[]>(bindData->numThreads);
    }

    std::shared_ptr<InternalIDValueBitSet> getThreadBitSet(uint32_t threadId) {
        auto threadBitSet = threadBitSets[threadId];
        if (threadBitSet == nullptr) {
//            threadMemTime.tik();
            auto localBitSet = std::make_shared<InternalIDValueBitSet>(context->getCatalog(),
                context->getStorageManager(), context->getTx());
            threadBitSets[threadId] = localBitSet;
//            threadMemTime.tok();
            return localBitSet;
        } else {
            return threadBitSet;
        }
    }
    int64_t numThreads;
    bool hasNodeFilter;
    main::ClientContext* context;
    transaction::Transaction* tx;

    //    点个数的value
    std::unordered_set<table_id_t> nodeFilter;
    std::unique_ptr<std::shared_ptr<InternalIDValueBitSet>[]> threadBitSets;
    std::vector<std::pair<common::table_id_t, std::unique_ptr<RelTableInfo>>> reltables;

    //
//    TikTok funTime;
//    TikTok unionTime;
//    TikTok combineTime;
//    TikTok threadMemTime;
//    uint64_t s1 = 0, s2 = 0;
};

class ScanState {
public:
    explicit ScanState(SsspSharedData& sharedData, nodeID_t nodeID, std::string direction)
        : sharedData(sharedData), direction(direction) {

        nodeNumbers = 1;

        std::vector<nodeID_t> nextMission;
        nextMission.emplace_back(nodeID);
        nextFrontier.emplace_back(std::move(nextMission));

        globalBitSet = std::make_shared<InternalIDValueBitSet>(sharedData.context->getCatalog(),
            sharedData.context->getStorageManager(), sharedData.context->getTx());
        globalBitSet->markVisited(nodeID, 1);
    }

    void scanTask(storage::RelTableScanState& readState, common::ValueVector& srcVector,
        common::ValueVector& dstVector, RelTableInfo* info,
        std::shared_ptr<InternalIDValueBitSet> threadBitSet, RelDataDirection relDataDirection,
        uint32_t tid) {
        auto prevTableID =
            relDataDirection == RelDataDirection::FWD ? info->srcTableID : info->dstTableID;
        auto nextTableID =
            relDataDirection == RelDataDirection::BWD ? info->srcTableID : info->dstTableID;
        if (sharedData.hasNodeFilter && !sharedData.nodeFilter.count(nextTableID)) {
            return;
        }
        auto relTable = info->relTable;
        readState.direction = relDataDirection;
        readState.dataScanState->resetState();

        auto dstState = dstVector.state;
        for (auto currentNodeID : nextFrontier[tid]) {
            if (prevTableID != currentNodeID.tableID) {
                break;
            }
            srcVector.setValue<nodeID_t>(0, currentNodeID);
            relTable->initializeScanState(sharedData.tx, readState);
            // todo 先判断有没有再获取count?
            auto count = globalBitSet->getNodeValueCount(currentNodeID);
            while (readState.hasMoreToRead(sharedData.tx)) {
                relTable->scan(sharedData.tx, readState);
                KU_ASSERT(dstState->getSelVector().isUnfiltered());
                for (auto i = 0u; i < dstState->getSelVector().getSelSize(); ++i) {
                    auto nbrID = dstVector.getValue<nodeID_t>(i);

                    if (globalBitSet->isVisited(nbrID)) {
                        continue;
                    }
                    threadBitSet->markVisited(nbrID, count);
                }
            }
        }
    }

    void funcTask(uint32_t threadId) {

        auto readState = storage::RelTableScanState({}, RelDataDirection::FWD);
        auto srcState = DataChunkState::getSingleValueDataChunkState();
        auto dstState = std::make_shared<common::DataChunkState>();

        auto mm = sharedData.context->getMemoryManager();
        auto srcVector = common::ValueVector(LogicalType::INTERNAL_ID(), mm);
        auto dstVector = common::ValueVector(LogicalType::INTERNAL_ID(), mm);

        srcVector.state = srcState;
        dstVector.state = dstState;
        readState.nodeIDVector = &srcVector;
        readState.outputVectors.clear();
        readState.outputVectors.emplace_back(&dstVector);
        auto threadBitSet = sharedData.getThreadBitSet(threadId);
        auto size = nextFrontier.size();
        while (true) {
            uint32_t tid = taskID.fetch_add(1, std::memory_order_relaxed);
            if (tid >= size) {
                break;
            }
            for (auto& [tableID, info] : sharedData.reltables) {
                if (direction == "out" || direction == "both") {
                    scanTask(readState, srcVector, dstVector, info.get(), threadBitSet,
                        RelDataDirection::FWD, tid);
                }
                if (direction == "in" || direction == "both") {
                    scanTask(readState, srcVector, dstVector, info.get(), threadBitSet,
                        RelDataDirection::BWD, tid);
                }
            }
        }
    }

    UnionResult unionTask(uint32_t tid, const ScanState& otherScanState) {
        uint64_t nodeCount = 0;
        uint64_t ans = 0;
        std::vector<std::vector<nodeID_t>> tempFrontier;
        auto numThreads = sharedData.numThreads;
//        TikTok s1, s2;
        auto& threadBitSets = sharedData.threadBitSets;
        for (auto tableID = 0u; tableID < globalBitSet->getTableNum(); ++tableID) {
            uint32_t tableSize = globalBitSet->getTableSize(tableID);
            if (!tableSize) {
                continue;
            }
            uint32_t l = tableSize * tid / numThreads, r = tableSize * (tid + 1) / numThreads;
            std::vector<Value> tempMark;
            tempMark.reserve(r - l);
            tempMark.resize(r - l);
//            s1.tik();
            for (auto i = 0u; i < numThreads; ++i) {
                auto threadBitSet = threadBitSets[i];
                for (auto offset = l; offset < r; ++offset) {
                    auto& value = threadBitSet->getNodeValue(tableID, offset);
                    if (value.mark) {
                        tempMark[offset - l].merge(value);
                        value.reset();
                    }
                }
            }
//            s1.tok();
//            s2.tik();
            std::vector<nodeID_t> nextMission;
            for (auto i = l; i < r; ++i) {
                auto& mark = tempMark[i - l];
                uint64_t now = mark.mark;
                if (!now) {
                    continue;
                }
                globalBitSet->getNodeValue(tableID, i).merge(mark);
                uint64_t pos;
                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = InternalIDBitSet::getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    auto otherSideCount = otherScanState.globalBitSet->getNodeValueCount(nowNode);
                    if (otherSideCount) {
                        ans += 1LL * globalBitSet->getNodeValueCount(nowNode) * otherSideCount;
                    }

                    if (!ans) {
                        if ((!nextMission.empty() &&
                                ((nextMission.back().offset ^ nowNode.offset) >>
                                    StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                            nodeCount += nextMission.size();
                            tempFrontier.emplace_back(std::move(nextMission));
                        }
                        nextMission.emplace_back(nowNode);
                    }

                    // 还原成 now & (now - 1) ,和result配合,统计now中1的个数 now=now & (now - 1)
                    now ^= pos;
                }
            }
            if (!ans && !nextMission.empty()) {
                nodeCount += nextMission.size();
                tempFrontier.emplace_back(std::move(nextMission));
            }
//            s2.tok();
        }
        return {ans, nodeCount, tempFrontier};
    }

    bool isFrontierEmpty() const { return nextFrontier.empty(); }

    uint64_t getNextFrontier(ScanState& other) {
        nodeNumbers = taskID = 0;
//        sharedData.funTime.tik();
        std::vector<std::thread> threads;
        for (auto i = 0u; i < sharedData.numThreads; ++i) {
            threads.emplace_back([&, i] { funcTask(i); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
//        sharedData.funTime.tok();

        std::vector<std::future<UnionResult>> unionFuture;
        for (auto i = 0u; i < sharedData.numThreads; i++) {
            unionFuture.emplace_back(
                std::async(std::launch::async, &ScanState::unionTask, this, i, std::ref(other)));
        }
        nextFrontier.clear();
        uint64_t pathCount = 0;
        for (auto& future : unionFuture) {
//            sharedData.unionTime.tik();
            auto [ans, count, tempFrontier] = future.get();
//            sharedData.unionTime.tok();
            pathCount += ans;
            if (pathCount == 0) {
//                sharedData.combineTime.tik();
                nextFrontier.insert(nextFrontier.end(),
                    std::make_move_iterator(tempFrontier.begin()),
                    std::make_move_iterator(tempFrontier.end()));
                nodeNumbers += count;
//                sharedData.combineTime.tok();
            }

//            sharedData.s1 += s1;
//            sharedData.s2 += s2;
        }
        return pathCount;
    }

    SsspSharedData& sharedData;
    std::string direction;
    std::atomic_uint64_t taskID;
    int64_t nodeNumbers;
    std::vector<std::vector<nodeID_t>> nextFrontier; //  block划分的内部vector

    // 记录点的count数
    std::shared_ptr<InternalIDValueBitSet> globalBitSet;
};

static common::offset_t tableFunc(TableFuncInput& input, TableFuncOutput& output) {
    auto sharedState = input.sharedState->ptrCast<CallFuncSharedState>();
    if (!sharedState->getMorsel().hasMoreToOutput()) {
        return 0;
    }
    auto& dataChunk = output.dataChunk;
    auto pos = dataChunk.state->getSelVector()[0];
    auto bindData = input.bindData->constPtrCast<SsspBindData>();
    auto numThreads = bindData->numThreads;
    auto direction = bindData->direction;
    auto maxHop = bindData->maxHop;
    auto mode = bindData->mode;
    SsspSharedData sharedData(bindData, numThreads);
    //    sharedData.threadMarks.resize(numThreads);
    auto srcNodeID = getNodeID(sharedData.context, bindData->srcTableName, bindData->srcPrimaryKey);
    auto dstNodeID = getNodeID(sharedData.context, bindData->dstTableName, bindData->dstPrimaryKey);
    auto srcDirection = direction;
    auto dstDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
    ScanState srcScanState(sharedData, srcNodeID, srcDirection);
    ScanState dstScanState(sharedData, dstNodeID, dstDirection);
    int64_t lengthResult = -1, numberResult = 0;
    if (srcNodeID == dstNodeID) {
        lengthResult = 0;
        numberResult = 1;
    } else {
        int64_t hop = 0;
        while (hop < maxHop) {
            auto srcIsEmpty = srcScanState.isFrontierEmpty();
            auto dstIsEmpty = dstScanState.isFrontierEmpty();
            if (srcIsEmpty && dstIsEmpty) {
                break;
            }
            hop++;
            if (!srcIsEmpty && !dstIsEmpty) {
                if (srcScanState.nodeNumbers <= dstScanState.nodeNumbers) {
                    numberResult = srcScanState.getNextFrontier(dstScanState);
                } else {
                    numberResult = dstScanState.getNextFrontier(srcScanState);
                }
            } else if (!srcIsEmpty) {
                numberResult = srcScanState.getNextFrontier(dstScanState);
            } else {
                numberResult = dstScanState.getNextFrontier(srcScanState);
            }

            if (numberResult) {
                lengthResult = hop;
                break;
            }
        }
    }

//    std::cout << sharedData.funTime.getElapsedTimeInMS() << "[fun],"
//              << sharedData.unionTime.getElapsedTimeInMS() << "[union],"
//              << sharedData.combineTime.getElapsedTimeInMS() << "[combine],"
//              << sharedData.threadMemTime.getElapsedTimeInMS() << "[mem]," << sharedData.s1
//              << "[s1]," << sharedData.s2 << "[s2]," << std::endl;

    if (mode == "length") {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, lengthResult);
    } else if (mode == "count") {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, numberResult);
    } else {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, lengthResult);
        dataChunk.getValueVector(1)->setValue<int64_t>(pos, numberResult);
    }
    return 1;
}

static std::unique_ptr<TableFuncBindData> bindFunc(main::ClientContext* context,
    TableFuncBindInput* input) {
    std::vector<std::string> returnColumnNames;
    std::vector<LogicalType> returnTypes;
    auto mode = input->inputs[6].getValue<std::string>();
    if (mode == "length") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(LogicalType::INT64());
    } else if (mode == "count") {
        returnColumnNames.emplace_back("count");
        returnTypes.emplace_back(LogicalType::INT64());
    } else if (mode == "length_count") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(LogicalType::INT64());
        returnColumnNames.emplace_back("count");
        returnTypes.emplace_back(LogicalType::INT64());
    } else {
        throw BinderException(
            "unknown mode, following modes are supported: length, count, length_count");
    }
    auto direction = input->inputs[4].getValue<std::string>();
    if (direction != "in" && direction != "out" && direction != "both") {
        throw BinderException(
            "unknown direction, following directions are supported: in, out, both");
    }
    auto maxHop = input->inputs[5].getValue<int64_t>();
    KU_ASSERT(maxHop >= -1);
    maxHop = (maxHop == -1) ? INT64_MAX : maxHop;
    auto numThreads = input->inputs[7].getValue<int64_t>();
    KU_ASSERT(numThreads >= 0);
    // filter
    auto nodeList = input->inputs[8];
    auto relList = input->inputs[9];
    std::unordered_set<std::string> nodeFilter, relFilter;
    for (auto pos = 0u; pos < NestedVal::getChildrenSize(&nodeList); ++pos) {
        auto node = NestedVal::getChildVal(&nodeList, pos);
        if (node->getDataType().getLogicalTypeID() != common::LogicalTypeID::STRING) {
            throw BinderException("wrong node name type");
        }
        /*we can check whether the nodetable is exist here*/
        nodeFilter.insert(node->getValue<std::string>());
    }
    for (auto pos = 0u; pos < NestedVal::getChildrenSize(&relList); ++pos) {
        auto rel = NestedVal::getChildVal(&relList, pos);
        if (rel->getDataType().getLogicalTypeID() != common::LogicalTypeID::STRING) {
            throw BinderException("wrong rel name type");
        }
        relFilter.insert(rel->getValue<std::string>());
    }
    auto bindData = std::make_unique<SsspBindData>(context,
        input->inputs[0].getValue<std::string>(), input->inputs[1].getValue<std::string>(),
        input->inputs[2].getValue<std::string>(), input->inputs[3].getValue<std::string>(),
        direction, maxHop, mode, numThreads, nodeFilter, relFilter, std::move(returnTypes),
        std::move(returnColumnNames), 1 /* one line of results */);
    return bindData;
}

function_set GraphBspSsspFunction::getFunctionSet() {
    function_set functionSet;
    functionSet.push_back(std::make_unique<TableFunction>(name, tableFunc, bindFunc,
        initSharedState, initEmptyLocalState,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::STRING, LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::INT64, LogicalTypeID::STRING, LogicalTypeID::INT64, LogicalTypeID::LIST,
            LogicalTypeID::LIST}));
    return functionSet;
}

} // namespace function
} // namespace kuzu