#include "runtime_gate.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        DeleteKeyEdge key;
        check(!key.update(true, true), "held DEL on startup must not trigger");
        check(!key.update(false, true), "release must not trigger");
        check(!key.update(true, false), "background key must not trigger");
        check(!key.update(true, true), "focus change while held must not trigger");
        key.update(false, true);
        check(key.update(true, true), "foreground DEL must trigger");
        check(!key.update(true, true), "held DEL must not repeat");

        RuntimeGate gate;
        check(!gate.enter(), "initial gate must be closed");
        for (int cycle = 0; cycle < 10000; ++cycle) {
            gate.start();
            check(gate.enter(), "reactivation must admit work");
            check(!gate.stopFor(0ms), "active callback must prevent cleanup");
            check(!gate.enter(), "stopping must reject new work");
            gate.leave();
            check(gate.stopFor(0ms), "cleanup retry must succeed after callback exits");
            check(gate.stopFor(0ms), "repeated stop must be idempotent");
        }

        gate.start();
        check(gate.enter(), "callback entry failed");
        std::promise<void> started;
        auto stopping = std::async(std::launch::async, [&] {
            started.set_value();
            return gate.stopFor(5s);
        });
        started.get_future().wait();
        while (gate.enabled()) std::this_thread::yield();
        check(!gate.enter(), "concurrent stop must close admission");
        gate.leave();
        check(stopping.get(), "last callback must wake the stopping thread");
        std::puts("PASS: DEL edges, 10000 restart cycles, timeout/retry and concurrent drain");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
