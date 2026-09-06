#include "team_state.h"
#include <cstdio>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    try {
        TeamState state;
        auto apply = [&](const char16_t* team, int method, std::vector<TeamState::Name> players) {
            return state.apply(team, method, players, u"A", u"B");
        };
        auto initial = apply(u"red", 0, {u"A", u"other"});
        require(initial.players == std::vector<TeamState::Name>{u"A", u"other", u"B"}, "snapshot must include B");
        require(apply(u"red", 2, {}).send, "team parameters must pass through");
        auto leave = apply(u"red", 4, {u"A", u"B", u"A"});
        require(leave.players == std::vector<TeamState::Name>{u"A", u"B"}, "remove each member once");
        require(!apply(u"red", 4, {u"A", u"B"}).send, "duplicate removal must be suppressed");
        apply(u"red", 3, {u"A"});
        apply(u"blue", 0, {u"A"});
        require(!apply(u"red", 4, {u"A"}).send, "stale removal after move must be suppressed");
        require(apply(u"blue", 4, {u"A"}).players.size() == 2, "new team removal must succeed");
        apply(u"red", 3, {u"A"});
        apply(u"blue", 3, {u"B"});
        require(apply(u"red", 4, {u"A"}).players.size() == 2, "remote B name must not move local alias");
        require(apply(u"red", 1, {}).send, "team deletion must pass through");
        require(!apply(u"red", 4, {u"other"}).send, "deleted team has no members");
        require(!apply(u"missing", 2, {}).send, "unknown team update must be suppressed");
        apply(u"red", 0, {u"A"});
        require(apply(u"red", 4, {u"A"}).players.size() == 2, "team recreation must work");
        state = TeamState{};
        require(!apply(u"red", 4, {u"A"}).send, "new session must discard memberships");
        state.apply(u"same", 0, {u"A", u"A"}, u"A", u"A");
        require(state.apply(u"same", 4, {u"A", u"A"}, u"A", u"A").players.size() == 1,
                "same identity must not duplicate removal");
        std::puts("PASS: snapshot, moves, duplicate removal, same names, deletion and reconnect");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
