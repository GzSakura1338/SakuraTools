#include "internal.h"

namespace proxy_server {
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

bool initializeBIdentity(JNIEnv* env, jobject uuid, jstring name) {
    std::lock_guard<std::recursive_mutex> lock(server.dispatchMutex);
    PlayerIdentity next;
    if (!name || !uuidToBytes(env, uuid, next.uuidBytes))
        return false;
    next.uuid = env->NewGlobalRef(uuid);
    if (next.uuid && !env->ExceptionCheck()) next.name = static_cast<jstring>(env->NewGlobalRef(name));
    if (!next.uuid || !next.name || env->ExceptionCheck()) {
        if (next.uuid)
            env->DeleteGlobalRef(next.uuid);
        if (next.name)
            env->DeleteGlobalRef(next.name);
        LogAndClearException(env, "login/identity");
        return false;
    }
    next.ready = true;
    if (server.bPlayer.uuid)
        env->DeleteGlobalRef(server.bPlayer.uuid);
    if (server.bPlayer.name)
        env->DeleteGlobalRef(server.bPlayer.name);
    server.bPlayer = next;
    server.teams = TeamState{};
    return true;
}
} // namespace proxy_server
