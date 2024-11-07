#pragma once

#include <vector>

#include "common/read_write_lock.h"
#include "common/serializer/deserializer.h"
#include "common/serializer/serializer.h"
#include "common/types/types.h"

namespace kuzu {
namespace storage {
class MemoryManager;

template<class T>
class GroupCollection {
public:
    GroupCollection() {}

    common::ReadLock readLock() const { return common::ReadLock{mtx}; }
    common::WriteLock writeLock() const { return common::WriteLock{mtx}; }

    void deserializeGroups(MemoryManager& memoryManager, common::Deserializer& deSer) {
        writeLock();
        deSer.deserializeVectorOfPtrs<T>(groups,
            [&](common::Deserializer& deser) { return T::deserialize(memoryManager, deser); });
    }
    void serializeGroups(common::Serializer& ser) {
        readLock();
        ser.serializeVectorOfPtrs<T>(groups);
    }

    void appendGroup(const common::WriteLock& lock, std::unique_ptr<T> group) {
        KU_ASSERT(group);
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        groups.push_back(std::move(group));
    }
    T* getGroup(const common::ReadWriteLock& lock, common::idx_t groupIdx) const {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        KU_ASSERT(groupIdx < groups.size());
        return groups[groupIdx].get();
    }
    T* getGroupNoLock(common::idx_t groupIdx) const {
        KU_ASSERT(groupIdx < groups.size());
        return groups[groupIdx].get();
    }
    void replaceGroup(const common::WriteLock& lock, common::idx_t groupIdx,
        std::unique_ptr<T> group) {
        KU_ASSERT(group);
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        if (groupIdx >= groups.size()) {
            groups.resize(groupIdx + 1);
        }
        groups[groupIdx] = std::move(group);
    }

    void resize(const common::WriteLock& lock, common::idx_t newSize) {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        if (newSize <= groups.size()) {
            return;
        }
        groups.resize(newSize);
    }

    bool isEmpty(const common::ReadWriteLock& lock) const {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        return groups.empty();
    }
    common::idx_t getNumGroups(const common::ReadWriteLock& lock) const {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        return groups.size();
    }
    common::idx_t getNumGroupsNoLock() const { return groups.size(); }

    const std::vector<std::unique_ptr<T>>& getAllGroups(const common::ReadWriteLock& lock) const {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        return groups;
    }
    T* getFirstGroup(const common::ReadWriteLock& lock) const {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        if (groups.empty()) {
            return nullptr;
        }
        return groups.front().get();
    }
    T* getFirstGroupNoLock() const {
        if (groups.empty()) {
            return nullptr;
        }
        return groups.front().get();
    }
    T* getLastGroup(const common::ReadWriteLock& lock) const {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        if (groups.empty()) {
            return nullptr;
        }
        return groups.back().get();
    }

    void clear(const common::WriteLock& lock) {
        KU_ASSERT(lock.isLocked());
        KU_UNUSED(lock);
        groups.clear();
    }

private:
    mutable std::shared_mutex mtx;
    std::vector<std::unique_ptr<T>> groups;
};

} // namespace storage
} // namespace kuzu
