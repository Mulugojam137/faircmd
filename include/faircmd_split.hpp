/*------------------------------------------------------------------------------
  faircmd_split.hpp — Split modes; machine store is a BAG (presence, not order)

  Purpose: availability/robustness checks where order does not matter.

  Guarantees:
    - Satisfies waits when a matching token is present; not FIFO.
    - Diagnostics -> std::cerr.

  API: same as other variants.
------------------------------------------------------------------------------*/
#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <atomic>
#include <thread>
#include <initializer_list>
#include <stdexcept>

namespace faircmd_split {
  namespace detail {
    inline std::mutex& mtx() { static std::mutex m; return m; }
    inline std::condition_variable& cv() { static std::condition_variable v; return v; }
    inline std::unordered_map<std::string,int>& bag() { static std::unordered_map<std::string,int> b; return b; }
    inline std::atomic<int>& default_fails() { static std::atomic<int> v{1000}; return v; }
    inline std::atomic<long long>& yield_ns() { static std::atomic<long long> v{0}; return v; }
  }

  inline void reset() noexcept {
    std::lock_guard<std::mutex> lk(detail::mtx());
    detail::bag().clear();
    detail::cv().notify_all();
  }
  inline void set_default_fails(int fails) noexcept { detail::default_fails().store(fails); }
  inline void set_yield_sleep(std::chrono::milliseconds ms) noexcept { detail::yield_ns().store(ms.count()*1000000ll); }

  inline void preload(std::initializer_list<const char*> tokens) {
    std::lock_guard<std::mutex> lk(detail::mtx());
    for (auto* s : tokens) detail::bag()[s]++;
    detail::cv().notify_all();
  }
  inline void push(std::string token) {
    std::lock_guard<std::mutex> lk(detail::mtx());
    detail::bag()[std::move(token)]++;
    detail::cv().notify_all();
  }

  inline void dump_pending_to_stderr() noexcept {
    std::lock_guard<std::mutex> lk(detail::mtx());
    std::cerr << "[faircmd-split][pending=" << detail::bag().size() << "] { ";
    for (auto& kv : detail::bag()) std::cerr << '"' << kv.first << "\": " << kv.second << "  ";
    std::cerr << "}\n";
  }

  inline void WaitForCommand(const char* who, const char* expected) {
    const std::string exp = expected ? expected : "";
    int remaining = detail::default_fails().load();
    std::unique_lock<std::mutex> lk(detail::mtx());

    while (true) {
      auto it = detail::bag().find(exp);
      if (it != detail::bag().end() && it->second > 0) {
        if (--(it->second) == 0) detail::bag().erase(it);
        detail::cv().notify_all();
        return;
      }

      if (--remaining <= 0) {
        std::cerr << "[faircmd-split][ERROR] " << (who?who:"?")
                  << " expected \"" << exp << "\" but it was not present; giving up after "
                  << detail::default_fails().load() << " waits\n";
        throw std::runtime_error("faircmd_split: token not present");
      }

      auto ns = detail::yield_ns().load();
      lk.unlock();
      if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
      else std::this_thread::yield();
      lk.lock();
    }
  }

  // Waits for a full line (instead of token-by-token).
  inline void WaitForCommandLoose(const char* who, const char* expected) {
      const std::string exp = expected ? expected : "";
      int remaining = detail::default_fails().load();

      while (true) {
          std::string line;
          if (!std::getline(std::cin, line)) {
              throw std::runtime_error("faircmd_split: stdin closed while waiting");
          }

          if (line == exp) return;

          if (--remaining <= 0) {
              std::cerr << "[faircmd-split][ERROR] " << (who ? who : "?")
                  << " expected line \"" << exp
                  << "\" but it was not entered; giving up after "
                  << detail::default_fails().load() << " attempts\n";
              throw std::runtime_error("faircmd_split: line not entered");
          }

          auto ns = detail::yield_ns().load();
          if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
          else std::this_thread::yield();
      }
  }
} // namespace faircmd_split
