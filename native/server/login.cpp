#include "internal.h"
#include "world_snapshot.h"

namespace proxy_server {

bool beginSnapshot(JNIEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
    if (server.stopping.load() || server.handoff.state() != LoginHandoff::State::Capturing)
        return JNI_FALSE;
    // A's earlier packet tasks have run on the render thread. The snapshot includes
    // them; packets dispatched after this boundary remain buffered as live deltas.
    clearPendingPackets(env);
    return JNI_TRUE;
}

void writeToB(JNIEnv* env, jobject packet) {
    jobject ch;
    {
        std::lock_guard<std::mutex> l(server.clientMutex);
        ch = server.clientChannel ? env->NewLocalRef(server.clientChannel) : nullptr;
    }
    if (!ch)
        return;
    env->CallObjectMethod(ch, server.refs.channelWriteAndFlushMid, packet);
    if (env->ExceptionCheck())
        LogAndClearException(env, "writeAndFlush");
    env->DeleteLocalRef(ch);
}

bool uuidToBytes(JNIEnv* env, jobject uuid, unsigned char out[16]) {
    if (!uuid || !server.refs.uuidGetMsbMid || !server.refs.uuidGetLsbMid)
        return false;
    jlong msb = env->CallLongMethod(uuid, server.refs.uuidGetMsbMid);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jlong lsb = env->CallLongMethod(uuid, server.refs.uuidGetLsbMid);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    for (int i = 0; i < 8; ++i)
        out[i] = (unsigned char)((msb >> ((7 - i) * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i)
        out[8 + i] = (unsigned char)((lsb >> ((7 - i) * 8)) & 0xFF);
    return true;
}

void ensureARealUuid(JNIEnv* env) {
    if (server.aPlayer.ready && server.aPlayer.name)
        return;
    if (!server.refs.minecraftCls || !server.refs.mcGetInstanceMid || !server.refs.mcGetUserMid)
        return;
    jobject mc = env->CallStaticObjectMethod(server.refs.minecraftCls, server.refs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject user = env->CallObjectMethod(mc, server.refs.mcGetUserMid);
    env->DeleteLocalRef(mc);
    if (!user || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    if (!server.aPlayer.ready && server.refs.userGetProfileIdMid) {
        jobject uuid = env->CallObjectMethod(user, server.refs.userGetProfileIdMid);
        if (uuid && !env->ExceptionCheck()) {
            unsigned char bytes[16];
            if (uuidToBytes(env, uuid, bytes)) {
                server.aPlayer.uuid = env->NewGlobalRef(uuid);
                std::memcpy(server.aPlayer.uuidBytes, bytes, 16);
                server.aPlayer.ready = true;
                LogTo("mirror: cached A's real UUID for tab-list mirroring");
            }
            env->DeleteLocalRef(uuid);
        } else if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!server.aPlayer.name && server.refs.userGetGameProfileMid && server.refs.gameProfileGetNameMid) {
        jobject profile = env->CallObjectMethod(user, server.refs.userGetGameProfileMid);
        if (profile && !env->ExceptionCheck()) {
            jstring nm = (jstring)env->CallObjectMethod(profile, server.refs.gameProfileGetNameMid);
            if (nm && !env->ExceptionCheck()) {
                server.aPlayer.name = (jstring)env->NewGlobalRef(nm);
                const char* utf = env->GetStringUTFChars(nm, nullptr);
                LogTo("team-mirror: cached A's name = '%s'", utf ? utf : "?");
                if (utf)
                    env->ReleaseStringUTFChars(nm, utf);
                env->DeleteLocalRef(nm);
            } else if (env->ExceptionCheck())
                env->ExceptionClear();
            env->DeleteLocalRef(profile);
        } else if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    env->DeleteLocalRef(user);
}

void sendSelfInfoToB(JNIEnv* env, jobject offlineUuid, jstring name) {
    if (!server.refs.minecraftCls || !server.refs.mcGetInstanceMid || !server.refs.mcGetProfilePropsMid ||
        !server.refs.friendlyBufCls || !server.refs.friendlyBufCtor || !server.refs.fbbWriteByteMid ||
        !server.refs.fbbWriteBooleanMid || !server.refs.fbbWriteVarIntMid || !server.refs.fbbWriteUUIDMid ||
        !server.refs.fbbWriteUtfMid || !server.refs.fbbWriteGpPropsMid || !server.refs.unpooledCls ||
        !server.refs.unpooledBufferMid || !server.refs.playerInfoUpdatePacketCls ||
        !server.refs.playerInfoUpdatePacketBufCtor) {
        LogTo("self-info: missing refs, skipping push");
        return;
    }
    jobject mc = env->CallStaticObjectMethod(server.refs.minecraftCls, server.refs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) {
        env->ExceptionClear();
        LogTo("self-info: no Minecraft");
        return;
    }
    jobject props = env->CallObjectMethod(mc, server.refs.mcGetProfilePropsMid);
    env->DeleteLocalRef(mc);
    if (!props || env->ExceptionCheck()) {
        env->ExceptionClear();
        LogTo("self-info: no profile props");
        return;
    }

    jobject bb = env->CallStaticObjectMethod(server.refs.unpooledCls, server.refs.unpooledBufferMid);
    if (!bb || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(props);
        return;
    }
    jobject buf = env->NewObject(server.refs.friendlyBufCls, server.refs.friendlyBufCtor, bb);
    env->DeleteLocalRef(bb);
    if (!buf || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(props);
        return;
    }

    env->CallObjectMethod(buf, server.refs.fbbWriteByteMid, (jint)0x1D);
    env->CallObjectMethod(buf, server.refs.fbbWriteByteMid, (jint)0x01);
    env->CallObjectMethod(buf, server.refs.fbbWriteUUIDMid, offlineUuid);

    env->CallObjectMethod(buf, server.refs.fbbWriteUtfMid, name, (jint)16);
    env->CallVoidMethod(buf, server.refs.fbbWriteGpPropsMid, props);

    env->CallObjectMethod(buf, server.refs.fbbWriteVarIntMid, (jint)0);

    // Keep B's profile for its skin, but show only the server's players in TAB.
    env->CallObjectMethod(buf, server.refs.fbbWriteBooleanMid, (jboolean)JNI_FALSE);

    env->CallObjectMethod(buf, server.refs.fbbWriteVarIntMid, (jint)0);
    env->DeleteLocalRef(props);
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "self-info: buffer build");
        env->DeleteLocalRef(buf);
        return;
    }

    jobject pkt = env->NewObject(server.refs.playerInfoUpdatePacketCls, server.refs.playerInfoUpdatePacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) {
        LogAndClearException(env, "self-info: packet ctor");
        return;
    }
    writeToB(env, pkt);
    env->DeleteLocalRef(pkt);
    LogTo("self-info: pushed B's own player-info to B (unlisted, A's skin)");
}

bool reconstructAndSendLoginToB(JNIEnv* env, jobject ch) {
    jobject mc = env->CallStaticObjectMethod(server.refs.minecraftCls, server.refs.mcGetInstanceMid);
    jobject target;
    {
        std::lock_guard<std::mutex> lock(server.targetMutex);
        target = server.targetConnection ? env->NewLocalRef(server.targetConnection) : nullptr;
    }
    if (!mc || !target || env->ExceptionCheck()) {
        if (mc)
            env->DeleteLocalRef(mc);
        if (target)
            env->DeleteLocalRef(target);
        LogAndClearException(env, "snapshot/target");
        return false;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
        clearPendingPackets(env);
        server.handoff.begin();
        server.clientState.store(ClientState::Syncing, std::memory_order_release);
    }
    auto started = std::chrono::steady_clock::now();
    std::vector<jobject> packets;
    bool captured = CaptureWorldSnapshot(env, mc, target, beginSnapshot, packets);
    env->DeleteLocalRef(mc);
    env->DeleteLocalRef(target);
    if (!captured || env->ExceptionCheck()) {
        LogAndClearException(env, "snapshot/capture");
        for (jobject packet : packets)
            env->DeleteGlobalRef(packet);
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
    bool ok = !server.stopping.load() && server.handoff.state() == LoginHandoff::State::Capturing;
    const size_t count = packets.size();
    for (jobject packet : packets) {
        if (ok) {
            jobject outgoing = nullptr;
            ok = prepareTeamPacket(env, packet, outgoing);
            if (ok && outgoing) ok = WriteSnapshotPacket(env, ch, outgoing);
            if (outgoing) env->DeleteLocalRef(outgoing);
        }
        env->DeleteGlobalRef(packet);
    }
    if (!ok) {
        LogAndClearException(env, "snapshot/write");
        clearPendingPackets(env);
        server.handoff.fail();
        return false;
    }
    LogTo("mid-login: snapshot queued packets=%d elapsed=%lldms", (int)count,
          (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
              .count());
    return true;
}

void completeLogin(JNIEnv* env, jobject hello) {

    if (!server.refs.helloPacketNameFid) {
        LogTo("login: no hello.name field");
        return;
    }
    jstring jname = (jstring)env->GetObjectField(hello, server.refs.helloPacketNameFid);
    if (!jname) {
        LogTo("login: hello.name is null");
        return;
    }
    const char* utfName = env->GetStringUTFChars(jname, nullptr);
    LogTo("login: B says name='%s'", utfName ? utfName : "?");

    std::string composite = std::string("OfflinePlayer:") + (utfName ? utfName : "");
    if (utfName)
        env->ReleaseStringUTFChars(jname, utfName);
    jbyteArray arr = env->NewByteArray((jsize)composite.size());
    env->SetByteArrayRegion(arr, 0, (jsize)composite.size(), reinterpret_cast<const jbyte*>(composite.data()));
    jobject uuid = env->CallStaticObjectMethod(server.refs.uuidCls, server.refs.uuidNameUuidFromBytesMid, arr);
    env->DeleteLocalRef(arr);
    if (!uuid || env->ExceptionCheck()) {
        LogAndClearException(env, "login: nameUUIDFromBytes");
        env->DeleteLocalRef(jname);
        return;
    }
    LogTo("login: computed offline UUID");

    if (!server.refs.gameProfileCtor) {
        LogTo("login: no GameProfile ctor");
        env->DeleteLocalRef(uuid);
        env->DeleteLocalRef(jname);
        return;
    }
    jobject profile = env->NewObject(server.refs.gameProfileCls, server.refs.gameProfileCtor, uuid, jname);
    if (!profile || env->ExceptionCheck()) {
        LogAndClearException(env, "login: GameProfile ctor");
        env->DeleteLocalRef(uuid);
        env->DeleteLocalRef(jname);
        return;
    }

    if (!server.refs.loginFinishedPacketCtor) {
        LogTo("login: no LoginFinished ctor");
        return;
    }
    jobject lfp = env->NewObject(server.refs.loginFinishedPacketCls, server.refs.loginFinishedPacketCtor, profile);
    env->DeleteLocalRef(profile);
    if (!lfp || env->ExceptionCheck()) {
        LogAndClearException(env, "login: LoginFinished ctor");
        return;
    }

    writeToB(env, lfp);
    env->DeleteLocalRef(lfp);
    LogTo("login: sent ClientboundGameProfilePacket to B");

    {
        std::lock_guard<std::recursive_mutex> dispatch(server.dispatchMutex);
        server.teams = TeamState{};
        if (server.bPlayer.name) env->DeleteGlobalRef(server.bPlayer.name);
        server.bPlayer.name = static_cast<jstring>(env->NewGlobalRef(jname));
        ensureARealUuid(env);
    }

    jobject ch;
    {
        std::lock_guard<std::mutex> l(server.clientMutex);
        ch = server.clientChannel ? env->NewLocalRef(server.clientChannel) : nullptr;
    }
    if (ch) {
        setProtocolState(env, ch, server.refs.protoPlay);
        LogTo("login: protocol switched to PLAY");

        if (server.midSession.load(std::memory_order_acquire)) {
            LogTo("login: mid-session — rebuilding login for B from A's state");
            if (!reconstructAndSendLoginToB(env, ch)) {
                LogTo("mid-login: reconstruction failed; closing B instead of entering empty PLAY");
                env->CallObjectMethod(ch, server.refs.channelCloseMid);
                if (env->ExceptionCheck())
                    LogAndClearException(env, "mid-login/close");
                env->DeleteLocalRef(ch);
                env->DeleteLocalRef(uuid);
                env->DeleteLocalRef(jname);
                return;
            }
        }
        env->DeleteLocalRef(ch);
    }

    sendSelfInfoToB(env, uuid, jname);

    unsigned char bBytes[16];
    if (uuidToBytes(env, uuid, bBytes)) {
        std::memcpy(server.bPlayer.uuidBytes, bBytes, 16);
        server.bPlayer.ready = true;
    }

    if (server.bPlayer.uuid)
        env->DeleteGlobalRef(server.bPlayer.uuid);
    server.bPlayer.uuid = env->NewGlobalRef(uuid);


    env->DeleteLocalRef(uuid);
    env->DeleteLocalRef(jname);

    if (server.midSession.load(std::memory_order_acquire)) {
        std::lock_guard<std::recursive_mutex> dispatch(server.dispatchMutex);
        if (server.stopping.load())
            return;
        if (!server.handoff.snapshotSent()) {
            server.handoff.fail();
            BServer_CheckLoginTimeout(env);
            return;
        }
        server.clientState.store(ClientState::AwaitReady, std::memory_order_release);
        std::vector<jobject> pending;
        pending.swap(server.pendingPackets);
        for (jobject packet : pending) {
            BServer_ForwardToB(env, packet);
            env->DeleteGlobalRef(packet);
        }
        jobject channel;
        {
            std::lock_guard<std::mutex> lock(server.clientMutex);
            channel = server.clientChannel ? env->NewLocalRef(server.clientChannel) : nullptr;
        }
        if (channel) {
            jclass type = env->GetObjectClass(channel);
            jmethodID flush = env->GetMethodID(type, "flush", "()Lio/netty/channel/Channel;");
            if (flush)
                env->CallObjectMethod(channel, flush);
            env->DeleteLocalRef(type);
            env->DeleteLocalRef(channel);
        }
        LogTo("mid-login: snapshot sent; waiting for B initialization acknowledgement");
        return;
    }
    server.clientState.store(ClientState::Play, std::memory_order_release);
    {
        std::lock_guard<std::mutex> l(server.gateMutex);
        server.clientConnected = true;
    }
    server.gateChanged.notify_all();
    LogTo("login: B in PLAY (empty world); released A's Render thread — A now "
          "connects and its live join stream feeds B");
}

} // namespace proxy_server
