#pragma once

#include "bfs_state.h"

namespace kuzu {
namespace processor {

template<bool TRACK_PATH>
class ShortestPathState : public BaseBFSState {
public:
    ShortestPathState(uint8_t upperBound, TargetDstNodes* targetDstNodes,
        main::ClientContext* clientContext)
        : BaseBFSState{upperBound, targetDstNodes, clientContext}, numVisitedDstNodes{0} {}
    ~ShortestPathState() override = default;

    inline bool isComplete() final {
        return isCurrentFrontierEmpty() || isUpperBoundReached() || isAllDstReached();
    }
    inline void resetState() final {
        BaseBFSState::resetState();
        numVisitedDstNodes = 0;
        visited.clear();
    }

    inline void markSrc(common::nodeID_t nodeID) final {
        visited.insert(nodeID);
        if (targetDstNodes->contains(nodeID)) {
            numVisitedDstNodes++;
        }
        currentFrontier->addSrcNode(nodeID);
    }

    inline void markVisited(common::nodeID_t boundNodeID, common::nodeID_t nbrNodeID,
        common::nodeID_t relID, uint64_t /*multiplicity*/) final {
        if (visited.contains(nbrNodeID)) {
            return;
        }
        visited.insert(nbrNodeID);
        if (targetDstNodes->contains(nbrNodeID)) {
            numVisitedDstNodes++;
        }
        if constexpr (TRACK_PATH) {
            static_cast<TrackPathFrontier*>(nextFrontier)->addEdge(boundNodeID, nbrNodeID, relID);
        } else {
            static_cast<UnTrackPathFrontier*>(nextFrontier)->addNodeWithMultiplicity(nbrNodeID, 1);
        }
    }

    inline std::unique_ptr<Frontier> createFrontier() override {
        if constexpr (TRACK_PATH) {
            return std::make_unique<TrackPathFrontier>();
        } else {
            return std::make_unique<UnTrackPathFrontier>(clientContext->getCatalog(),
                clientContext->getStorageManager(), clientContext->getTx());
        }
    }

private:
    inline bool isAllDstReached() const {
        return numVisitedDstNodes == targetDstNodes->getNumNodes();
    }

private:
    uint64_t numVisitedDstNodes;
    common::node_id_set_t visited;
};

} // namespace processor
} // namespace kuzu
