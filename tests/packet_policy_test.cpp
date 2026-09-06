#include "packet_policy.h"
#include <cstdio>

int main() {
    struct Case {
        const char* name;
        bool activeA;
        bool activeB;
    };
    const Case cases[] = {
        {"net.minecraft.network.protocol.game.ServerboundMovePlayerPacket$PosRot", false, true},
        {"net.minecraft.network.protocol.game.ServerboundAcceptTeleportationPacket", false, true},
        {"net.minecraft.network.protocol.game.ServerboundKeepAlivePacket", true, false},
        {"net.minecraft.network.protocol.game.ServerboundCustomPayloadPacket", true, false},
        {"net.minecraft.network.protocol.game.ServerboundPongPacket", false, true},
        {"net.minecraft.network.protocol.login.ServerboundHelloPacket", true, false},
        {"net.minecraft.network.protocol.handshake.ClientIntentionPacket", true, false},
        {"net.minecraft.network.protocol.status.ServerboundStatusRequestPacket", true, false},
        {"example.UnknownPacket", true, false},
        {"", true, false},
    };
    for (const auto& item : cases) {
        if (!packet_policy::allowFromA(item.name, false) ||
            packet_policy::allowFromA(item.name, true) != item.activeA ||
            packet_policy::allowFromB(item.name) != item.activeB) {
            std::fprintf(stderr, "FAIL: routing ownership for %s\n", item.name);
            return 1;
        }
    }
    std::puts("PASS: A/B routing ownership before and after handoff");
    return 0;
}
