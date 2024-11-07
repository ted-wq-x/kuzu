#pragma once

#include <mutex>
#include <shared_mutex>

namespace kuzu {
namespace common {

struct ReadWriteLock {
    virtual ~ReadWriteLock() = default;

    virtual bool isLocked() const = 0;

    virtual void release() = 0;
};
struct ReadLock : public ReadWriteLock {
    ReadLock() {}
    explicit ReadLock(std::shared_mutex& mtx) : lck{mtx} {}

    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;

    ReadLock(ReadLock&& other) noexcept { std::swap(lck, other.lck); }
    ReadLock& operator=(ReadLock&& other) noexcept {
        std::swap(lck, other.lck);
        return *this;
    }
    bool isLocked() const override { return lck.owns_lock(); }

    void release() override {
        KU_ASSERT(isLocked());
        lck.unlock();
    }

private:
    std::shared_lock<std::shared_mutex> lck;
};
struct WriteLock : public ReadWriteLock {
    WriteLock() {}
    explicit WriteLock(std::shared_mutex& mtx) : lck{mtx} {}

    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;

    WriteLock(WriteLock&& other) noexcept { std::swap(lck, other.lck); }
    WriteLock& operator=(WriteLock&& other) noexcept {
        std::swap(lck, other.lck);
        return *this;
    }
    bool isLocked() const override { return lck.owns_lock(); }

    void release() override {
        KU_ASSERT(isLocked());
        lck.unlock();
    }

private:
    std::unique_lock<std::shared_mutex> lck;
};

} // namespace common
} // namespace kuzu
