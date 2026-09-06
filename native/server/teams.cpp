#include "internal.h"
#include <stdexcept>

namespace proxy_server {
namespace {
void check(JNIEnv* env) {
    if (env->ExceptionCheck())
        throw std::runtime_error("team JNI operation failed");
}
std::u16string name(JNIEnv* env, jstring value) {
    if (!value)
        return {};
    const jsize length = env->GetStringLength(value);
    const jchar* chars = env->GetStringChars(value, nullptr);
    check(env);
    if (!chars)
        throw std::runtime_error("team string unavailable");
    std::u16string result(reinterpret_cast<const char16_t*>(chars), length);
    env->ReleaseStringChars(value, chars);
    return result;
}
} // namespace

bool prepareTeamPacket(JNIEnv* env, jobject packet, jobject& output) {
    output = nullptr;
    if (!server.refs.setPlayerTeamPacketCls || !env->IsInstanceOf(packet, server.refs.setPlayerTeamPacketCls)) {
        if (server.refs.playLoginPacketCls && env->IsInstanceOf(packet, server.refs.playLoginPacketCls))
            server.teams = TeamState{};
        output = env->NewLocalRef(packet);
        return output != nullptr;
    }
    if (env->PushLocalFrame(32) < 0)
        return false;
    try {
        const auto& refs = server.refs;
        if (!refs.setPlayerTeamCtor || !refs.setPlayerTeamParametersFid || !refs.setPlayerTeamMethodFid ||
            !refs.setPlayerTeamNameFid || !refs.setPlayerTeamPlayersFid)
            throw std::runtime_error("team bindings unavailable");
        jint method = env->GetIntField(packet, refs.setPlayerTeamMethodFid);
        auto team = static_cast<jstring>(env->GetObjectField(packet, refs.setPlayerTeamNameFid));
        jobject players = env->GetObjectField(packet, refs.setPlayerTeamPlayersFid);
        jobject parameters = env->GetObjectField(packet, refs.setPlayerTeamParametersFid);
        check(env);
        jclass collection = env->FindClass("java/util/Collection");
        jmethodID toArray = env->GetMethodID(collection, "toArray", "()[Ljava/lang/Object;");
        check(env);
        auto array = static_cast<jobjectArray>(env->CallObjectMethod(players, toArray));
        check(env);
        std::vector<TeamState::Name> names;
        for (jsize i = 0, size = env->GetArrayLength(array); i < size; ++i) {
            auto value = static_cast<jstring>(env->GetObjectArrayElement(array, i));
            names.push_back(name(env, value));
            env->DeleteLocalRef(value);
        }
        auto next = server.teams;
        auto update =
            next.apply(name(env, team), method, names, name(env, server.aPlayer.name), name(env, server.bPlayer.name));
        if (!update.send) {
            server.teams = std::move(next);
            env->PopLocalFrame(nullptr);
            return true;
        }
        jclass listClass = env->FindClass("java/util/ArrayList");
        jmethodID ctor = env->GetMethodID(listClass, "<init>", "()V");
        jmethodID add = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
        check(env);
        jobject list = env->NewObject(listClass, ctor);
        check(env);
        for (const auto& player : update.players) {
            jstring value =
                env->NewString(reinterpret_cast<const jchar*>(player.data()), static_cast<jsize>(player.size()));
            check(env);
            env->CallBooleanMethod(list, add, value);
            env->DeleteLocalRef(value);
            check(env);
        }
        jobject result =
            env->NewObject(refs.setPlayerTeamPacketCls, refs.setPlayerTeamCtor, team, method, parameters, list);
        check(env);
        if (!result)
            throw std::runtime_error("team packet unavailable");
        server.teams = std::move(next);
        output = env->PopLocalFrame(result);
        return output != nullptr;
    } catch (const std::exception& error) {
        LogTo("teams: %s", error.what());
        LogAndClearException(env, "teams/prepare");
        env->PopLocalFrame(nullptr);
        return false;
    }
}
} // namespace proxy_server
