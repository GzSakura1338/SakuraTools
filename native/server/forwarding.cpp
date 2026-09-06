#include "internal.h"
#include "packet_policy.h"
#include "relay_handler.h"
#include "status_response.h"

namespace proxy_server {

void routeToA(JNIEnv* env, jobject packet) {
    jobject target;
    {
        std::lock_guard<std::mutex> l(server.targetMutex);
        target = server.targetConnection ? env->NewLocalRef(server.targetConnection) : nullptr;
    }
    if (!target)
        return;
    if (!server.refs.connectionSendMid) {
        env->DeleteLocalRef(target);
        return;
    }

    RelayFilter_MarkBypass(env, packet);
    env->CallVoidMethod(target, server.refs.connectionSendMid, packet);
    if (env->ExceptionCheck())
        LogAndClearException(env, "routeToA");
    env->DeleteLocalRef(target);
}


void writeClientPacket(JNIEnv* env, jobject ch, jobject packet) {

    if (server.refs.customPayloadPacketCls && env->IsInstanceOf(packet, server.refs.customPayloadPacketCls)) {
        static std::atomic<int> dropped{0};
        int n = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n & 0x3FF) == 0)
            LogTo("ForwardToB: skipping ClientboundCustomPayloadPacket (mod channel, count=%d)", n);
        return;
    }
    if (!sendGamePacket(env, ch, packet)) return;
    jobject mirror = nullptr;
    if (!buildPlayerInfoMirror(env, packet, mirror)) closeBChannel(env, ch);
    else if (mirror) {
        writePacket(env, ch, mirror);
        env->DeleteLocalRef(mirror);
    }
}

bool forwardBundleExpanded(JNIEnv* env, jobject ch, jobject packet) {
    if (!server.refs.bundlePacketCls || !server.refs.bundleSubPacketsMid || !server.refs.iterableIteratorMid ||
        !server.refs.iteratorHasNextMid || !server.refs.iteratorNextMid)
        return false;
    if (!env->IsInstanceOf(packet, server.refs.bundlePacketCls))
        return false;

    jobject iterable = env->CallObjectMethod(packet, server.refs.bundleSubPacketsMid);
    if (env->ExceptionCheck() || !iterable) {
        LogAndClearException(env, "bundle.subPackets");
        return true;
    }
    jobject it = env->CallObjectMethod(iterable, server.refs.iterableIteratorMid);
    env->DeleteLocalRef(iterable);
    if (env->ExceptionCheck() || !it) {
        LogAndClearException(env, "bundle.iterator");
        return true;
    }

    int forwarded = 0, nulls = 0;
    while (true) {
        jboolean more = env->CallBooleanMethod(it, server.refs.iteratorHasNextMid);
        if (env->ExceptionCheck()) {
            LogAndClearException(env, "bundle.iter.hasNext");
            break;
        }
        if (!more)
            break;

        jobject sub = env->CallObjectMethod(it, server.refs.iteratorNextMid);
        if (env->ExceptionCheck()) {
            LogAndClearException(env, "bundle.iter.next");
            break;
        }
        if (!sub) {
            ++nulls;
            continue;
        }

        if (!forwardBundleExpanded(env, ch, sub)) {
            writeClientPacket(env, ch, sub);
        }
        env->DeleteLocalRef(sub);
        ++forwarded;
    }
    env->DeleteLocalRef(it);
    if (nulls > 0)
        LogTo("bundle: forwarded=%d, null-subs=%d (skipped)", forwarded, nulls);
    return true;
}

void handleClientPacket(JNIEnv* env, jobject ctx, jobject msg) {
    jobject ch = env->CallObjectMethod(ctx, server.refs.contextChannelMid);
    if (!ch || env->ExceptionCheck())
        return;
    std::string cls = JniClassName(env, msg);
    ClientState state = server.clientState.load(std::memory_order_acquire);
    if (state == ClientState::AwaitHandshake || state == ClientState::AwaitLogin)
        LogTo("BServer: RX %s (state=%d)", cls.c_str(), (int)state);

    if (server.refs.intentPacketCls && env->IsInstanceOf(msg, server.refs.intentPacketCls)) {
        jobject nextProto = server.refs.protoLogin;
        if (server.refs.intentionPacketIntentFid) {
            jobject intent = env->GetObjectField(msg, server.refs.intentionPacketIntentFid);
            if (intent) {
                if (env->IsSameObject(intent, server.refs.protoStatus))
                    nextProto = server.refs.protoStatus;
                else
                    nextProto = server.refs.protoLogin;
                env->DeleteLocalRef(intent);
            }
        }
        setProtocolState(env, ch, nextProto);
        if (nextProto != server.refs.protoStatus) {
            bool occupied = false;
            {
                std::lock_guard<std::mutex> l(server.clientMutex);
                occupied = server.clientChannel && !env->IsSameObject(ch, server.clientChannel);
                if (!occupied) {
                    if (!server.clientChannel)
                        server.clientChannel = env->NewGlobalRef(ch);
                    if (!server.clientChannel)
                        return;
                    server.clientState.store(ClientState::AwaitLogin, std::memory_order_release);
                }
            }
            if (occupied) {
                env->CallObjectMethod(ch, server.refs.channelCloseMid);
                return;
            }
        }
        LogTo("BServer: intention → %s", nextProto == server.refs.protoStatus ? "STATUS" : "LOGIN");
        return;
    }

    if (server.refs.statusRequestPacketCls && env->IsInstanceOf(msg, server.refs.statusRequestPacketCls)) {
        jobject response = server.refs.statusClassLoader
                               ? BuildStatusResponse(env, server.refs.statusClassLoader, currentServerMotd(env))
                               : nullptr;
        if (!response && server.refs.statusResponse)
            response = env->NewLocalRef(server.refs.statusResponse);
        if (!response) {
            LogTo("STATUS: closing query because response initialization failed");
            env->CallObjectMethod(ch, server.refs.channelCloseMid);
            return;
        }
        writePacket(env, ch, response);
        if (env->ExceptionCheck())
            LogAndClearException(env, "STATUS/write response");
        else
            LogTo("STATUS: replied MOTD '%s'", kLanMotd);
        return;
    }

    if (server.refs.pingRequestPacketCls && env->IsInstanceOf(msg, server.refs.pingRequestPacketCls)) {
        jlong t = 0;
        if (server.refs.pingRequestPacketTimeFid)
            t = env->GetLongField(msg, server.refs.pingRequestPacketTimeFid);
        if (server.refs.pongResponsePacketCtor) {
            jobject pong = env->NewObject(server.refs.pongResponsePacketCls, server.refs.pongResponsePacketCtor, t);
            if (pong && !env->ExceptionCheck())
                writePacket(env, ch, pong);
            LogTo("BServer: replied Pong(%lld)", (long long)t);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> l(server.clientMutex);
        if (!server.clientChannel || !env->IsSameObject(ch, server.clientChannel))
            return;
    }
    if (state == ClientState::AwaitLogin && server.refs.helloPacketCls &&
        env->IsInstanceOf(msg, server.refs.helloPacketCls)) {
        completeLogin(env, msg);
        return;
    }

    std::lock_guard<std::recursive_mutex> dispatch(server.dispatchMutex);
    if (cls == "net.minecraft.network.protocol.game.ServerboundAcceptTeleportationPacket" &&
        server.refs.teleportAckId) {
        jint id = env->CallIntMethod(msg, server.refs.teleportAckId);
        if (env->ExceptionCheck())
            return;
        if (server.handoff.acknowledge(id)) {
            if (server.clientState.load() == ClientState::AwaitReady &&
                server.handoff.state() == LoginHandoff::State::Active) {
                server.clientState.store(ClientState::Play, std::memory_order_release);
                LogTo("mid-login: B acknowledged world snapshot; control handed to B");
            }
            return;
        }
    }

    if (server.clientState.load(std::memory_order_acquire) == ClientState::Play) {
        if (packet_policy::allowFromB(cls)) {
            routeToA(env, msg);
        }
    }
}

} // namespace proxy_server

using namespace proxy_server;

void BServer_ForwardToB(JNIEnv* env, jobject packet) {
    if (!packet)
        return;
    std::lock_guard<std::recursive_mutex> dispatch(server.dispatchMutex);
    ClientState state = server.clientState.load(std::memory_order_acquire);
    if (server.stopping.load() ||
        (state != ClientState::Play && state != ClientState::Syncing && state != ClientState::AwaitReady))
        return;

    std::string cls = JniClassName(env, packet);
    if (!packet_policy::isGamePacket(cls))
        return;
    if (state == ClientState::Syncing) {
        if (server.handoff.state() != LoginHandoff::State::Capturing)
            return;
        jobject ref = server.pendingPackets.size() < kMaxPendingPackets ? env->NewGlobalRef(packet) : nullptr;
        if (ref)
            server.pendingPackets.push_back(ref);
        else {
            server.handoff.fail();
            clearPendingPackets(env);
            LogTo("mid-login: live packet buffer exhausted; snapshot cancelled");
        }
        return;
    }

    jobject ch;
    {
        std::lock_guard<std::mutex> l(server.clientMutex);
        ch = server.clientChannel ? env->NewLocalRef(server.clientChannel) : nullptr;
    }
    if (!ch)
        return;

    if (!forwardBundleExpanded(env, ch, packet)) {
        writeClientPacket(env, ch, packet);
    }

    env->DeleteLocalRef(ch);
}
