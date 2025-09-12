/*------------------------------------------------------------------------------
  faircmd_hybrid.hpp — Hybrid stdin + scripted queue (human+machine)

  Purpose: interactive demos and manual runs with a little scripted help.
  Behavior: reads tokens from BOTH an in-memory queue and (optionally) live stdin
            via a background pumper thread.

  Guarantees:
    - Convenience over determinism. Best-effort order; not strictly FIFO.
    - Writes diagnostics ONLY to std::cerr (never to your JSON/output stream).

  API:
    void reset() noexcept;
    void set_default_fails(int) noexcept;
    void set_yield_sleep(std::chrono::milliseconds) noexcept;
    void preload(std::initializer_list<const char*>);
    void push(std::string);
    void WaitForCommand(const char* who, const char* expected);
    void dump_pending_to_stderr() noexcept;

    // Optional: start/stop stdin pumper thread (reads lines, pushes as tokens)
    void start_stdin_pumper();
    void stop_stdin_pumper();

  Use for: tinkering, REPL-like sessions. Not recommended for CI.
------------------------------------------------------------------------------*/
#pragma once
#include <deque>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <atomic>
#include <thread>

namespace faircmd_hybrid {
  namespace detail {
    inline std::mutex& mtx() { static std::mutex m; return m; }
    inline std::condition_variable& cv() { static std::condition_variable v; return v; }
    inline std::deque<std::string>& q() { static std::deque<std::string> d; return d; }
    inline std::atomic<int>& default_fails() { static std::atomic<int> v{1000}; return v; }
    inline std::atomic<long long>& yield_ns() { static std::atomic<long long> v{0}; return v; }
    inline std::atomic<bool>& pumper_running() { static std::atomic<bool> b{false}; return b; }
    inline std::thread*& pumper_thread() { static std::thread* t = nullptr; return t; }

    inline void push_unlocked(std::string s) {
      q().push_back(std::move(s));
      cv().notify_all();
    }
  }

  inline void reset() noexcept {
    std::lock_guard<std::mutex> lk(detail::mtx());
    detail::q().clear();
    detail::cv().notify_all();
  }
  inline void set_default_fails(int fails) noexcept { detail::default_fails().store(fails); }
  inline void set_yield_sleep(std::chrono::milliseconds ms) noexcept { detail::yield_ns().store(ms.count()*1000000ll); }

  inline void preload(std::initializer_list<const char*> tokens) {
    std::lock_guard<std::mutex> lk(detail::mtx());
    for (auto* s : tokens) detail::push_unlocked(s);
  }
  inline void push(std::string token) {
    std::lock_guard<std::mutex> lk(detail::mtx());
    detail::push_unlocked(std::move(token));
  }

  inline void dump_pending_to_stderr() noexcept {
    std::lock_guard<std::mutex> lk(detail::mtx());
    std::cerr << "[faircmd-hybrid][pending=" << detail::q().size() << "] { ";
    for (auto& s : detail::q()) std::cerr << '"' << s << "" ";
    std::cerr << "}
";
  }

  inline void WaitForCommand(const char* who, const char* expected) {
    const std::string exp = expected ? expected : "";
    int remaining = detail::default_fails().load();
    std::unique_lock<std::mutex> lk(detail::mtx());

    while (true) {
      // pump one line from stdin if available (non-blocking-ish: check and switch)
      lk.unlock();
      // Note: getline will block if no line; we only try when there is something buffered
      // but standard C++ doesn't give a portable in_avail; keep it simple: do nothing here.
      lk.lock();

      if (!detail::q().empty() && detail::q().front() == exp) {
        detail::q().pop_front();
        detail::cv().notify_all();
        return;
      }

      if (--remaining <= 0) {
        if (!detail::q().empty()) {
          std::cerr << "[faircmd-hybrid][ERROR] " << (who?who:"?") << " expected ""
                    << exp << "" but front is "" << detail::q().front()
                    << ""; giving up after " << detail::default_fails().load()
                    << " waits
";
        } else {
          std::cerr << "[faircmd-hybrid][ERROR] " << (who?who:"?") << " expected ""
                    << exp << "" but queue is empty; giving up
";
        }
        throw std::runtime_error("faircmd_hybrid: front mismatch/empty");
      }

      auto ns = detail::yield_ns().load();
      lk.unlock();
      if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
      else std::this_thread::yield();
      lk.lock();
    }
  }

  inline void start_stdin_pumper() {
    bool expected = false;
    if (!detail::pumper_running().compare_exchange_strong(expected, true)) return;
    detail::pumper_thread() = new std::thread([]{
      std::string line;
      while (detail::pumper_running().load()) {
        if (!std::getline(std::cin, line)) {
          // no line (EOF or blocked); yield a bit
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        std::lock_guard<std::mutex> lk(detail::mtx());
        detail::push_unlocked(line);
      }
    });
  }
  inline void stop_stdin_pumper() {
    if (!detail::pumper_running().load()) return;
    detail::pumper_running().store(false);
    if (auto* t = detail::pumper_thread()) {
      if (t->joinable()) t->join();
      delete t;
      detail::pumper_thread() = nullptr;
    }
  }
} // namespace faircmd_hybrid
