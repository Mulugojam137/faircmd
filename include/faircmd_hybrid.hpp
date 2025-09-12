/*------------------------------------------------------------------------------
  faircmd_hybrid.hpp — Hybrid stdin + scripted queue (human+machine)

  Demo-friendly variant for Windows consoles:
  - Uses blocking std::getline() (typing works reliably in PowerShell/ConHost).
  - stop_stdin_pumper() detaches the thread (no hang on join).
    NOTE: This is appropriate for demos/tests that end soon after stopping.
          For long-lived apps, prefer a fully non-blocking/Windows-API pumper.

  API:
    void reset() noexcept;
    void set_default_fails(int) noexcept;
    void set_yield_sleep(std::chrono::milliseconds) noexcept;
    void preload(std::initializer_list<const char*>);
    void push(std::string);
    void WaitForCommand(const char* who, const char* expected);       // strict front match
    void WaitForCommandLoose(const char* who, const char* expected);  // scan/skip to first match
    void dump_pending_to_stderr() noexcept;

    void start_stdin_pumper();  // blocking getline-based pumper
    void stop_stdin_pumper();   // detaches pumper to avoid join hang

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
#include <sstream>
#include <initializer_list>

namespace faircmd_hybrid {
    namespace detail {
        inline std::mutex& mtx() { static std::mutex m; return m; }
        inline std::condition_variable& cv() { static std::condition_variable v; return v; }
        inline std::deque<std::string>& q() { static std::deque<std::string> d; return d; }
        inline std::atomic<int>& default_fails() { static std::atomic<int> v{ 1000 }; return v; }
        inline std::atomic<long long>& yield_ns() { static std::atomic<long long> v{ 0 }; return v; }

        // pumper state
        inline std::atomic<bool>& pumper_running() { static std::atomic<bool> b{ false }; return b; }
        inline std::thread*& pumper_thread() { static std::thread* t = nullptr; return t; }

        inline void push_unlocked(std::string s) {
            q().push_back(std::move(s));
            cv().notify_all();
        }

        // helper: find expected anywhere; erase up to and including it
        inline bool erase_up_to_expected_unlocked(const std::string& exp) {
            auto& Q = q();
            for (auto it = Q.begin(); it != Q.end(); ++it) {
                if (*it == exp) {
                    Q.erase(Q.begin(), std::next(it));
                    cv().notify_all();
                    return true;
                }
            }
            return false;
        }
    } // namespace detail

    inline void reset() noexcept {
        std::lock_guard<std::mutex> lk(detail::mtx());
        detail::q().clear();
        detail::cv().notify_all();
    }

    inline void set_default_fails(int fails) noexcept { detail::default_fails().store(fails); }
    inline void set_yield_sleep(std::chrono::milliseconds ms) noexcept {
        detail::yield_ns().store(ms.count() * 1000000ll);
    }

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
        for (auto& s : detail::q()) std::cerr << '"' << s << "\" ";
        std::cerr << "}\n";
    }

    // STRICT: only consumes when expected == queue.front()
    inline void WaitForCommand(const char* who, const char* expected) {
        const std::string exp = expected ? expected : "";
        int remaining = detail::default_fails().load();
        std::unique_lock<std::mutex> lk(detail::mtx());

        while (true) {
            if (!detail::q().empty() && detail::q().front() == exp) {
                detail::q().pop_front();
                detail::cv().notify_all();
                return;
            }

            if (--remaining <= 0) {
                if (!detail::q().empty()) {
                    std::cerr << "[faircmd-hybrid][ERROR] " << (who ? who : "?")
                        << " expected \"" << exp << "\" but front is \""
                        << detail::q().front() << "\"; giving up after "
                        << detail::default_fails().load() << " waits\n";
                }
                else {
                    std::cerr << "[faircmd-hybrid][ERROR] " << (who ? who : "?")
                        << " expected \"" << exp << "\" but queue is empty; giving up\n";
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

    // FORGIVING: scans ahead; drops earlier tokens and consumes first match
    inline void WaitForCommandLoose(const char* who, const char* expected) {
        const std::string exp = expected ? expected : "";
        int remaining = detail::default_fails().load();
        std::unique_lock<std::mutex> lk(detail::mtx());

        while (true) {
            if (!detail::q().empty() && detail::q().front() == exp) {
                detail::q().pop_front(); detail::cv().notify_all(); return;
            }
            if (detail::erase_up_to_expected_unlocked(exp)) return;

            if (--remaining <= 0) {
                std::cerr << "[faircmd-hybrid][ERROR] " << (who ? who : "?")
                    << " expected \"" << exp << "\" but not present; giving up after "
                    << detail::default_fails().load() << " waits\n";
                throw std::runtime_error("faircmd_hybrid: token not present");
            }

            auto ns = detail::yield_ns().load();
            lk.unlock();
            if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
            else std::this_thread::yield();
            lk.lock();
        }
    }

    // Blocking pumper (reliable input everywhere). Splits lines into words.
    inline void start_stdin_pumper() {
        bool expected = false;
        if (!detail::pumper_running().compare_exchange_strong(expected, true)) return;

        detail::pumper_thread() = new std::thread([] {
            std::string line;
            while (detail::pumper_running().load(std::memory_order_relaxed)) {
                if (!std::getline(std::cin, line)) {
                    // If EOF or error, back off briefly and retry while running.
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::istringstream iss(line);
                std::string tok;
                std::lock_guard<std::mutex> lk(detail::mtx());
                while (iss >> tok) detail::push_unlocked(tok);
            }
            });
    }

    // Detach instead of join so we never hang on a blocked getline().
    // Safe for short-lived demos/tests that exit soon after stopping.
    inline void stop_stdin_pumper() {
        if (!detail::pumper_running().exchange(false)) return;
        if (auto* t = detail::pumper_thread()) {
            if (t->joinable()) t->detach();
            delete t;
            detail::pumper_thread() = nullptr;
        }
    }

} // namespace faircmd_hybrid
