# Celeritas

> A C++20 simulation of the core infrastructure of an electronic trading venue: order ingest, matching, execution publishing, market-data publishing. (Repo: `https://github.com/jesseosu/celeritas`)

## What it is

Celeritas takes the standard exchange data path and implements it as a four-thread pipeline connected by lock-free SPSC ring buffers. The point isn't to be a real venue. It's to be an honest, measurable model of one, where every architectural choice maps to a real low-latency-trading concern (cache locality, false sharing, queueing, scheduler interference).

## What I built

- **4-thread pipeline.** Producer (order generator) → Matching engine → Execution publisher → Market-data publisher. Three SPSC queues between them: `in_q`, `exec_q`, `md_q`. End-of-stream is signalled with sentinel messages (`ExecType::End`, `symbol == UINT32_MAX`) so consumers terminate without an extra control channel.
- **Order book.** `std::map<int, std::deque<Order>>` keyed by integer price, with each price level as a FIFO. Price-time priority. Iteration over levels breaks at the first unmatched price; no binary search.
- **Backpressure.** When a queue saturates, the writer spins on `cpu_relax()` then yields. Chosen over condition variables to keep the wakeup path off the critical path.
- **Latency capture.** `std::chrono::steady_clock::now()` at ingest and at publish; nanosecond timestamps stored alongside each order, percentiles computed at end.
- **Cache layout.** Message structs aligned to avoid false sharing across the ring boundary.

Key files: `apps/pipeline_runner/main.cpp`, `src/exchange/OrderBook.cpp`, `src/common/Time.cpp`, `src/proto/Decoder.cpp`.

Published bench (WSL2 Ubuntu, GCC 13.3.0, default scheduler, no pinning): **p50 ≈ 14 µs, p95 ≈ 50 µs, p99 ≈ 70 µs, max ≈ 11 ms.** The max is the interesting number, let's see "What I'd do differently."

## What I learned

- **SPSC is a different beast from MPMC.** With exactly one writer and one reader you don't need CAS at all; release on the producer's index store and acquire on the consumer's index load is enough. The C++ memory model carries the dependent payload writes for free. Detail in → [`../systems/lockfree-spsc-queues.md`](../systems/lockfree-spsc-queues.md).
- **False sharing is not a microbenchmark curiosity.** Putting `head` and `tail` on the same cache line means every producer write invalidates the consumer's cached copy of `tail` and vice versa, even though they're logically independent. `alignas(64)` on each index is non-negotiable. → [`../systems/cpu-cache-coherence.md`](../systems/cpu-cache-coherence.md).
- **The Disruptor pattern is the conceptual ancestor.** Three SPSC queues between four pinned threads is the simplified Disruptor lineage, a ring buffer plus cursors per consumer. Reading the LMAX paper after I'd already built this clarified why the design works. → [`../papers/lmax-disruptor.md`](../papers/lmax-disruptor.md).
- **Tail latency comes from outside your code.** The matching path is microseconds, but the max is milliseconds. Almost every long tail traces to something the OS did to me; scheduler eviction, page fault, IRQ — not to a slow code path. → [`../systems/linux-scheduler-cfs.md`](../systems/linux-scheduler-cfs.md).
- **Steady_clock is the right primitive for latency.** `system_clock` jumps when ntpd corrects; `steady_clock` is monotonic. Cheap on x86 thanks to vDSO.
- **A `std::map` order book is fine for v1.** Logarithmic in level count, FIFO at each level. The cost is that you allocate on level creation and that price levels aren't memory-adjacent. Worth it for correctness-first.

## Design decisions and tradeoffs

- **SPSC per pipeline edge instead of MPMC anywhere.** I picked the strictest queue I could because it's the cheapest. The cost is a fixed-shape pipeline, adding parallelism inside a stage means redesigning, not just spinning more threads. For a matching engine that's the right tradeoff; matching has to be single-threaded per book anyway.
- **`std::map<int, std::deque>` over a flat array of price levels.** Map gave me logarithmic insert/lookup with no thought about price bounds. A flat array indexed by tick would be faster (constant time, contiguous memory) but needs price-bound assumptions and pre-allocation. I'd revisit this. see below.
- **Spin + yield backpressure over condition variables.** The condition-variable wakeup path costs microseconds and adds tail latency. Spinning costs CPU but the variance is bounded. For a single-process simulation pinned to dedicated cores, spinning wins.
- **Sentinel-based shutdown.** Pushing an `End` message through the pipeline guarantees that consumers drain their input before they exit, with no extra control plane. The cost is one branch per dequeue checking for the sentinel.
- **No external dependencies.** Just the standard library, CMake. Intentionally, I wanted to prove the design without leaning on Folly's `ProducerConsumerQueue` or boost::lockfree. The cost is reinventing things; the upside is no opacity.

## What I'd do differently

- **Lead change: chase the ~11 ms tail.** p50 of 14 µs is fine for a default-scheduler WSL2 run, but max of 11 ms is a four-order-of-magnitude tail. That gap is where the systems-engineering conversation lives. The next pass would be:
  - **Pin threads with `pthread_setaffinity_np`** to dedicated cores; ideally those cores reserved with `isolcpus=` so the scheduler leaves them alone.
  - **`mlockall(MCL_CURRENT | MCL_FUTURE)`** to keep the working set out of swap and pin page tables.
  - **Huge pages** for the order book and the ring buffers to cut TLB pressure.
  - **`SCHED_FIFO`** (or at least nice -20) on the matching thread.
  - Instrument with `perf sched` and `perf record -e major-faults` to confirm what's actually causing the spikes before adding mitigations blindly.
- **Replace `std::map` with a flat array of price levels.** Prices in the simulator are bounded integers post-tick-normalization. A `std::array<PriceLevel, N>` indexed by tick is constant-time, cache-friendly, and predictable. Map's tree rotations on insert are exactly the kind of variance you want to remove.
- ******Batched commit on the producer side.** Right now each `enqueue` performs a release store. A Disruptor-style batched publish, where you write N items then release the cursor once, would amortise the release barrier and improve throughput at the cost of slightly higher per-message latency. Worth measuring.
- **Move from `cpu_relax` spin to `pause` + adaptive backoff.** A short `_mm_pause` loop, then `sched_yield`, then a futex wait. Spinning forever wastes power and CPU when the queue is genuinely empty for a long time. The adaptive shape gives you the best of both.
- **Add proper benchmarking infrastructure.** Right now the latency capture is in the runner. A separate harness that produces a histogram (HdrHistogram-style) and runs warmup + measurement phases with explicit cool-down would make the numbers more comparable across runs.
- **Run on real Linux.** WSL2 is a fine development environment; it's a bad benchmarking environment. The numbers I have are honest but they're upper bounds on what the design is capable of, not measurements of it.

## Links

- Repo: `https://github.com/jesseosu/celeritas`
- Compendium notes:
  - [`../systems/lockfree-spsc-queues.md`](../systems/lockfree-spsc-queues.md)
  - [`../systems/cpu-cache-coherence.md`](../systems/cpu-cache-coherence.md)
  - [`../systems/linux-scheduler-cfs.md`](../systems/linux-scheduler-cfs.md)
  - [`../papers/lmax-disruptor.md`](../papers/lmax-disruptor.md)
  - [`../experiments/lockfree-spsc-bench/`](../experiments/lockfree-spsc-bench/) - toy SPSC benchmark of the pattern Celeritas uses internally.
- External:
  - Thompson et al., *The LMAX Disruptor*, 2011.
  - Drepper, *What Every Programmer Should Know About Memory*, §3.3 on cache coherence.
  - Vyukov's writeups at https://www.1024cores.net/.
