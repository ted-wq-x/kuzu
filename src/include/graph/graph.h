#pragma once

#include "common/types/types.h"
#include "common/vector/value_vector.h"

namespace kuzu {

namespace graph {

/*
 * Graph is implemented with the following assumptions
 * - single node and edge table.
 * - topology only, i.e. does not track property.
 * */
class Graph {
public:
    Graph() = default;
    virtual ~Graph() = default;

    // TODO: we may not need the following interface.
    virtual common::table_id_t getNodeTableID() = 0;

    // Get total number of nodes in the graph.
    virtual common::offset_t getNumNodes() = 0;
    // Get total number of edges in the graph.
    virtual common::offset_t getNumEdges() = 0;

    // Scan neighbors using forward index.
    // Return total number of neighbors.
    virtual uint64_t scanNbrFwd(common::offset_t offset) = 0;

    virtual std::vector<common::nodeID_t> getNbrs() = 0;
    //
    virtual common::nodeID_t getNbr(common::idx_t idx) = 0;
};

} // namespace graph
} // namespace kuzu

// 1. Parall