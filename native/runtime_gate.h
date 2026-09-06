#pragma once

#include <condition_variable>
#include <chrono>
#include <mutex>

// Stop admits no new work, then waits for callbacks already using session state.
class RuntimeGate {
public:
    bool enter() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_) return false;
        ++inFlight_;
        return true;
    }
    void leave() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (--inFlight_ == 0) drained_.notify_all();
    }
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = true;
    }
    void stop() {
        std::unique_lock<std::mutex> lock(mutex_);
        enabled_ = false;
        drained_.wait(lock, [&] { return inFlight_ == 0; });
    }
    bool stopFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        enabled_ = false;
        return drained_.wait_for(lock, timeout, [&] { return inFlight_ == 0; });
    }
    bool enabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return enabled_;
    }
private:
    mutable std::mutex mutex_;
    std::condition_variable drained_;
    bool enabled_ = false;
    unsigned inFlight_ = 0;
};

inline RuntimeGate g_runtimeGate;

class RuntimeCallback {
public:
    RuntimeCallback() : entered_(g_runtimeGate.enter()) {}
    ~RuntimeCallback() { if (entered_) g_runtimeGate.leave(); }
    RuntimeCallback(const RuntimeCallback&) = delete;
    RuntimeCallback& operator=(const RuntimeCallback&) = delete;
    explicit operator bool() const { return entered_; }
private:
    bool entered_;
};

class DeleteKeyEdge {
public:
    bool update(bool down, bool targetForeground) {
        bool pressed = down && !wasDown_ && targetForeground;
        wasDown_ = down;
        return pressed;
    }
private:
    bool wasDown_ = true;
};
