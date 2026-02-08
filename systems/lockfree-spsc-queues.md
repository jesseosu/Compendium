# Lock-Free SPSC Queues

A single-producer, single-consumer queue is the simplest useful lock-free data structure, and the one I implemented in [Celeritas](../project-retrospectives/celeritas.md) for the order-ingest, exec-publish, and md-publish edges of the matching pipeline. The constraint "exactly one thread writes, exactly one thread reads" is strong enough to let you avoid CAS entirely — plain loads and stores with the right memory ordering are sufficient.

## The basic idea

Ring buffer of fixed capacity `N` (power of two so index math is a mask). Two indices: `head` written only by the producer, `tail` written only by the consumer. Each side reads the other's index to check fullness/emptiness.

```
producer:                          consumer:
if (head - tail == N) full         if (head == tail) empty
buf[head & (N-1)] = item           item = buf[tail & (N-1)]
head++                             tail++
```

No locks, no CAS. Correctness comes entirely from the memory ordering on those index updates.

## Why the memory ordering matters

On x86 the hardware memory model is strong (TSO — stores aren't reordered with other stores, loads aren't reordered with other loads). You can almost get away with no explicit ordering. *But the compiler will happily reorder your code* unless you tell it not to. And on ARM/POWER the hardware itself reorders aggressively, so the explicit ordering is doing real work.

The minimum correct ordering:

- **Producer stores `head`** with `memory_order_release`. Ensures the prior write to `buf[i]` is visible before the consumer observes the new `head`.
- **Consumer loads `head`** with `memory_order_acquire`. Pairs with the release: once the consumer sees the new `head`, it sees the payload too.
- **Producer loads `tail`** with `memory_order_acquire`.
- **Consumer stores `tail`** with `memory_order_release`.

Release/acquire is cheaper than seq_cst. On x86 they compile to plain MOVs in this case; on ARM, seq_cst forces a full DMB barrier while release/acquire compile to the cheaper LDAR/STLR. → [`./cpu-cache-coherence.md`](./cpu-cache-coherence.md) covers the underlying coherence model.

## False sharing is the real killer

The naive layout puts `head` and `tail` on the same cache line. Every producer write to `head` invalidates the consumer's cached copy of `tail`, and vice versa — even though they're logically independent. Cache-line ping-pong; throughput collapses.

```cpp
struct alignas(64) SPSCQueue {
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) std::array<T, N> buf_;
};
```

The `alignas(64)` on each field forces it onto its own 64-byte cache line (typical line size on x86_64 and most ARM). On Celeritas, padding the indices was the change that actually mattered for sustained throughput. The runnable benchmark in [`../experiments/lockfree-spsc-bench/`](../experiments/lockfree-spsc-bench/) lets you measure the effect on your hardware — I haven't published a number because the magnitude depends heavily on the CPU and queue depth.

## Cached indices — the second optimization

The producer reads `tail` to check fullness. But `tail` is on a cache line owned by the consumer, so every read is potentially a coherence miss. If the queue is usually not-full, most of those reads are wasted.

Optimization: keep a *local cached copy* of the opposite index, and only refresh it when the cached value would indicate full/empty.

```cpp
// producer side
if (head_ - cached_tail_ == N) {
    cached_tail_ = tail_.load(std::memory_order_acquire);
    if (head_ - cached_tail_ == N) return false;  // really full
}
```

The cached copy lives on the producer's cache line; the cross-core read only happens when it looks like we're about to block. Cuts coherence traffic dramatically under light load.

## When SPSC isn't enough

SPSC is fine for a thread-per-role design (ingress thread → matching thread → egress thread). Add multiple producers or consumers and the design space explodes:

- **Michael-Scott queues** — CAS-based, suffer from ABA, generally unbounded and node-based.
- **Vyukov's bounded MPMC** — per-slot sequence numbers; widely used in production (Folly, crossbeam-rs).
- **LCRQ** — CAS2-based, almost-wait-free. Hard to implement portably.

For a matching engine like Celeritas the SPSC-per-edge pattern is preferable anyway — pin threads to cores, keep roles separate, and never need MPMC. → [`../papers/lmax-disruptor.md`](../papers/lmax-disruptor.md) is the canonical writeup of this pattern in production.

## Where I've used this

Three SPSC queues in [Celeritas](../project-retrospectives/celeritas.md), one between each pair of pipeline stages.

## References

- Vyukov's bounded SPSC writeup: https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue (covers MPMC; SPSC foundation is the same).
- Herlihy & Shavit, *The Art of Multiprocessor Programming*, ch. 10.
- Folly's `ProducerConsumerQueue` (facebook/folly on GitHub) — clean reference implementation.
- Drepper, *What Every Programmer Should Know About Memory*, §3.3.

## Open questions

- How does `std::hardware_destructive_interference_size` compare to hardcoded 64? Apparently unreliable across compilers — worth measuring.
- Wait-free vs lock-free: SPSC as described is wait-free (both sides progress in bounded steps). What breaks this guarantee on the move to MPMC, exactly?
- io_uring submission/completion queues are SPSC across the user/kernel boundary. How does the kernel handle the memory ordering, given userspace can lie about it?
