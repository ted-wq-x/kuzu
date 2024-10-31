#pragma once

#include "bfs_state.h"

namespace kuzu {
namespace processor {

template<bool TRACK_PATH>
struct VariableLengthState : public BaseBFSState {
    VariableLengthState(uint8_t upperBound, TargetDstNodes* targetDstNodes,
        main::ClientContext* clientContext)
        : BaseBFSState{upperBound, targetDstNodes, clientContext} {}
    ~VariableLengthState() override = default;

    inline void resetState() final { BaseBFSState::resetState(); }
    inline bool isComplete() final { return isCurrentFrontierEmpty() || isUpperBoundReached(); }

    inline void markSrc(common::nodeID_t nodeID) final { currentFrontier->addSrcNode(nodeID); }

    inline void markVisited(common::nodeID_t boundNodeID, common::nodeID_t nbrNodeID,
        common::relID_t relID, uint64_t multiplicity) final {
        if constexpr (TRACK_PATH) {
            static_cast<TrackPathFrontier*>(nextFrontier)->addEdge(boundNodeID, nbrNodeID, relID);
        } else {
            static_cast<UnTrackPathFrontier*>(nextFrontier)
                ->addNodeWithMultiplicity(nbrNodeID, multiplicity);
        }
    }

    inline std::unique_ptr<Frontier> createFrontier() override {
        if constexpr (TRACK_PATH) {
            return std::make_unique<TrackPathFrontier>();
        } else {
            //            if (clientContext->getTransactionContext()->isAutoTransaction()) {
            //                // 手动事物的单次插入点,由于其offset太大,故使用map存储
            //                return
            //                std::make_unique<UnTrackPath1Frontier>(clientContext->getCatalog(),
            //                    clientContext->getStorageManager(), clientContext->getTx());
            //            } else {
            //                return std::make_unique<UnTrackPath2Frontier>();
            //            }
            return std::make_unique<UnTrackPath1Frontier>(clientContext->getCatalog(),
                clientContext->getStorageManager(), clientContext->getTx());
        }
    }
};

} // namespace processor
} // namespace kuzu
