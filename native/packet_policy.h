#pragma once

#include <string_view>

// The remote connection belongs to A. Keep its heartbeat and mod handshake on A,
// while B supplies gameplay input after initialization has completed.
namespace packet_policy {
inline constexpr std::string_view gamePrefix = "net.minecraft.network.protocol.game.";

inline bool isGamePacket(std::string_view name) {
    return name.substr(0, gamePrefix.size()) == gamePrefix;
}

inline bool isOwnedByA(std::string_view name) {
    return name == "net.minecraft.network.protocol.game.ServerboundKeepAlivePacket" ||
           name == "net.minecraft.network.protocol.game.ServerboundCustomPayloadPacket";
}

inline bool allowFromA(std::string_view name, bool bActive) {
    return !bActive || !isGamePacket(name) || isOwnedByA(name);
}

inline bool allowFromB(std::string_view name) {
    return isGamePacket(name) && !isOwnedByA(name);
}
} // namespace packet_policy
