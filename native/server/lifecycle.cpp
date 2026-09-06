#include "internal.h"
#include "relay_handler.h"

namespace proxy_server {

void clearPendingPackets(JNIEnv* env) {
    for (jobject packet : server.pendingPackets)
        env->DeleteGlobalRef(packet);
    server.pendingPackets.clear();
}

void closeARemoteConnection(JNIEnv* env) {
    if (!server.refs.connectionChannelFid || !server.refs.channelCloseMid)
        return;
    jobject conn;
    {
        std::lock_guard<std::mutex> l(server.targetMutex);
        conn = server.targetConnection ? env->NewLocalRef(server.targetConnection) : nullptr;
    }
    if (!conn) {
        LogTo("[A-CLOSE] no target A connection cached; nothing to close");
        return;
    }
    jobject aChannel = env->GetObjectField(conn, server.refs.connectionChannelFid);
    env->DeleteLocalRef(conn);
    if (!aChannel) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        LogTo("[A-CLOSE] Connection.channel is null (not yet channelActive?)");
        return;
    }
    jobject future = env->CallObjectMethod(aChannel, server.refs.channelCloseMid);
    if (env->ExceptionCheck())
        LogAndClearException(env, "[A-CLOSE] channel.close");
    else
        LogTo("[A-CLOSE] A's netty channel to remote server closed directly");
    if (future)
        env->DeleteLocalRef(future);
    env->DeleteLocalRef(aChannel);

    std::lock_guard<std::mutex> l(server.targetMutex);
    if (server.targetConnection) {
        env->DeleteGlobalRef(server.targetConnection);
        server.targetConnection = nullptr;
    }
}

bool waitNettyFuture(JNIEnv* env, jobject future) {
    if (!future || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jclass cls = env->GetObjectClass(future);
    jmethodID await = env->GetMethodID(cls, "awaitUninterruptibly", "(J)Z");
    jmethodID success = env->GetMethodID(cls, "isSuccess", "()Z");
    bool ok = false;
    if (await && success && !env->ExceptionCheck()) {
        jboolean done = env->CallBooleanMethod(future, await, (jlong)5000);
        if (!env->ExceptionCheck() && done)
            ok = env->CallBooleanMethod(future, success);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(future);
    return ok;
}

} // namespace proxy_server

using namespace proxy_server;

bool InstallBServer(JNIEnv* env) {
    if (server.bound)
        return true;
    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader)
        return false;
    if (!server.bindingsReady) {
        server.bindingsReady =
            defineInitClass(env, mcLoader) && defineHandlerClass(env, mcLoader) && cacheJavaRefs(env, mcLoader);
    }
    {
        std::lock_guard<std::mutex> lock(server.gateMutex);
        server.gateCancelled = false;
        server.clientConnected = false;
    }
    server.midSession.store(false);
    server.stopping.store(false);
    bool ok = server.bindingsReady && bindServer(env, mcLoader);
    if (ok) {
        server.bound = true;
        startLanAnnouncement(env, mcLoader);
    }
    env->DeleteGlobalRef(mcLoader);
    return ok;
}

bool BServer_IsBActive() {
    return !server.stopping.load() && server.clientState.load(std::memory_order_acquire) == ClientState::Play;
}

std::recursive_mutex& BServer_DispatchMutex() {
    return server.dispatchMutex;
}

void BServer_CheckLoginTimeout(JNIEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
    if (!server.handoff.expired() && server.handoff.state() != LoginHandoff::State::Failed)
        return;
    server.handoff.fail();
    clearPendingPackets(env);
    jobject ch;
    {
        std::lock_guard<std::mutex> guard(server.clientMutex);
        ch = server.clientChannel ? env->NewLocalRef(server.clientChannel) : nullptr;
    }
    if (ch) {
        jobject future = env->CallObjectMethod(ch, server.refs.channelCloseMid);
        if (future)
            env->DeleteLocalRef(future);
        env->DeleteLocalRef(ch);
    }
    LogAndClearException(env, "snapshot/timeout close");
    server.handoff.reset();
    LogTo("mid-login: snapshot failed or timed out; B closed and A retains control");
}

void BServer_RequestStop() {
    server.stopping.store(true);
    {
        std::lock_guard<std::mutex> lock(server.gateMutex);
        server.clientConnected = false;
        server.gateCancelled = true;
    }
    server.gateChanged.notify_all();
}

bool StopBServer(JNIEnv* env) {
    BServer_RequestStop();
    bool ok = true;
    auto close = [&](jobject channel) -> bool {
        if (!channel)
            return true;
        jobject future = env->CallObjectMethod(channel, server.refs.channelCloseMid);
        bool closed = waitNettyFuture(env, future);
        if (!closed)
            ok = false;
        return closed;
    };
    close(server.listener);
    {
        std::lock_guard<std::mutex> lock(server.childrenMutex);
        std::vector<jweak> pending;
        for (jweak weak : server.children) {
            jobject channel = env->NewLocalRef(weak);
            bool closed = !env->ExceptionCheck();
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                ok = false;
            }
            if (channel) {
                closed = close(channel);
                env->DeleteLocalRef(channel);
            }
            if (closed)
                env->DeleteWeakGlobalRef(weak);
            else
                pending.push_back(weak);
        }
        server.children.swap(pending);
    }
    if (server.lanPinger) {
        jclass cls = env->GetObjectClass(server.lanPinger);
        jmethodID interrupt = env->GetMethodID(cls, "interrupt", "()V");
        jmethodID join = env->GetMethodID(cls, "join", "(J)V");
        jmethodID alive = env->GetMethodID(cls, "isAlive", "()Z");
        if (interrupt && join && alive && !env->ExceptionCheck()) {
            env->CallVoidMethod(server.lanPinger, interrupt);
            if (!env->ExceptionCheck())
                env->CallVoidMethod(server.lanPinger, join, (jlong)5000);
            if (!env->ExceptionCheck() && env->CallBooleanMethod(server.lanPinger, alive))
                ok = false;
        } else
            ok = false;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ok = false;
        }
        env->DeleteLocalRef(cls);
    }
    if (server.eventLoop) {
        jclass cls = env->GetObjectClass(server.eventLoop);
        jmethodID shutdown = env->GetMethodID(cls, "shutdownGracefully", "()Lio/netty/util/concurrent/Future;");
        if (shutdown && !env->ExceptionCheck()) {
            jobject future = env->CallObjectMethod(server.eventLoop, shutdown);
            if (!waitNettyFuture(env, future))
                ok = false;
        } else {
            env->ExceptionClear();
            ok = false;
        }
        env->DeleteLocalRef(cls);
    }
    {
        std::unique_lock<std::mutex> lock(server.gateMutex);
        if (!server.gateChanged.wait_for(lock, std::chrono::seconds(5), [] { return server.gateTasks == 0; }))
            ok = false;
    }
    if (!ok) {
        LogTo("STOP: cleanup incomplete; reactivation blocked until remaining tasks finish");
        return false;
    }
    auto release = [&](jobject& ref) {
        if (ref) {
            env->DeleteGlobalRef(ref);
            ref = nullptr;
        }
    };
    release(server.listener);
    release(server.eventLoop);
    release(server.lanPinger);
    {
        std::lock_guard<std::mutex> lock(server.clientMutex);
        release(server.clientChannel);
        server.clientState.store(ClientState::AwaitHandshake);
    }
    {
        std::lock_guard<std::mutex> lock(server.targetMutex);
        release(server.targetConnection);
    }
    release(server.aPlayer.uuid);
    release(server.bPlayer.uuid);
    if (server.aPlayer.name) {
        env->DeleteGlobalRef(server.aPlayer.name);
        server.aPlayer.name = nullptr;
    }
    if (server.bPlayer.name) {
        env->DeleteGlobalRef(server.bPlayer.name);
        server.bPlayer.name = nullptr;
    }
    server.aPlayer.ready = false;
    server.bPlayer.ready = false;
    server.bound = false;
    server.midSession.store(false);
    {
        std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
        clearPendingPackets(env);
        server.handoff.reset();
    }
    LogTo("STOP: B disconnected; listener, LAN announcement and event loops stopped; A retained");
    return true;
}

bool BServer_IsLoginIntention(JNIEnv* env, jobject packet) {
    if (!env || !packet)
        return false;
    if (!server.refs.intentPacketCls || !env->IsInstanceOf(packet, server.refs.intentPacketCls))
        return false;
    if (!server.refs.intentionPacketIntentFid || !server.refs.protoLogin)
        return false;
    jobject intent = env->GetObjectField(packet, server.refs.intentionPacketIntentFid);
    if (!intent)
        return false;
    bool isLogin = env->IsSameObject(intent, server.refs.protoLogin);
    env->DeleteLocalRef(intent);
    return isLogin;
}

bool BServer_WaitForBConnected(int timeoutMs) {
    std::unique_lock<std::mutex> lock(server.gateMutex);
    if (server.clientConnected)
        return true;
    if (timeoutMs < 0) {
        server.gateChanged.wait(lock, [] { return server.clientConnected || server.gateCancelled; });
        return server.clientConnected;
    }
    server.gateChanged.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                [] { return server.clientConnected || server.gateCancelled; });
    return server.clientConnected;
}

bool BServer_BlockAMainThreadUntilBConnected(JNIEnv* env) {
    if (!env || !g_jvmti)
        return false;
    if (BServer_WaitForBConnected(0))
        return true;

    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader) {
        LogTo("[MAIN-GATE] Minecraft ClassLoader unavailable");
        return false;
    }
    if (!defineMainGateClass(env, mcLoader)) {
        env->DeleteGlobalRef(mcLoader);
        return false;
    }

    jclass minecraftCls = LoadClassInLoader(env, mcLoader, "net.minecraft.client.Minecraft");
    env->DeleteGlobalRef(mcLoader);
    if (!minecraftCls) {
        LogTo("[MAIN-GATE] Minecraft class unavailable");
        return false;
    }

    jmethodID getInstanceMid = findMethodByDescriptor(minecraftCls, "()Lnet/minecraft/client/Minecraft;", true);
    jmethodID executeMid = env->GetMethodID(minecraftCls, "execute", "(Ljava/lang/Runnable;)V");
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!getInstanceMid || !executeMid) {
        LogTo("[MAIN-GATE] Minecraft getInstance/execute lookup failed");
        env->DeleteLocalRef(minecraftCls);
        return false;
    }

    jobject minecraft = env->CallStaticObjectMethod(minecraftCls, getInstanceMid);
    jobject gate = env->NewObject(server.refs.mainGateClass, server.refs.mainGateCtor);
    if (!minecraft || !gate || env->ExceptionCheck()) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        LogTo("[MAIN-GATE] failed to create Minecraft gate task");
        if (gate)
            env->DeleteLocalRef(gate);
        if (minecraft)
            env->DeleteLocalRef(minecraft);
        env->DeleteLocalRef(minecraftCls);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(server.gateMutex);
        if (server.gateCancelled) {
            env->DeleteLocalRef(gate);
            env->DeleteLocalRef(minecraft);
            env->DeleteLocalRef(minecraftCls);
            return false;
        }
        ++server.gateTasks;
    }
    env->CallVoidMethod(minecraft, executeMid, gate);
    bool ok = !env->ExceptionCheck();
    if (!ok) {
        env->ExceptionClear();
        std::lock_guard<std::mutex> lock(server.gateMutex);
        --server.gateTasks;
        server.gateChanged.notify_all();
    }
    LogTo("[MAIN-GATE] task %s on A Render thread", ok ? "queued" : "queue failed");

    env->DeleteLocalRef(gate);
    env->DeleteLocalRef(minecraft);
    env->DeleteLocalRef(minecraftCls);
    return ok;
}

void BServer_SetTargetConnection(JNIEnv* env, jobject connection) {
    std::lock_guard<std::mutex> l(server.targetMutex);
    if (server.targetConnection)
        env->DeleteGlobalRef(server.targetConnection);
    server.targetConnection = connection ? env->NewGlobalRef(connection) : nullptr;
    LogTo("BServer: target A connection = %p", (void*)server.targetConnection);
}

LiveConnectionState BServer_TryCaptureLiveConnection(JNIEnv* env) {
    if (!env)
        return LiveConnectionState::Failed;
    jobject loader = GetMinecraftClassLoader(env, g_jvmti);
    if (!loader)
        return LiveConnectionState::Failed;
    // Session-specific lookups must be retried even when the server refs are cached.
    bool ready = refreshMidSessionRefs(env, loader);
    env->DeleteGlobalRef(loader);
    if (!ready)
        return LiveConnectionState::Failed;
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "mid-session/refresh");
        return LiveConnectionState::Failed;
    }
    jobject mc = env->CallStaticObjectMethod(server.refs.minecraftCls, server.refs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) {
        LogAndClearException(env, "mid-session/Minecraft");
        return LiveConnectionState::Failed;
    }
    jobject cpl = env->CallObjectMethod(mc, server.refs.mcGetConnectionMid);
    env->DeleteLocalRef(mc);
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "mid-session/getConnection");
        return LiveConnectionState::Failed;
    }
    if (!cpl)
        return LiveConnectionState::NotConnected;
    jobject conn = env->CallObjectMethod(cpl, server.refs.cplGetConnectionMid);
    env->DeleteLocalRef(cpl);
    if (!conn || env->ExceptionCheck()) {
        LogAndClearException(env, "mid-session/listener connection");
        return LiveConnectionState::Failed;
    }

    jobject channel = env->GetObjectField(conn, server.refs.connectionChannelFid);
    if (!channel || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(conn);
        LogTo("mid-session: connection.channel null");
        return LiveConnectionState::Failed;
    }
    jobject pipeline = env->CallObjectMethod(channel, server.refs.channelPipelineMid);
    env->DeleteLocalRef(channel);
    if (!pipeline || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(conn);
        LogTo("mid-session: channel.pipeline() null");
        return LiveConnectionState::Failed;
    }
    bool attached = RelayHandler_AttachToPipelineObject(env, pipeline);
    env->DeleteLocalRef(pipeline);
    if (!attached) {
        env->DeleteLocalRef(conn);
        return LiveConnectionState::Failed;
    }
    BServer_SetTargetConnection(env, conn);
    env->DeleteLocalRef(conn);

    server.midSession.store(true, std::memory_order_release);
    LogTo("mid-session: captured A's live Connection + attached relay (A already in-game)");
    return LiveConnectionState::Captured;
}
