/*------------------------------------------------------------------------------
  faircmd_hybrid.hpp — Hybrid stdin + scripted queue (human+machine)

  Demo-friendly variant for Windows consoles:
  - Uses blocking std::getline() (typing works reliably in PowerShell/ConHost).
  - stop_stdin_pumper() detaches the thread (no hang on join).
    NOTE: Appropriate for demos/tests that end soon after stopping.
          For long-lived apps, prefer a fully non-blocking pumper.

  Public API:
    void reset() noexcept;
    void set_default_fails(int) noexcept;
    void set_yield_sleep(std::chrono::milliseconds) noexcept;
    void preload(std::initializer_list<const char*>);
    void push(std::string);

    // Waits for an expected token with a "strict" rule (must be at the front).
    void WaitForCommand(const char* who, const char* expected);

    // Waits for an expected token "loosely" (may appear anywhere; skipped tokens are discarded).
    void WaitForCommandLoose(const char* who, const char* expected);

    // Optional helper for debug visibility of queued tokens:
    void dump_pending_to_stderr();

    // Stdin pumper:
    void start_stdin_pumper();
    void stop_stdin_pumper();

   ---------------------------------------------------------------------------
   NEW: Minimal recording helpers (only tokens actually CONSUMED by WaitFor*):
    std::vector<std::string> snapshot_consumed();
    void clear_recording();
    std::string emit_cpp(const std::string& mode = "preload",
                         const std::string& var  = "script");
   ---------------------------------------------------------------------------

  Usage sketch:
      using namespace faircmd_hybrid;
      reset();
      preload({"go","promote","go","stop"});
      start_stdin_pumper();   // optional; stdin input merges with scripted
      WaitForCommand("worker", "go");
      WaitForCommand("main", "promote");
      WaitForCommandLoose("worker", "go");  // loose match ok
      WaitForCommand("worker", "stop");
      stop_stdin_pumper();

      std::string cpp = emit_cpp("preload");   // paste into a unit test
      std::cerr << cpp;

------------------------------------------------------------------------------*/

#pragma once
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace faircmd_hybrid {

    namespace detail {
        // Core singletons
        inline std::mutex& mtx() { static std::mutex m; return m; }
        inline std::condition_variable& cv() { static std::condition_variable v; return v; }
        inline std::deque<std::string>& q() { static std::deque<std::string> d; return d; }
        inline std::atomic<int>& default_fails() { static std::atomic<int> v{ 1000 }; return v; }
        inline std::atomic<long long>& yield_ns() { static std::atomic<long long> v{ 0 }; return v; }

        // Pumper state
        inline std::atomic<bool>& pumper_running() { static std::atomic<bool> b{ false }; return b; }
        inline std::thread*& pumper_thread() { static std::thread* t = nullptr; return t; }

        // --- NEW: Recorder of tokens actually consumed by WaitFor* -----------
        inline std::vector<std::string>& consumed() { static std::vector<std::string> v; return v; }
        inline void record_consumed_unlocked(const std::string& s) { consumed().push_back(s); }

        // Tokenization helper for the pumper (split by whitespace)
        inline void tokenize_and_push_unlocked(const std::string& line) {
            std::istringstream iss(line);
            std::string tok;
            while (iss >> tok) {
                q().push_back(tok);
            }
            cv().notify_all();
        }

        // Helper used by loose wait: erase up to expected; record only the match
        inline bool erase_up_to_expected_unlocked_record(const std::string& exp) {
            auto& Q = q();
            for (auto it = Q.begin(); it != Q.end(); ++it) {
                if (*it == exp) {
                    const std::string matched = *it;
                    Q.erase(Q.begin(), std::next(it));
                    record_consumed_unlocked(matched);
                    cv().notify_all();
                    return true;
                }
            }
            return false;
        }
    } // namespace detail

    // Basic controls -----------------------------------------------------------

    inline void reset() noexcept {
        std::lock_guard<std::mutex> lk(detail::mtx());
        detail::q().clear();
        detail::consumed().clear(); // NEW: also clear recording
        detail::cv().notify_all();
    }

    inline void set_default_fails(int n) noexcept {
        detail::default_fails().store(n);
    }

    inline void set_yield_sleep(std::chrono::milliseconds ms) noexcept {
        detail::yield_ns().store(static_cast<long long>(ms.count()) * 1'000'000LL);
    }

    inline void preload(std::initializer_list<const char*> items) {
        std::lock_guard<std::mutex> lk(detail::mtx());
        for (const char* s : items) {
            detail::q().emplace_back(s ? s : "");
        }
        detail::cv().notify_all();
    }

    inline void push(std::string s) {
        std::lock_guard<std::mutex> lk(detail::mtx());
        detail::q().push_back(std::move(s));
        detail::cv().notify_all();
    }

    // Debug helper -------------------------------------------------------------

    inline void dump_pending_to_stderr() {
        std::lock_guard<std::mutex> lk(detail::mtx());
        std::cerr << "[faircmd] pending:";
        for (auto& s : detail::q()) std::cerr << " \"" << s << "\"";
        std::cerr << "\n";
    }

    // Wait APIs ---------------------------------------------------------------

    // Strict: expected must be at the front.
    inline void WaitForCommand(const char* who, const char* expected) {
        const std::string exp = expected ? expected : "";
        int remaining = detail::default_fails().load();

        std::unique_lock<std::mutex> lk(detail::mtx());
        while (true) {
            // immediate front match?
            if (!detail::q().empty() && detail::q().front() == exp) {
                const std::string matched = detail::q().front(); // NEW
                detail::q().pop_front();
                detail::record_consumed_unlocked(matched);        // NEW
                detail::cv().notify_all();
                return;
            }

            // no tokens yet: wait or spin-yield
            if (detail::q().empty()) {
                if (detail::yield_ns().load() > 0) {
                    lk.unlock();
                    std::this_thread::sleep_for(std::chrono::nanoseconds(detail::yield_ns().load()));
                    lk.lock();
                }
                else {
                    detail::cv().wait(lk);
                }
                continue;
            }

            // front exists but not the expected token
            if (--remaining <= 0) {
                std::ostringstream oss;
                oss << "WaitForCommand(" << (who ? who : "?") << "): expected \"" << exp
                    << "\" but got \"" << detail::q().front() << "\"";
                throw std::runtime_error(oss.str());
            }

            // Small backoff & retry
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lk.lock();
        }
    }

    // Loose: expected may appear anywhere; skip/discard tokens until found.
    // Only the matched token is recorded; skips are not recorded.
    inline void WaitForCommandLoose(const char* who, const char* expected) {
        const std::string exp = expected ? expected : "";
        int remaining = detail::default_fails().load();

        std::unique_lock<std::mutex> lk(detail::mtx());
        while (true) {
            // front match fast-path
            if (!detail::q().empty() && detail::q().front() == exp) {
                const std::string matched = detail::q().front(); // NEW
                detail::q().pop_front();
                detail::record_consumed_unlocked(matched);        // NEW
                detail::cv().notify_all();
                return;
            }

            // If queue has items, try to find 'exp' somewhere and erase up to it.
            if (!detail::q().empty() && detail::erase_up_to_expected_unlocked_record(exp)) {
                return;
            }

            // Wait for more input if empty
            if (detail::q().empty()) {
                if (detail::yield_ns().load() > 0) {
                    lk.unlock();
                    std::this_thread::sleep_for(std::chrono::nanoseconds(detail::yield_ns().load()));
                    lk.lock();
                }
                else {
                    detail::cv().wait(lk);
                }
                continue;
            }

            // Didn't find it in a non-empty queue (rare race): retry a few times
            if (--remaining <= 0) {
                std::ostringstream oss;
                oss << "WaitForCommandLoose(" << (who ? who : "?") << "): expected \"" << exp
                    << "\" but not found after retries";
                throw std::runtime_error(oss.str());
            }

            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lk.lock();
        }
    }

    // Stdin pumper ------------------------------------------------------------

    inline void start_stdin_pumper() {
        bool expected = false;
        if (!detail::pumper_running().compare_exchange_strong(expected, true)) {
            return; // already running
        }

        // Create thread (captured by pointer singleton)
        if (detail::pumper_thread()) {
            // Shouldn't happen, but clean up if it does
            if (detail::pumper_thread()->joinable()) detail::pumper_thread()->detach();
            delete detail::pumper_thread();
            detail::pumper_thread() = nullptr;
        }

        detail::pumper_thread() = new std::thread([] {
            // Blocking getline loop; tokenize into whitespace-separated tokens.
            std::string line;
            while (detail::pumper_running().load()) {
                if (!std::getline(std::cin, line)) {
                    // EOF or stream closed: pause briefly to avoid hot spin
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::lock_guard<std::mutex> lk(detail::mtx());
                detail::tokenize_and_push_unlocked(line);
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

    // --- Recording API (minimal; no behavior change to waits) ----------------

    // Get the tokens actually matched by WaitFor* so far.
    inline std::vector<std::string> snapshot_consumed() {
        std::lock_guard<std::mutex> lk(detail::mtx());
        return detail::consumed();
    }

    // Clear the recorded, matched tokens.
    inline void clear_recording() {
        std::lock_guard<std::mutex> lk(detail::mtx());
        detail::consumed().clear();
    }

    // Emit pasteable C++ that recreates the same command stream.
    //   mode == "preload" (default):  faircmd_hybrid::preload({ ... });
    //   mode == "vector":  std::vector<std::string> <var> = { ... };
    inline std::string emit_cpp(const std::string& mode = "preload",
        const std::string& var = "script") {
        auto esc = [](const std::string& s) {
            std::ostringstream o;
            o << '"';
            for (unsigned char c : s) {
                switch (c) {
                case '\\': o << "\\\\"; break;
                case '"':  o << "\\\""; break;
                case '\n': o << "\\n";  break;
                case '\r': o << "\\r";  break;
                case '\t': o << "\\t";  break;
                default:
                    if (c < 0x20 || c == 0x7F) { // control chars
                        o << "\\x" << std::hex << std::setw(2) << std::setfill('0') << int(c);
                    }
                    else {
                        o << c;
                    }
                }
            }
            o << '"';
            return o.str();
            };

        std::lock_guard<std::mutex> lk(detail::mtx());
        const auto& H = detail::consumed();
        std::ostringstream out;

        if (mode == "vector") {
            out << "std::vector<std::string> " << var << " = {";
            for (size_t i = 0; i < H.size(); ++i) {
                if (i) out << ", ";
                out << esc(H[i]);
            }
            out << "};\n";
            out << "// Usage: faircmd_hybrid::preload({ /* copy from " << var << " if desired */ });\n";
            return out.str();
        }

        // default: preload
        out << "faircmd_hybrid::preload({";
        for (size_t i = 0; i < H.size(); ++i) {
            if (i) out << ", ";
            out << esc(H[i]);
        }
        out << "});\n";
        return out.str();
    }

} // namespace faircmd_hybrid
