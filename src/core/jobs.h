// Photon - minimal job system: a fixed pool of worker threads that run
// parallel-for loops. Workers spin briefly between calls (the solver issues
// many small loops per time step) and sleep when the pool goes idle.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define PHOTON_PAUSE() _mm_pause()
#else
#define PHOTON_PAUSE() ((void)0)
#endif

namespace photon {

class JobPool {
public:
    explicit JobPool(int threads = 0) {
        int n = threads > 0 ? threads : (int)std::thread::hardware_concurrency();
        if (n < 1) n = 1;
        for (int i = 1; i < n; ++i) workers_.emplace_back([this] { workerLoop(); });
    }
    ~JobPool() {
        quit_.store(true);
        generation_.fetch_add(1, std::memory_order_release);
        for (auto& t : workers_) t.join();
    }
    JobPool(const JobPool&) = delete;
    JobPool& operator=(const JobPool&) = delete;

    int threadCount() const { return (int)workers_.size() + 1; }

    // Calls fn(begin, end) over [0, n). The calling thread participates.
    void parallelFor(int n, const std::function<void(int, int)>& fn, int minChunk = 1) {
        if (n <= 0) return;
        int t = threadCount();
        int chunk = std::max(minChunk, (n + t * 4 - 1) / (t * 4));
        if (workers_.empty() || n <= chunk) { fn(0, n); return; }
        fn_ = &fn;
        n_ = n;
        chunk_ = chunk;
        next_.store(0, std::memory_order_relaxed);
        pending_.store((int)workers_.size(), std::memory_order_relaxed);
        generation_.fetch_add(1, std::memory_order_release);
        runChunks();
        // spin, then yield: under oversubscription (another busy process) pure
        // spinning starves the workers we are waiting for
        for (int spin = 0; pending_.load(std::memory_order_acquire) > 0; ++spin) {
            if (spin < 1000) PHOTON_PAUSE();
            else std::this_thread::yield();
        }
        fn_ = nullptr;
    }

private:
    void runChunks() {
        for (;;) {
            int b = next_.fetch_add(chunk_, std::memory_order_relaxed);
            if (b >= n_) break;
            (*fn_)(b, std::min(n_, b + chunk_));
        }
    }
    void workerLoop() {
        uint64_t seen = generation_.load(std::memory_order_acquire);
        int idle = 0;
        for (;;) {
            uint64_t g = generation_.load(std::memory_order_acquire);
            if (g == seen) {
                ++idle;
                if (idle < 1000) PHOTON_PAUSE();
                else if (idle < 20000) std::this_thread::yield();
                else std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }
            seen = g;
            idle = 0;
            if (quit_.load()) return;
            runChunks();
            pending_.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    std::vector<std::thread> workers_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<int> next_{0};
    std::atomic<int> pending_{0};
    std::atomic<bool> quit_{false};
    const std::function<void(int, int)>* fn_ = nullptr;
    int n_ = 0, chunk_ = 1;
};

} // namespace photon
