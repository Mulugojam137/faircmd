/*------------------------------------------------------------------------------
  faircmd_machine.hpp — Strict FIFO scripted command queue (deterministic)

  Purpose: CI and protocol tests that require exact token order.

  Guarantees:
    - Consumes only when expected == queue.front().
    - Cooperative wait with per-item fail budget.
    - Silent by default; optional dump to std::cerr.

  API:
    void reset() noexcept;                                        // clear state
    void set_default_fails(int fails) noexcept;                   // e.g., 1000
    void set_yield_sleep(std::chrono::milliseconds) noexcept;     // e.g., 0–1ms
    void preload(std::initializer_list<const char*> tokens);      // seed script
    void push(std::string token);                                 // inject at tail
    void WaitForCommand(const char* who, const char* expected);
      // Blocks until queue.front() == expected; otherwise yields and retries.
      // Decrements per-item fail budget; throws when exhausted.

    void dump_pending_to_stderr() noexcept;  // optional debug; NEVER use main os

  Tips:
    - Keep JSON/structured output separate from diagnostics (stderr).
    - If you need human input for a one-off run, switch temporarily to the
      hybrid variant, but don’t use it in CI.
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

namespace faircmd_machine {
  namespace detail {
    inline std::mutex& mtx() { static std::mutex m; return m; }
    inline std::condition_variable& cv() { static std::condition_variable v; return v; }
    inline std::deque<std::string>& q() { static std::deque<std::string> d; return d; }
    inline std::atomic<int>& default_fails() { static std::atomic<int> v{1000}; return v; }
    inline std::atomic<long long>& yield_ns() { static std::atomic<long long> v{0}; return v; }
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
    for (auto* s : tokens) detail::q().emplace_back(s);
    detail::cv().notify_all();
  }
  inline void push(std::string token) {
    std::lock_guard<std::mutex> lk(detail::mtx());
    detail::q().push_back(std::move(token));
    detail::cv().notify_all();
  }

  inline void dump_pending_to_stderr() noexcept {
    std::lock_guard<std::mutex> lk(detail::mtx());
    std::cerr << "[faircmd][pending=" << detail::q().size() << "] { ";
    for (auto& s : detail::q()) std::cerr << '"' << s << "" ";
    std::cerr << "}
";
  }

  inline void WaitForCommand(const char* who, const char* expected) {
    const std::string exp = expected ? expected : "";
    int remaining = detail::default_fails().load();
    std::unique_lock<std::mutex> lk(detail::mtx());

    while (true) {
      // wait for any token
      detail::cv().wait(lk, []{ return !detail::q().empty(); });

      if (detail::q().front() == exp) {
        detail::q().pop_front();
        detail::cv().notify_all();
        return;
      }

      if (--remaining <= 0) {
        std::cerr << "[faircmd][ERROR] " << (who?who:"?") << " expected ""
                  << exp << "" but saw "" << detail::q().front()
                  << ""; giving up after " << detail::default_fails().load()
                  << " waits
";
        throw std::runtime_error("faircmd_machine: front mismatch");
      }

      // cooperative yield
      auto ns = detail::yield_ns().load();
      lk.unlock();
      if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
      else std::this_thread::yield();
      lk.lock();
    }
  }
} // namespace faircmd_machine
