#pragma once

#include "cache.hpp"
#include "lru_cache_policy.hpp"
#include "phmap.h"
#include "storage/store/csr_chunked_node_group.h"

namespace kuzu {
namespace common {

struct ObjectCache {
private:
    template<typename Key, typename Value>
    using lru_cache_t = typename caches::fixed_sized_cache<Key, Value, caches::LRUCachePolicy,
        phmap::node_hash_map<Key, Value>>;

public:
    explicit ObjectCache(int32_t size) {
        if (size > 0) {
            cache =
                std::make_unique<lru_cache_t<void*, std::shared_ptr<storage::ChunkedCSRHeader>>>(
                    size);
        } else {
            cache = nullptr;
        }
    }

    std::shared_ptr<storage::ChunkedCSRHeader> get(void* csrNodeGroup) const {
        return cache->TryGet(csrNodeGroup);
    }

    void put(void* csrNodeGroup, std::shared_ptr<storage::ChunkedCSRHeader> header) {
        cache->Put(csrNodeGroup, header);
    }

    bool enable() const { return cache != nullptr; }

private:
    std::unique_ptr<lru_cache_t<void*, std::shared_ptr<storage::ChunkedCSRHeader>>> cache;
};
} // namespace main
} // namespace kuzu
