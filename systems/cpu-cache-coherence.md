# CPU Cache Coherence

The reason "atomic" is more than syntactic sugar, and the reason `alignas(64)` matters in low-latency code. Understanding the coherence protocol is what separates "wrote a lock-free queue" from "wrote a lock-free queue that's actually fast."

## The model in one paragraph

Each core has its own L1 (and usually L2) cache. When two cores read the same cache line, both have a copy. When one writes, the protocol must ensure the other sees a consistent value — either by invalidating the other's copy (MESI) or by transferring ownership. The unit of coherence is the cache line (64 bytes on x86_64 and most ARM), not the variable. Two unrelated variables on the same line are coupled, even if no thread touches both.

## MESI states

The classic four-state protocol per cache line:

- **M (Modified)** — this core has the only copy, and it's been written. RAM is stale.
- **E (Exclusive)** — this core has the only copy, but it matches RAM.
- **S (Shared)** — multiple cores have copies; all match RAM.
- **I (Invalid)** — line not present.

State transitions on read/write events drive the bus traffic. AMD's MOESI adds an "Owned" state to allow dirty-line forwarding without write-back; the practical implications are similar.

## What "writing" actually triggers

A write to a line in S forces a Read-For-Ownership (RFO): bus message to other cores → they invalidate → this core takes M. That's the slow part. RFO costs scale with the number of sharers and the topology distance to them. On a NUMA system, an RFO across the inter-socket link is dramatically more expensive than within a socket.

## False sharing — the canonical low-latency footgun

Two threads, two unrelated counters on the same cache line. Each increment by either thread invalidates the other's copy. They behave as if they were the same variable, with no programmer-visible reason. → I hit this exact problem in [Celeritas](../project-retrospectives/celeritas.md) on the SPSC ring buffer indices; → [`./lockfree-spsc-queues.md`](./lockfree-spsc-queues.md) is the long version.

Diagnose with `perf c2c` (cache-to-cache analysis). It tells you which lines are bouncing and which sources are hitting them.

Fix with `alignas(64)` or `std::hardware_destructive_interference_size` (the latter is theoretically portable but the value is wrong on some compiler/hardware combos — verify before trusting it).

## Memory barriers and the store buffer

x86 is TSO: Total Store Order. Stores can't be reordered with other stores; loads can't be reordered with other loads; but **a load can be reordered before an earlier store to a different address**. This is because the store sits in a per-core store buffer waiting to be flushed; a later load to a different line doesn't wait.

Hence `mfence` (or `lock`-prefixed instructions) for the load-after-store pairs that need ordering — most famously the seqlock and Dekker's algorithm.

ARM is much weaker: stores can be reordered with stores, loads with loads, both with each other, all subject to the rules of `LDAR`/`STLR` and `DMB`. C++'s `memory_order_acquire`/`release` compile to LDAR/STLR on ARMv8, which are the cheap barriers.

## NUMA and the locality story

On a multi-socket server, "memory" is divided among NUMA nodes; each socket has its own memory controller. A core's RFO for a line owned by a remote socket goes across the inter-socket link (UPI on Intel, Infinity Fabric on AMD), which is order-of-magnitude slower than local L3.

Mitigations:

- `numactl --cpunodebind=N --membind=N` to colocate threads and their memory.
- `pthread_setaffinity_np` to pin threads to specific cores.
- First-touch allocation policy: the page is allocated on the NUMA node of the first thread to touch it. Initialize from the thread that will use it.

For a matching engine, all this matters; for a Lambda function, none of it does.

## Where I've used this

- [Celeritas](../project-retrospectives/celeritas.md) — `alignas(64)` on SPSC indices; padding messages; aware of (but didn't fix) NUMA placement on WSL2.
- [Invenio](../project-retrospectives/invenio.md) — when persistence lands and the index becomes mmap-backed, locality of posting-list storage becomes the dominant cost.

## References

- Drepper, *What Every Programmer Should Know About Memory*, 2007 — long, dense, still the best reference on this layer.
- Intel Software Developer's Manual, vol. 3A, ch. 8 (memory ordering).
- ARM ARM (Architecture Reference Manual), ch. B2.3 — the formal ARM memory model.
- Paul McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — free PDF; the Linux kernel's perspective.

## Open questions

- Apple Silicon (M-series): TSO mode for x86 emulation, otherwise weak. How does this interact with my Celeritas SPSC code if I rebuild it natively?
- AMD's CCX topology: with 8-core CCXes, intra-CCX coherence is fast but cross-CCX is much slower. Does pinning to within-CCX matter for SPSC throughput? Need to measure.
- DDIO (Data Direct I/O) on Intel Xeons: NIC writes go straight to L3, skipping memory. Big deal for kernel-bypass networking; how much of that machinery is exposed to userspace?
