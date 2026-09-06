#include "internal.h"
#include "snapshot_jni.h"

namespace proxy_server {
bool cachePacketWriter(JNIEnv* env, jobject loader) {
    try {
        SnapshotLocalFrame frame(env);
        SnapshotJni j(env, loader);
        PacketWriterBindings refs;
        const char* channel = "io.netty.channel.Channel";
        refs.write = j.method(channel, "write", "write", "(Ljava/lang/Object;)Lio/netty/channel/ChannelFuture;");
        refs.writeAndFlush =
            j.method(channel, "writeAndFlush", "writeAndFlush", "(Ljava/lang/Object;)Lio/netty/channel/ChannelFuture;");
        refs.close = j.method(channel, "close", "close", "()Lio/netty/channel/ChannelFuture;");
        refs.isOpen = j.method(channel, "isOpen", "isOpen", "()Z");
        refs.addListener =
            j.method("io.netty.channel.ChannelFuture", "addListener", "addListener",
                     "(Lio/netty/util/concurrent/GenericFutureListener;)Lio/netty/channel/ChannelFuture;");
        refs.isDone = j.method("io.netty.util.concurrent.Future", "isDone", "isDone", "()Z");
        refs.isSuccess = j.method("io.netty.util.concurrent.Future", "isSuccess", "isSuccess", "()Z");
        jclass listener = j.type("io.netty.channel.ChannelFutureListener");
        auto field = env->GetStaticFieldID(listener, "CLOSE_ON_FAILURE", "Lio/netty/channel/ChannelFutureListener;");
        j.check();
        jobject value = env->GetStaticObjectField(listener, field);
        refs.closeOnFailure = j.required(env->NewGlobalRef(j.required(value)));
        if (server.refs.writer.closeOnFailure)
            env->DeleteGlobalRef(server.refs.writer.closeOnFailure);
        server.refs.writer = refs;
        return true;
    } catch (const std::exception& error) {
        LogTo("bindings: packet writer unavailable: %s", error.what());
        LogAndClearException(env, "bindings/packet writer");
        return false;
    }
}

void closeBChannel(JNIEnv* env, jobject channel) {
    LogAndClearException(env, "send/close");
    if (!channel)
        return;
    jmethodID close = server.refs.writer.close ? server.refs.writer.close : server.refs.channelCloseMid;
    if (!close)
        return;
    jobject future = env->CallObjectMethod(channel, close);
    if (future)
        env->DeleteLocalRef(future);
    LogAndClearException(env, "send/channel.close");
}

bool writePacket(JNIEnv* env, jobject channel, jobject packet, bool flush) {
    const auto& r = server.refs.writer;
    if (!channel || !packet || !r.write || !r.writeAndFlush || !r.isOpen || !r.addListener || !r.closeOnFailure ||
        !r.isDone || !r.isSuccess || env->ExceptionCheck()) {
        closeBChannel(env, channel);
        return false;
    }
    if (env->PushLocalFrame(8) != JNI_OK) {
        closeBChannel(env, channel);
        return false;
    }
    bool ok = false;
    do {
        jboolean open = env->CallBooleanMethod(channel, r.isOpen);
        if (env->ExceptionCheck() || !open)
            break;
        jobject future = env->CallObjectMethod(channel, flush ? r.writeAndFlush : r.write, packet);
        if (env->ExceptionCheck() || !future)
            break;
        env->CallObjectMethod(future, r.addListener, r.closeOnFailure);
        if (env->ExceptionCheck())
            break;
        jboolean done = env->CallBooleanMethod(future, r.isDone);
        if (env->ExceptionCheck())
            break;
        ok = !done || env->CallBooleanMethod(future, r.isSuccess);
        if (env->ExceptionCheck())
            ok = false;
    } while (false);
    if (!ok) {
        LogTo("send: packet submission failed; closing B");
        closeBChannel(env, channel);
    }
    env->PopLocalFrame(nullptr);
    return ok;
}

bool sendGamePacket(JNIEnv* env, jobject channel, jobject packet, bool flush) {
    std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
    bool team = server.refs.setPlayerTeamPacketCls && env->IsInstanceOf(packet, server.refs.setPlayerTeamPacketCls);
    bool login = server.refs.playLoginPacketCls && env->IsInstanceOf(packet, server.refs.playLoginPacketCls);
    if (!team && !login)
        return writePacket(env, channel, packet, flush);
    auto previous = server.teams;
    jobject outgoing = nullptr;
    bool ok = prepareTeamPacket(env, packet, outgoing);
    if (ok && outgoing)
        ok = writePacket(env, channel, outgoing, flush);
    if (outgoing)
        env->DeleteLocalRef(outgoing);
    if (!ok) {
        server.teams = std::move(previous);
        closeBChannel(env, channel);
    }
    // State describes accepted writes. A later failure closes this session through
    // CLOSE_ON_FAILURE; the next login rebuilds all memberships from its snapshot.
    return ok;
}
} // namespace proxy_server
