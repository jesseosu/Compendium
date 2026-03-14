# LSM-Trees

> O'Neil, Cheng, Gawlick, O'Neil. *The Log-Structured Merge-Tree (LSM-Tree).* Acta Informatica, 1996.

The storage engine behind LevelDB, RocksDB, Cassandra, ScyllaDB, HBase, BigTable, DynamoDB's storage layer, and roughly half of every modern distributed database. If you ever expect to reason about write-heavy storage performance, you need to understand this.

## The problem

B-trees are great for reads (logarithmic), great for point updates of small values. They're not great for sustained sequential write workloads — every insert touches a leaf, every leaf write is random I/O, and pages get repeatedly modified in place. On rotational disks this was a disaster; on SSDs it's still suboptimal because random writes burn write-amplification.

## The idea

Don't update in place. Buffer writes in memory; periodically flush sorted runs to disk; merge runs in the background to keep the on-disk count manageable. Reads consult the in-memory buffer + all on-disk runs.

```
write path:
  client write -> WAL (durability) + memtable (in-memory sorted)
  memtable full -> flush to immutable SSTable on disk
  background -> merge SSTables (compaction)

read path:
  consult memtable, then SSTables newest-to-oldest
  bloom filter per SSTable to skip files that can't contain the key
  index/fence pointers within an SSTable for the actual lookup
```

## SSTable shape

A Sorted String Table is, structurally, a sorted file with a sparse index at the end:

- Block of sorted (key, value) pairs.
- Sparse index pointing to the first key of each block (fence pointers).
- Bloom filter so a point query that doesn't match can return without reading any block.
- Footer with offsets to the index and bloom.

Append-only. Once written, never modified. Compaction produces *new* SSTables that supersede old ones; old ones are deleted after no reader holds them.

## Compaction strategies — the design space

This is where LSMs get opinionated.

- **Tiered.** Each level holds N SSTables of similar size. When a level fills, all N are merged into one big SSTable in the next level. Low write amplification; high read amplification (point read might check N SSTables per level); high space amplification.
- **Leveled.** Each level holds non-overlapping SSTables (one big sorted run, sharded into files). When a level overflows, one SSTable is merged into the next level (which may rewrite many files). High write amplification; low read amplification (one SSTable per level checked); low space amplification.
- **Hybrid (FIFO, time-windowed)** — for time-series workloads where old data can just be dropped.

The three amplifications form a trilemma — you can pick two:

- **Read amp** — how many disk reads per logical read.
- **Write amp** — total bytes written per logical byte.
- **Space amp** — disk used per logical byte stored.

Tiered minimises write amp at the cost of read and space amp. Leveled minimises read and space at the cost of write. RocksDB defaults to leveled; Cassandra defaults to tiered.

## Where it breaks

- **Compaction stalls.** If the workload writes faster than compaction can keep up, levels back up. Eventually new flushes are throttled or stalled. Compaction is the LSM's hidden cost; it competes for CPU and disk bandwidth with foreground reads.
- **Range scans across many SSTables.** Each SSTable contributes its own iterator; merging the iterators is essentially a heap merge, which gets expensive when the count is high. Leveled minimises this; tiered makes it worse.
- **Tombstones.** Deletes are writes (a "tombstone" record). Until compaction processes them, both the original and the tombstone are on disk; range scans must filter. Mass deletes are a known performance pothole.

## Where I've touched this

Nothing in the Compendium directly implements LSM, but [Arbiter](../project-retrospectives/arbiter.md) uses SQLite WAL mode, which is a single-tier journal with checkpointing. Same vocabulary — write-ahead log for durability, deferred merge into the main structure — applied to a B-tree instead of sorted runs.

## References

- The 1996 O'Neil paper (the original): https://www.cs.umb.edu/~poneil/lsmtree.pdf
- Petrov, *Database Internals* — chapters 6 and 7 are the cleanest modern overview.
- Dong, Callaghan, Galanis et al., *Optimizing Space Amplification in RocksDB*, CIDR 2017.
- The RocksDB wiki on tuning: https://github.com/facebook/rocksdb/wiki

## Open questions

- WiscKey (Lu et al., FAST 2016) — separating keys from values to reduce write amplification on large values. Worth reading; do any production systems actually use this approach?
- Bw-trees and FASTER (Microsoft's index work) — alternatives to both B-trees and LSMs. What does the design space look like beyond these two poles?
- Compaction scheduling: there's a real research topic in "when to compact" given mixed workloads. RocksDB has knobs but defaults are surprisingly load-bearing.
