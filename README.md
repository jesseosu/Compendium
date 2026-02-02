# Compendium

Working notebook for systems internals, low-latency engineering, and quant interview prep. Notes on papers I've read, experiments I've run, problems I've drilled, and retrospectives on the projects I've shipped.

## How to read this

- [`skills.md`](./skills.md) - capability inventory. Skim this first if you want a 30-second view of what I can do, organized by topic with links into the rest of the repo.
- [`project-retrospectives/`](./project-retrospectives/) - one file per shipped project, covering what I built, what I learned, and what I'd do differently. The files most worth your time are [`celeritas.md`](./project-retrospectives/celeritas.md) and [`vigil.md`](./project-retrospectives/vigil.md).
- [`systems/`](./systems/) and [`papers/`](./papers/) - topical notes on things I'm studying. Cross-linked from the retrospectives wherever the theory underpins something I built.
- [`experiments/`](./experiments/) - small, runnable C/C++ programs that measure one specific thing. `make` in any subdirectory.
- [`interview-prep/`](./interview-prep/) - probability, mental math, and coding pattern notes with worked LeetCode solutions.
- [`books/`](./books/) - chapter-level notes on the books I'm working through.

## Currently focused on

- Lock-free data structures beyond SPSC (MPMC tradeoffs, hazard pointers).
- Linux scheduler - the EEVDF transition replacing CFS in 6.6+.
- BM25 internals and inverted-index compression (varint, Stream VByte).
- io_uring vs epoll for high-fanout IO.

## External repos referenced

These are mine. The retrospectives in this repo connect back to them.

- [Celeritas](https://github.com/jesseosu/celeritas) - C++20 low-latency exchange / matching engine.
- [Vigil](https://github.com/jesseosu/vigil) - zero-dependency C11 system-monitoring daemon.
- [Invenio](https://github.com/jesseosu/invenio) - full-text search engine in Go (BM25 from scratch).
- [Cirrus](https://github.com/jesseosu/cirrus) - AWS fleet self-healing platform.
- [Arbiter](https://github.com/jesseosu/Arbiter) - content-moderation pipeline simulating TikTok Live.
- [CloudNative-E-Commerce](https://github.com/jesseosu/CloudNative-E-Commerce) - serverless e-commerce platform on AWS.

## Building the experiments

Each experiment is self-contained. Linux only.

```bash
cd experiments/lockfree-spsc-bench && make && ./bench
cd experiments/io-uring-hello       && make && ./hello   # needs Linux 5.1+ and liburing-dev
cd experiments/syscall-latency-measure && make && ./latency
```

## Layout

```
papers/                  # paper writeups (LSM, Raft, LMAX Disruptor)
systems/                 # systems internals notes
interview-prep/          # probability, mental math, coding patterns
experiments/             # runnable benchmarks and demos
books/                   # ongoing book notes
project-retrospectives/  # one per shipped project
skills.md                # capability inventory (start here)
```

## License

Code under [MIT](./LICENSE). Prose under CC-BY-4.0.
