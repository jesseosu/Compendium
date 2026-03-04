# Lock-Free SPSC Benchmark

Toy version of the SPSC ring buffer that [Celeritas](../../project-retrospectives/celeritas.md) uses on its three internal pipeline edges. Two threads, one producer, one consumer, 10M items, percentile histogram on the producer side.

## What this measures (and what it doesn't)

It measures **per-push latency** on the producer thread — wall time between consecutive successful `try_push` calls. That's a proxy for end-to-end throughput, not for queueing latency seen by an item.

It does **not** measure:

- Queueing delay (item dequeued time minus item enqueued time). Adding this is a five-line change but doubles the timestamping overhead.
- Latency under contention with other workloads on the box.
- The cost of the cache-line padding vs an unpadded layout — to see that, edit `spsc.hpp`, remove the `alignas(64)` lines, rebuild, compare. Worth doing.

## Build & run

```
make
./bench
```

Output is something like:

```
SPSC bench: 10000000 iters, capacity 16384
  p50   =       42 ns/op
  p95   =       65 ns/op
  p99   =       95 ns/op
  p99.9 =      280 ns/op
  max   =    18000 ns/op
```

Numbers depend heavily on:

- CPU model and frequency scaling.
- Whether the threads land on the same physical core (HT pairs) or different cores.
- Queue depth (`kCapacity` in `main.cpp`).
- Compiler version and flags.
- System load.

Treat them as ordinal. Don't compare across machines without controlling for the above.

## Useful experiments to run from here

1. **Padding ablation.** Remove `alignas(64)` from the indices in `spsc.hpp` and re-run. Expect throughput to drop, sometimes dramatically.
2. **Capacity sweep.** Try `kCapacity = 64, 256, 4096, 65536`. Larger capacity hides backpressure spikes; tiny capacity is contention-bound.
3. **Pinning.** Pin producer to CPU 0 and consumer to CPU 2 (avoiding HT pair) with `taskset -c 0,2 ./bench`. Compare against the default.
4. **Memory ordering relaxation.** Change `memory_order_release` on the producer's index store to `memory_order_seq_cst` and back. On x86 there's almost no difference; on ARM the difference is real.

## Related notes

- [`../../systems/lockfree-spsc-queues.md`](../../systems/lockfree-spsc-queues.md) — the long writeup of the design.
- [`../../systems/cpu-cache-coherence.md`](../../systems/cpu-cache-coherence.md) — why the `alignas(64)` matters.
- [`../../papers/lmax-disruptor.md`](../../papers/lmax-disruptor.md) — the multi-consumer generalization.
