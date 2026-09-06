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

void mirrorPlayerInfoUpdateToB(JNIEnv* env, jobject ch, jobject packet) {
    if (!server.refs.playerInfoUpdatePacketCls || !server.refs.playerInfoUpdatePacketBufCtor ||
        !server.refs.playerInfoUpdatePacketWriteMid || !server.refs.friendlyBufCls || !server.refs.friendlyBufCtor ||
        !server.refs.unpooledCls || !server.refs.unpooledBufferMid || !server.refs.byteBufGetByteMid ||
        !server.refs.listSizeMid || !server.refs.listGetMid || !server.refs.piuEntriesMidA ||
        !server.refs.piEntryProfileIdMid || !server.refs.fbbWriteByteMid || !server.refs.fbbWriteVarIntMid ||
        !server.refs.fbbWriteUUIDMid || !server.refs.fbbWriteBooleanMid)
        return;
    if (!server.aPlayer.ready || !server.bPlayer.ready || !server.bPlayer.uuid)
        return;
    if (!env->IsInstanceOf(packet, server.refs.playerInfoUpdatePacketCls))
        return;

    jobject bb0 = env->CallStaticObjectMethod(server.refs.unpooledCls, server.refs.unpooledBufferMid);
    if (!bb0 || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject sbuf = env->NewObject(server.refs.friendlyBufCls, server.refs.friendlyBufCtor, bb0);
    env->DeleteLocalRef(bb0);
    if (!sbuf || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    env->CallVoidMethod(packet, server.refs.playerInfoUpdatePacketWriteMid, sbuf);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(sbuf);
        return;
    }
    jint bits = env->CallByteMethod(sbuf, server.refs.byteBufGetByteMid, (jint)0) & 0xFF;
    env->DeleteLocalRef(sbuf);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    bool hasGameMode = bits & 0x04;
    bool hasListed = bits & 0x08;
    bool hasLatency = bits & 0x10;
    bool hasDisplay = bits & 0x20;
    int outBits = (hasGameMode ? 0x04 : 0) | (hasListed ? 0x08 : 0) | (hasLatency ? 0x10 : 0) | (hasDisplay ? 0x20 : 0);

    jobject listA = env->CallObjectMethod(packet, server.refs.piuEntriesMidA);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        listA = nullptr;
    }
    jobject listB = server.refs.piuEntriesMidB ? env->CallObjectMethod(packet, server.refs.piuEntriesMidB) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        listB = nullptr;
    }
    jint sizeA = -1, sizeB = -1;
    if (listA) {
        sizeA = env->CallIntMethod(listA, server.refs.listSizeMid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            sizeA = -1;
        }
    }
    if (listB) {
        sizeB = env->CallIntMethod(listB, server.refs.listSizeMid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            sizeB = -1;
        }
    }
    jobject entries = (sizeB > sizeA) ? listB : listA;
    jint nEntries = (sizeB > sizeA) ? sizeB : sizeA;

    jobject aEntry = nullptr;
    for (jint i = 0; i < nEntries && !aEntry; ++i) {
        jobject e = env->CallObjectMethod(entries, server.refs.listGetMid, i);
        if (!e || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (e)
                env->DeleteLocalRef(e);
            continue;
        }
        jobject euuid = env->CallObjectMethod(e, server.refs.piEntryProfileIdMid);
        if (euuid && !env->ExceptionCheck()) {
            unsigned char eb[16];
            if (uuidToBytes(env, euuid, eb) && std::memcmp(eb, server.aPlayer.uuidBytes, 16) == 0)
                aEntry = env->NewLocalRef(e);
            env->DeleteLocalRef(euuid);
        } else if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(e);
    }
    if (listA)
        env->DeleteLocalRef(listA);
    if (listB)
        env->DeleteLocalRef(listB);
    if (!aEntry)
        return;

    if (outBits == 0) {
        env->DeleteLocalRef(aEntry);
        return;
    }

    jobject bb = env->CallStaticObjectMethod(server.refs.unpooledCls, server.refs.unpooledBufferMid);
    if (!bb || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(aEntry);
        return;
    }
    jobject buf = env->NewObject(server.refs.friendlyBufCls, server.refs.friendlyBufCtor, bb);
    env->DeleteLocalRef(bb);
    if (!buf || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(aEntry);
        return;
    }

    env->CallObjectMethod(buf, server.refs.fbbWriteByteMid, (jint)outBits);
    env->CallObjectMethod(buf, server.refs.fbbWriteVarIntMid, (jint)1);
    env->CallObjectMethod(buf, server.refs.fbbWriteUUIDMid, server.bPlayer.uuid);

    if (hasGameMode && server.refs.piEntryGameModeMid && server.refs.gameTypeGetIdMid) {
        jobject gm = env->CallObjectMethod(aEntry, server.refs.piEntryGameModeMid);
        jint id = 0;
        if (gm && !env->ExceptionCheck())
            id = env->CallIntMethod(gm, server.refs.gameTypeGetIdMid);
        if (gm)
            env->DeleteLocalRef(gm);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->CallObjectMethod(buf, server.refs.fbbWriteVarIntMid, id);
    }
    if (hasListed) {
        // Mirrored updates must never add B as a second visible TAB entry.
        env->CallObjectMethod(buf, server.refs.fbbWriteBooleanMid, (jboolean)JNI_FALSE);
    }
    if (hasLatency && server.refs.piEntryLatencyMid) {
        jint lat = env->CallIntMethod(aEntry, server.refs.piEntryLatencyMid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            lat = 0;
        }
        env->CallObjectMethod(buf, server.refs.fbbWriteVarIntMid, lat);
    }
    if (hasDisplay) {
        jobject dn = server.refs.piEntryDisplayNameMid
                         ? env->CallObjectMethod(aEntry, server.refs.piEntryDisplayNameMid)
                         : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            dn = nullptr;
        }

        if (dn && server.refs.fbbWriteComponentMid) {
            env->CallObjectMethod(buf, server.refs.fbbWriteBooleanMid, (jboolean)JNI_TRUE);
            env->CallObjectMethod(buf, server.refs.fbbWriteComponentMid, dn);
        } else {
            env->CallObjectMethod(buf, server.refs.fbbWriteBooleanMid, (jboolean)JNI_FALSE);
        }
        if (dn)
            env->DeleteLocalRef(dn);
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    env->DeleteLocalRef(aEntry);

    jobject pkt = env->NewObject(server.refs.playerInfoUpdatePacketCls, server.refs.playerInfoUpdatePacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) {
        LogAndClearException(env, "mirror: single-entry ctor");
        return;
    }
    env->CallObjectMethod(ch, server.refs.channelWriteAndFlushMid, pkt);
    if (env->ExceptionCheck())
        LogAndClearException(env, "mirror: writeAndFlush");
    env->DeleteLocalRef(pkt);
    LogTo("mirror: single-entry PlayerInfoUpdate to B (bits=0x%02x)", outBits);
}

void writeClientPacket(JNIEnv* env, jobject ch, jobject packet) {

    if (server.refs.customPayloadPacketCls && env->IsInstanceOf(packet, server.refs.customPayloadPacketCls)) {
        static std::atomic<int> dropped{0};
        int n = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n & 0x3FF) == 0)
            LogTo("ForwardToB: skipping ClientboundCustomPayloadPacket (mod channel, count=%d)", n);
        return;
    }
    jobject outgoing = nullptr;
    if (!prepareTeamPacket(env, packet, outgoing)) {
        LogAndClearException(env, "ForwardToB/teams");
        env->CallObjectMethod(ch, server.refs.channelCloseMid);
        LogAndClearException(env, "ForwardToB/close");
        return;
    }
    if (!outgoing) return;
    env->CallObjectMethod(ch, server.refs.channelWriteAndFlushMid, outgoing);
    env->DeleteLocalRef(outgoing);
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "ForwardToB/writeOne");
        env->CallObjectMethod(ch, server.refs.channelCloseMid);
        LogAndClearException(env, "ForwardToB/close");
        return;
    }
    mirrorPlayerInfoUpdateToB(env, ch, packet);
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
        env->CallObjectMethod(ch, server.refs.channelWriteAndFlushMid, response);
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
                env->CallObjectMethod(ch, server.refs.channelWriteAndFlushMid, pong);
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
