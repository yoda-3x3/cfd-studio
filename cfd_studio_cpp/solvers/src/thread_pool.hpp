#pragma once

// Persistent std::thread pool for the threaded kernel backend --
// deliberately NOT built on OpenMP. The earlier Fortran build's OpenMP
// version stalled on real hardware mid-simulation, consistent with
// fork/join overhead accumulating over the thousands of `!$OMP PARALLEL`
// region entries/exits a long run produces (see kernels_scalar.cpp's file
// header). This pool creates its worker threads exactly once, for the life
// of the pool -- each parallel_for() call only wakes already-running
// threads via a condition variable and waits on another for completion, no
// OS thread creation/teardown per call, which is the architectural
// difference from OpenMP's default per-region thread-team setup that this
// design exists to avoid re-learning that lesson from.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cfd::solvers::detail {

class ThreadPool {
public:
    explicit ThreadPool(int n_threads) { resize(n_threads); }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void resize(int n_threads) {
        n_threads = std::max(1, n_threads);
        if (n_threads == thread_count()) return;
        shutdown();
        stop_ = false;
        generation_ = 0;
        workers_.reserve(static_cast<std::size_t>(n_threads));
        for (int t = 0; t < n_threads; ++t) {
            workers_.emplace_back([this, t] { worker_loop(t); });
        }
    }

    [[nodiscard]] int thread_count() const { return static_cast<int>(workers_.size()); }

    // Runs fn(i) for every i in [0, n), split into one contiguous chunk per
    // worker thread (uniform per-cell cost in these kernels, so static
    // chunking is fine -- no work-stealing/atomic-index contention needed).
    // Blocks until every chunk has completed.
    void parallel_for(int n, const std::function<void(int)>& fn) {
        if (n <= 0) return;
        int workers = thread_count();
        if (workers <= 1 || n < workers * 4) {
            // Not worth the synchronization overhead for a small extent --
            // exactly the "small grids favor scalar" tradeoff this backend
            // exists alongside, applied per-call as a cheap early-out too.
            for (int i = 0; i < n; ++i) fn(i);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            current_fn_ = &fn;
            current_n_ = n;
            remaining_ = workers;
            ++generation_;
        }
        cv_start_.notify_all();

        std::unique_lock<std::mutex> lock(mtx_);
        cv_done_.wait(lock, [this] { return remaining_ == 0; });
    }

private:
    void worker_loop(int thread_index) {
        std::uint64_t last_seen = 0;
        while (true) {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_start_.wait(lock, [this, last_seen] { return generation_ != last_seen || stop_; });
            if (stop_) return;
            last_seen = generation_;
            const std::function<void(int)>* fn = current_fn_;
            int n = current_n_;
            int workers = thread_count();
            lock.unlock();

            int chunk = (n + workers - 1) / workers;
            int begin = thread_index * chunk;
            int end = std::min(n, begin + chunk);
            for (int i = begin; i < end; ++i) (*fn)(i);

            {
                std::lock_guard<std::mutex> done_lock(mtx_);
                if (--remaining_ == 0) cv_done_.notify_one();
            }
        }
    }

    void shutdown() {
        if (workers_.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_start_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

    std::vector<std::thread> workers_;
    std::mutex mtx_;
    std::condition_variable cv_start_, cv_done_;
    bool stop_ = false;
    std::uint64_t generation_ = 0;
    int remaining_ = 0;
    const std::function<void(int)>* current_fn_ = nullptr;
    int current_n_ = 0;
};

} // namespace cfd::solvers::detail
