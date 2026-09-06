#include "login_handoff.h"
#include <stdexcept>
#include <cstdio>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    using S = LoginHandoff::State;
    using namespace std::chrono_literals;
    try {
        LoginHandoff gate;
        auto now = LoginHandoff::Clock::now();
        check(gate.acknowledge(LoginHandoff::teleportId), "local acknowledgements must never escape");
        check(gate.state() == S::Idle, "unsolicited acknowledgement activated control");
        for (int i = 0; i < 10000; ++i) {
            gate.begin(now);
            gate.acknowledge(LoginHandoff::teleportId);
            check(gate.state() == S::Capturing, "early acknowledgement activated control");
            check(gate.snapshotSent(), "snapshot transition failed");
            check(!gate.acknowledge(42), "remote teleport acknowledgement was consumed");
            check(gate.state() == S::AwaitingAck, "remote acknowledgement activated control");
            check(!gate.expired(now + 19s) && gate.expired(now + 20s), "deadline incorrect");
            gate.acknowledge(LoginHandoff::teleportId);
            check(gate.state() == S::Active && !gate.expired(now + 30s), "handoff not active");
            gate.reset();
        }
        gate.begin(now);
        gate.fail();
        check(!gate.snapshotSent(), "failed snapshot accepted");
        gate.acknowledge(LoginHandoff::teleportId);
        check(gate.state() == S::Failed, "late acknowledgement revived failed snapshot");
        std::puts("PASS: initialization gating, local acknowledgements, deadlines and restart");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
