#pragma once

#include "catalog/catalog_entry/node_table_catalog_entry.h"
#include "graph.h"
#include "storage/store/node_table.h"
#include "storage/store/rel_table.h"

namespace kuzu {
namespace graph {

struct NbrScanState {
    std::shared_ptr<common::DataChunkState> srcNodeIDVectorState;
    std::shared_ptr<common::DataChunkState> dstNodeIDVectorState;
    std::unique_ptr<common::ValueVector> srcNodeIDVector;
    std::unique_ptr<common::ValueVector> dstNodeIDVector;

    static constexpr common::RelDataDirection direction = common::RelDataDirection::FWD;
    std::vector<common::column_id_t> columnIDs;
    std::unique_ptr<storage::RelTableReadState> fwdReadState;

    explicit NbrScanState(storage::MemoryManager* mm);
};

class OnDiskGraph : public Graph {
public:
    OnDiskGraph(main::ClientContext* context, const std::string& nodeName,
        const std::string& relName);

    common::table_id_t getNodeTableID() override { return nodeTable->getTableID(); }

    common::offset_t getNumNodes() override;

    common::offset_t getNumEdges() override;

    uint64_t scanNbrFwd(common::offset_t offset) override;

    std::vector<common::nodeID_t> getNbrs() override { return nbrs; }

    common::nodeID_t getNbr(common::idx_t idx) override;

private:
    main::ClientContext* context;
    storage::NodeTable* nodeTable;
    storage::RelTable* relTable;
    //
    std::vector<common::nodeID_t> nbrs;
    std::unique_ptr<NbrScanState> nbrScanState;
};

} // namespace graph
} // namespace kuzu
