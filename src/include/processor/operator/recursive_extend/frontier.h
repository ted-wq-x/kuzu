#pragma once

#include "common/types/internal_id_util.h"
#include "function/table/basic.h"

namespace kuzu {
namespace processor {

using node_rel_id_t = std::pair<common::nodeID_t, common::relID_t>;

/*
 * A Frontier can stores dst node offsets, its multiplicity and its bwd edges. Note that we don't
 * need to track all information in BFS computation.
 *
 * Computation                   |  Information tracked
 * Shortest path track path      |  nodeIDs & bwdEdges
 * Shortest path NOT track path  |  nodeIDs
 * Var length track path         |  nodeIDs & bwdEdges
 * Var length NOT track path     |  nodeIDs & nodeIDToMultiplicity
 */
class Frontier {
public:
    Frontier() {}
    virtual ~Frontier() = default;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;

        /// the type of the values when the iterator is dereferenced
        using value_type = common::nodeID_t;
        /// a type to represent differences between iterators
        using difference_type = std::ptrdiff_t;
        /// defines a pointer to the type iterated over (value_type)
        using pointer = common::nodeID_t*;
        /// defines a reference to the type iterated over (value_type)
        using reference = common::nodeID_t&;

        virtual value_type operator*() const = 0;
        virtual iterator& operator++() = 0;
        virtual bool operator==(const iterator& other) const = 0;
        void operator++(int) { ++*this; }

    public:
        virtual ~iterator() = default;
    };

public:
    virtual uint64_t getMultiplicity(common::nodeID_t nodeID) const = 0;
    virtual bool isEmpty() const = 0;
    virtual void resetState() = 0;
    virtual void addSrcNode(common::nodeID_t /*nodeID*/) { fillingNodeIDIter(); }
    virtual void fillingNodeIDIter() = 0;

    std::unique_ptr<iterator> beginIter;
    std::unique_ptr<iterator> endIter;

protected:
    class map_iterator : public iterator {
    public:
        explicit map_iterator(std::vector<internalID_t>::iterator iter) : iter{iter} {}
        value_type operator*() const override { return *iter; }
        iterator& operator++() override {
            iter++;
            return *this;
        }
        bool operator==(const iterator& other) const override {
            return iter == dynamic_cast<const map_iterator*>(&other)->iter;
        }

    private:
        std::vector<internalID_t>::iterator iter;
    };
};

class TrackPathFrontier : public Frontier {
public:
    common::node_id_map_t<std::vector<node_rel_id_t>> bwdEdges;

    void addEdge(common::nodeID_t boundNodeID, common::nodeID_t nbrNodeID, common::nodeID_t relID) {
        bwdEdges[nbrNodeID].emplace_back(boundNodeID, relID);
    }

    uint64_t getMultiplicity(common::nodeID_t /*nodeID*/) const override { return 1; }

    void addSrcNode(common::nodeID_t nodeID) override {
        nodeIDs.push_back(nodeID);
        Frontier::addSrcNode(nodeID);
    }

    void resetState() override {
        bwdEdges.clear();
        nodeIDs.clear();
        beginIter = nullptr;
        endIter = nullptr;
    }

    bool isEmpty() const override { return nodeIDs.empty(); }

public:
    void fillingNodeIDIter() override {
        // 第一次为false,上一条填充完下一跳的,第二次为true,下一跳便利完,准备给后续组装结果时用的,可以避免重新生成nodeIDs
        if (nodeIDs.empty()) {
            nodeIDs.clear();
            nodeIDs.reserve(bwdEdges.size());
            for (const auto& item : bwdEdges) {
                nodeIDs.emplace_back(item.first);
            }
            std::sort(nodeIDs.begin(), nodeIDs.end());
        }
        beginIter = std::make_unique<map_iterator>(nodeIDs.begin());
        endIter = std::make_unique<map_iterator>(nodeIDs.end());
    }

private:
    std::vector<internalID_t> nodeIDs;
};

class UnTrackPathFrontier : public Frontier {

public:
    UnTrackPathFrontier(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx)
        : Frontier(), empty{true},
          nodeIDToMultiplicity{
              std::make_unique<function::InternalIDCountBitSet>(catalog, storage, tx)} {}

    void addNodeWithMultiplicity(common::nodeID_t nodeID, uint64_t multiplicity) {
        nodeIDToMultiplicity->markVisited(nodeID, multiplicity);
        empty = false;
    }

    uint64_t getMultiplicity(common::nodeID_t nodeID) const override {
        return nodeIDToMultiplicity->getNodeValueCount(nodeID);
    }

    uint64_t decrementMultiplicity(common::nodeID_t nodeID, uint64_t decrementValue) {
        return nodeIDToMultiplicity->decrement(nodeID, decrementValue);
    }

    void resetState() override {
        nodeIDToMultiplicity->clear();
        empty = true;
        beginIter = nullptr;
        endIter = nullptr;
    }

    void addSrcNode(common::nodeID_t nodeID) override {
        addNodeWithMultiplicity(nodeID, 1);
        Frontier::addSrcNode(nodeID);
    }

    bool isEmpty() const override { return empty; }

private:
    class bitset_iterator : public iterator {
    public:
        explicit bitset_iterator(function::InternalIDCountBitSet::Iterator iter)
            : iter{std::move(iter)} {}
        value_type operator*() const override { return *iter; }
        iterator& operator++() override {
            iter++;
            return *this;
        }
        bool operator==(const iterator& other) const override {
            return iter == dynamic_cast<const bitset_iterator*>(&other)->iter;
        }

    private:
        function::InternalIDCountBitSet::Iterator iter;
    };

public:
    void fillingNodeIDIter() override {
        beginIter = std::make_unique<bitset_iterator>(nodeIDToMultiplicity->begin());
        endIter = std::make_unique<bitset_iterator>(nodeIDToMultiplicity->end());
    }

private:
    bool empty;
    std::unique_ptr<function::InternalIDCountBitSet> nodeIDToMultiplicity;
};

// We assume number of edges per table is smaller than 2^63. So we mask the highest bit of rel
// offset to indicate if the src and dst node this relationship need to be flipped.
struct RelIDMasker {
    static constexpr uint64_t FLIP_SRC_DST_MASK = 0x8000000000000000;
    static constexpr uint64_t CLEAR_FLIP_SRC_DST_MASK = 0x7FFFFFFFFFFFFFFF;

    static void markFlip(common::internalID_t& relID) { relID.offset |= FLIP_SRC_DST_MASK; }
    static bool needFlip(common::internalID_t& relID) { return relID.offset & FLIP_SRC_DST_MASK; }
    static void clearMark(common::internalID_t& relID) { relID.offset &= CLEAR_FLIP_SRC_DST_MASK; }
    static common::internalID_t getWithoutMark(const common::internalID_t& relID) {
        return common::internalID_t(relID.offset & CLEAR_FLIP_SRC_DST_MASK, relID.tableID);
    }
};

} // namespace processor
} // namespace kuzu
