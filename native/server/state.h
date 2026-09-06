#pragma once

#include "bindings.h"
#include "login_handoff.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace proxy_server {

constexpr const char* kInitSuper = "io/netty/channel/ChannelInitializer";
constexpr const char* kHandlerSuper = "io/netty/channel/ChannelInboundHandlerAdapter";
constexpr const char* kLanMotd = "Sakura Tools";
constexpr jint kProxyPort = 25565;

constexpr const char* kInitChannelDesc = "(Lio/netty/channel/Channel;)V";
constexpr const char* kChannelReadDesc = "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
constexpr const char* kChannelActiveDesc = "(Lio/netty/channel/ChannelHandlerContext;)V";
constexpr const char* kChannelInactiveDesc = "(Lio/netty/channel/ChannelHandlerContext;)V";

enum class ClientState { AwaitHandshake, AwaitLogin, Play, Syncing, AwaitReady };

struct PlayerIdentity {
    jobject uuid = nullptr;
    jstring name = nullptr;
    unsigned char uuidBytes[16] = {};
    bool ready = false;
};

struct ServerState {
    JavaBindings refs;
    bool bindingsReady = false;

    // Controller thread owns listener startup and shutdown.
    bool bound = false;
    jobject listener = nullptr;
    jobject eventLoop = nullptr;
    jobject lanPinger = nullptr;
    std::atomic_bool stopping{true};
    std::atomic_bool midSession{false};

    // Lock order: dispatchMutex, then clientMutex or targetMutex.
    // Never hold these locks while waiting for Minecraft's main thread.
    std::recursive_mutex dispatchMutex;
    LoginHandoff handoff;
    std::vector<jobject> pendingPackets; // Owned global refs, released on drain/stop.
    PlayerIdentity aPlayer;
    PlayerIdentity bPlayer;

    std::mutex clientMutex;
    jobject clientChannel = nullptr; // Owned global ref.
    std::atomic<ClientState> clientState{ClientState::AwaitHandshake};
    std::mutex targetMutex;
    jobject targetConnection = nullptr; // Owned global ref.

    std::mutex childrenMutex;
    std::vector<jweak> children; // Weak refs: status queries must not retain channels.

    // Main-thread gate for injection before A connects to a server.
    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool clientConnected = false;
    bool gateCancelled = false;
    unsigned gateTasks = 0;
};

inline constexpr size_t kMaxPendingPackets = 8192;
extern ServerState server;

} // namespace proxy_server
