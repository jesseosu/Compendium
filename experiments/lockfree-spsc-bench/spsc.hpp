// Header-only SPSC ring buffer.
//
// Single producer, single consumer. Bounded. Capacity must be a power of two.
//
// The two design points worth seeing here:
//   1. `head_` and `tail_` are each on their own cache line via alignas(64).
//   2. The producer caches `tail_` locally and the consumer caches `head_`,
//      so we only pay a coherence miss when the cached value would block.
//
// Background: ../../systems/lockfree-spsc-queues.md
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity too small");

    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kCacheLine = 64;

public:
    SPSCQueue() = default;
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Producer side.
    bool try_push(const T& v) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t next = h + 1;
        if (next - cached_tail_ > Capacity) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (next - cached_tail_ > Capacity) return false;
        }
        buf_[h & kMask] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side.
    bool try_pop(T& out) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (t == cached_head_) return false;
        }
        out = buf_[t & kMask];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    // Producer's cached view of the consumer's tail. Lives on the producer line
    // so we don't pay a cross-core read every push.
    alignas(kCacheLine) std::size_t cached_tail_{0};

    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLine) std::size_t cached_head_{0};

    alignas(kCacheLine) T buf_[Capacity]{};
};
