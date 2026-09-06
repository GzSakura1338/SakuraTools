#pragma once

#include <chrono>

// Accessed under the packet-dispatch lock. The deadline is checked by the controller.
class LoginHandoff {
public:
    static constexpr int teleportId = -2147483647;
    enum class State { Idle, Capturing, AwaitingAck, Active, Failed };
    using Clock = std::chrono::steady_clock;

    void reset() { state_ = State::Idle; }
    void begin(Clock::time_point now = Clock::now()) {
        state_ = State::Capturing;
        deadline_ = now + std::chrono::seconds(20);
    }
    bool snapshotSent() {
        if (state_ != State::Capturing) return false;
        state_ = State::AwaitingAck;
        return true;
    }
    bool acknowledge(int id) {
        if (id != teleportId) return false;
        if (state_ == State::AwaitingAck) state_ = State::Active;
        return true;
    }
    bool expired(Clock::time_point now = Clock::now()) const {
        return (state_ == State::Capturing || state_ == State::AwaitingAck) && now >= deadline_;
    }
    void fail() { state_ = State::Failed; }
    State state() const { return state_; }

private:
    State state_ = State::Idle;
    Clock::time_point deadline_{};
};
