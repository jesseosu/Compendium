// SPSC ring buffer microbenchmark.
//
// Two threads (producer + consumer), N = 10M items pushed through. We measure
// the per-item latency on the producer side as the time between successful
// pushes (i.e. throughput-1, in nanoseconds), and report a histogram of
// percentiles.
//
// Numbers are highly dependent on CPU, kernel scheduling, queue depth, and
// whether the threads land on the same physical core. Treat them as ordinal,
// not absolute.
#include "spsc.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kCapacity = 1u << 14;   // 16384 slots
constexpr std::size_t kIters    = 10'000'000;

using Clock = std::chrono::steady_clock;
using Ns    = std::chrono::nanoseconds;

double percentile(std::vector<int64_t>& v, double p) {
    if (v.empty()) return 0.0;
    const auto n = static_cast<std::size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + n, v.end());
    return static_cast<double>(v[n]);
}

}  // namespace

int main() {
    SPSCQueue<std::int64_t, kCapacity> q;
    std::atomic<bool> done{false};

    std::vector<std::int64_t> samples;
    samples.reserve(kIters);

    std::thread consumer([&] {
        std::int64_t v;
        std::size_t got = 0;
        while (got < kIters) {
            if (q.try_pop(v)) {
                ++got;
            }
        }
        done.store(true, std::memory_order_release);
    });

    auto last = Clock::now();
    for (std::size_t i = 0; i < kIters; ++i) {
        while (!q.try_push(static_cast<std::int64_t>(i))) {
            // queue full -- spin
        }
        const auto now = Clock::now();
        samples.push_back(std::chrono::duration_cast<Ns>(now - last).count());
        last = now;
    }

    consumer.join();
    (void)done.load(std::memory_order_acquire);

    const double p50  = percentile(samples, 0.50);
    const double p95  = percentile(samples, 0.95);
    const double p99  = percentile(samples, 0.99);
    const double p999 = percentile(samples, 0.999);
    const auto max_it = std::max_element(samples.begin(), samples.end());
    const std::int64_t maxv = (max_it == samples.end()) ? 0 : *max_it;

    std::printf("SPSC bench: %zu iters, capacity %zu\n", kIters, kCapacity);
    std::printf("  p50   = %8.0f ns/op\n", p50);
    std::printf("  p95   = %8.0f ns/op\n", p95);
    std::printf("  p99   = %8.0f ns/op\n", p99);
    std::printf("  p99.9 = %8.0f ns/op\n", p999);
    std::printf("  max   = %8lld ns/op\n", static_cast<long long>(maxv));
    return 0;
}
