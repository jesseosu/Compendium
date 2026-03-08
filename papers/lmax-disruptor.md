# The LMAX Disruptor

> Thompson, Farley, Barker, Gee, Stewart. *LMAX Disruptor: High Performance Alternative to Bounded Queues for Exchanging Data Between Concurrent Threads.* 2011.

The reason I built [Celeritas](../project-retrospectives/celeritas.md) the way I did, even though I read the paper afterwards. It's the canonical low-latency interthread-communication pattern for a reason.

## What it actually is

A single-writer ring buffer with sequence numbers, where consumers read by tracking a cursor against the writer's published sequence. Sounds like a queue. *Isn't a queue.* The distinction matters.

In a queue, slots are owned (an item is dequeued and gone). In the Disruptor, slots are addressed by sequence number; consumers don't remove anything, they just advance their own cursor. Multiple consumers can independently read the same slots. The producer can't overwrite a slot until all consumers are past it.

## Single Writer Principle

The headline idea: a single writer per data structure removes contention without locks. No CAS loops, no memory-barrier-heavy ownership transfer — just plain stores from one thread, with release semantics on the published-sequence variable.

This is the constraint that makes the Disruptor fast. Multiple writers needs CAS. One writer needs nothing more than the right ordering on one final store.

## Mechanical sympathy

The phrase the LMAX team used for "design that works *with* the hardware, not against it." Concretely:

- **Cache-line padding.** Sequence variables are padded so independent variables don't share a line. → [`../systems/cpu-cache-coherence.md`](../systems/cpu-cache-coherence.md) is the long version.
- **Pre-allocated ring slots.** Writers don't allocate; they overwrite. GC pressure goes to zero (the paper is in Java; the cost of allocation in the hot path was a measured problem).
- **Power-of-two ring size.** Index = sequence & (size − 1) is a cheap mask, not a modulo.
- **Sequence barriers** rather than locks for cross-consumer dependencies. A consumer that depends on another consumer's progress reads its sequence directly.

## The batch effect

The non-obvious throughput win. When a consumer is behind, it can read up to the writer's published sequence in one go — process the whole batch — then advance its own sequence once at the end. The batch amortizes the synchronization cost across many items.

So under load (when batches are large), throughput goes *up* — the controller is implicitly self-adapting to load.

## Where the simple version breaks

- **Multi-producer.** The paper acknowledges this; their multi-producer support uses a CAS to claim a slot, then the publication is still a single-writer release. It works, it's slower than the single-producer case.
- **Variable-size messages.** Pre-allocated slots imply fixed-size or pointer-to-payload. The latter reintroduces allocation.
- **Backpressure without spinning.** Disruptor consumers traditionally spin or yield. For low-utilization servers this wastes CPU; for trading workloads it's the right call.

## Where I've used this

[Celeritas](../project-retrospectives/celeritas.md) doesn't implement a literal Disruptor — it uses three SPSC ring buffers between four threads. But the design vocabulary is the same: pre-allocated slots, sequence/cursor semantics, single-writer per edge, padded indices. The Disruptor is what you get when you generalise SPSC to multiple consumers reading the same stream.

## References

- The original paper (LMAX, 2011): https://lmax-exchange.github.io/disruptor/files/Disruptor-1.0.pdf
- Reference implementation: https://github.com/LMAX-Exchange/disruptor (Java).
- Martin Thompson's "Mechanical Sympathy" blog — context for why the LMAX team thinks this way.
- Vyukov's bounded SPSC writeup — for comparison against the simpler queue: https://www.1024cores.net/.

## Open questions

- The Disruptor in Java vs C++: how much of the win comes from avoiding GC vs from the design itself? A C++ implementation against `boost::lockfree::queue` would be the controlled experiment.
- Aeron (Real Logic's later messaging library, by some of the same authors) builds on these ideas with persistence. Is the persistence story compatible with single-writer, or does it force back into multi-writer territory?
- For matching engines specifically, is there a meaningful win from the multi-consumer pattern (matching consumer + risk-checking consumer + audit consumer) vs my SPSC-per-edge approach?
