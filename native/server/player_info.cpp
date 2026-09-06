#include "internal.h"
#include "snapshot_jni.h"

namespace proxy_server {
namespace {
enum Action { GameMode = 0x04, Listed = 0x08, Latency = 0x10, DisplayName = 0x20 };
void check(JNIEnv* env) {
    if (env->ExceptionCheck())
        throw std::runtime_error("player-info JNI operation failed");
}
class PacketBuffer {
  public:
    explicit PacketBuffer(JNIEnv* env) : env_(env) {
        const auto& r = server.refs;
        bytes_ = env->CallStaticObjectMethod(r.unpooledCls, r.unpooledBufferMid);
        check(env);
        if (!bytes_)
            throw std::runtime_error("player-info buffer unavailable");
    }
    jobject wrap() {
        auto result = env_->NewObject(server.refs.friendlyBufCls, server.refs.friendlyBufCtor, bytes_);
        check(env_);
        if (!result)
            throw std::runtime_error("player-info buffer wrapper unavailable");
        return result;
    }
    ~PacketBuffer() {
        jthrowable error = env_->ExceptionOccurred();
        if (error)
            env_->ExceptionClear();
        env_->CallBooleanMethod(bytes_, server.refs.byteBufReleaseMid);
        LogAndClearException(env_, "player-info/buffer.release");
        env_->DeleteLocalRef(bytes_);
        if (error) {
            env_->Throw(error);
            env_->DeleteLocalRef(error);
        }
    }

  private:
    JNIEnv* env_;
    jobject bytes_;
};
} // namespace

bool buildPlayerInfoMirror(JNIEnv* env, jobject packet, jobject& output) {
    output = nullptr;
    const auto& r = server.refs;
    if (!r.playerInfoUpdatePacketCls || !env->IsInstanceOf(packet, r.playerInfoUpdatePacketCls))
        return true;
    if (!server.aPlayer.ready || !server.aPlayer.uuid || !server.bPlayer.ready || !server.bPlayer.uuid)
        return true;
    if (env->PushLocalFrame(32) != JNI_OK)
        return false;
    try {
        if (!r.playerInfoUpdatePacketWriteMid || !r.playerInfoUpdatePacketBufCtor || !r.piuEntriesField ||
            !r.piEntryProfileIdMid || !r.listSizeMid || !r.listGetMid || !r.uuidGetMsbMid || !r.uuidGetLsbMid ||
            !r.unpooledCls || !r.unpooledBufferMid || !r.friendlyBufCls || !r.friendlyBufCtor || !r.byteBufReleaseMid ||
            !r.byteBufGetByteMid || !r.fbbWriteByteMid || !r.fbbWriteVarIntMid || !r.fbbWriteUUIDMid ||
            !r.fbbWriteBooleanMid)
            throw std::runtime_error("player-info required bindings unavailable");
        jint bits;
        {
            PacketBuffer storage(env);
            jobject buffer = storage.wrap();
            env->CallVoidMethod(packet, r.playerInfoUpdatePacketWriteMid, buffer);
            check(env);
            bits = env->CallByteMethod(buffer, r.byteBufGetByteMid, (jint)0) & 0xff;
            check(env);
        }
        // Only advertise fields whose complete read/write path is available.
        int actions = bits & Listed;
        if (r.piEntryGameModeMid && r.gameTypeGetIdMid)
            actions |= bits & GameMode;
        if (r.piEntryLatencyMid)
            actions |= bits & Latency;
        if (r.piEntryDisplayNameMid && r.fbbWriteComponentMid)
            actions |= bits & DisplayName;
        jobject entries = env->GetObjectField(packet, r.piuEntriesField);
        check(env);
        jint size = env->CallIntMethod(entries, r.listSizeMid);
        check(env);
        jobject entry = nullptr;
        jlong aMsb = env->CallLongMethod(server.aPlayer.uuid, r.uuidGetMsbMid);
        check(env);
        jlong aLsb = env->CallLongMethod(server.aPlayer.uuid, r.uuidGetLsbMid);
        check(env);
        for (jint i = 0; i < size && !entry; ++i) {
            jobject candidate = env->CallObjectMethod(entries, r.listGetMid, i);
            check(env);
            jobject uuid = env->CallObjectMethod(candidate, r.piEntryProfileIdMid);
            check(env);
            jlong msb = env->CallLongMethod(uuid, r.uuidGetMsbMid);
            check(env);
            jlong lsb = env->CallLongMethod(uuid, r.uuidGetLsbMid);
            check(env);
            if (msb == aMsb && lsb == aLsb)
                entry = env->NewLocalRef(candidate);
            env->DeleteLocalRef(uuid);
            env->DeleteLocalRef(candidate);
        }
        jobject result = nullptr;
        if (entry && actions) {
            jint mode = 0, latency = 0;
            jobject display = nullptr;
            if (actions & GameMode) {
                jobject gameMode = env->CallObjectMethod(entry, r.piEntryGameModeMid);
                check(env);
                if (!gameMode)
                    throw std::runtime_error("player-info game mode unavailable");
                mode = env->CallIntMethod(gameMode, r.gameTypeGetIdMid);
                check(env);
            }
            if (actions & Latency) {
                latency = env->CallIntMethod(entry, r.piEntryLatencyMid);
                check(env);
            }
            if (actions & DisplayName) {
                display = env->CallObjectMethod(entry, r.piEntryDisplayNameMid);
                check(env);
            }
            PacketBuffer storage(env);
            jobject buffer = storage.wrap();
            auto write = [&](jmethodID method, auto value) {
                env->CallObjectMethod(buffer, method, value);
                check(env);
            };
            write(r.fbbWriteByteMid, (jint)actions);
            write(r.fbbWriteVarIntMid, (jint)1);
            write(r.fbbWriteUUIDMid, server.bPlayer.uuid);
            if (actions & GameMode)
                write(r.fbbWriteVarIntMid, mode);
            if (actions & Listed)
                write(r.fbbWriteBooleanMid, (jboolean)JNI_FALSE);
            if (actions & Latency)
                write(r.fbbWriteVarIntMid, latency);
            if (actions & DisplayName) {
                write(r.fbbWriteBooleanMid, (jboolean)(display != nullptr));
                if (display)
                    write(r.fbbWriteComponentMid, display);
            }
            result = env->NewObject(r.playerInfoUpdatePacketCls, r.playerInfoUpdatePacketBufCtor, buffer);
            check(env);
            if (!result)
                throw std::runtime_error("player-info packet unavailable");
        }
        output = env->PopLocalFrame(result);
        return true;
    } catch (const std::exception& error) {
        LogTo("player-info: %s", error.what());
        LogAndClearException(env, "player-info/build");
        env->PopLocalFrame(nullptr);
        return false;
    }
}
} // namespace proxy_server
