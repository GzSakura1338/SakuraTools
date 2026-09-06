#pragma once

#include "proxy.h"
#include "mapped_method.h"
#include <stdexcept>
#include <string>
#include <unordered_map>

// Lookup caches live for one snapshot. Global class refs survive per-item local frames.
class SnapshotJni {
public:
    SnapshotJni(JNIEnv* env, jobject loader) : env(env), loader_(loader) {}
    ~SnapshotJni() { for (auto& entry : classes_) env->DeleteGlobalRef(entry.second); }
    SnapshotJni(const SnapshotJni&) = delete;
    SnapshotJni& operator=(const SnapshotJni&) = delete;

    void check() { if (env->ExceptionCheck()) throw std::runtime_error("JNI snapshot operation failed"); }
    jobject required(jobject object) {
        check();
        if (!object) throw std::runtime_error("Required snapshot object is missing");
        return object;
    }
    jclass type(const char* name) {
        auto it = classes_.find(name);
        if (it != classes_.end()) return it->second;
        jclass local = LoadClassInLoader(env, loader_, name);
        required(local);
        auto global = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        required(global);
        classes_.emplace(name, global);
        return global;
    }
    jmethodID method(const char* owner, const char* name, const char* mapped,
                     const char* desc, bool isStatic = false) {
        std::string key = std::string(owner) + ":" + name + desc + (isStatic ? ":S" : "");
        auto it = methods_.find(key);
        if (it != methods_.end()) return it->second;
        jmethodID result = FindMappedMethod(env, type(owner), name, mapped, desc, isStatic);
        check();
        if (!result) throw std::runtime_error("Snapshot method missing: " + key);
        methods_.emplace(key, result);
        return result;
    }
    jfieldID field(const char* owner, const char* name, const char* mapped, const char* desc) {
        jclass cls = type(owner);
        jfieldID result = env->GetFieldID(cls, name, desc);
        if (!result && env->ExceptionCheck()) {
            jthrowable error = env->ExceptionOccurred();
            env->ExceptionClear();
            bool missing = env->IsInstanceOf(error, type("java.lang.NoSuchFieldError"));
            if (missing) result = env->GetFieldID(cls, mapped, desc);
            else env->Throw(error);
            env->DeleteLocalRef(error);
        }
        check();
        if (!result) throw std::runtime_error(std::string("Snapshot field missing: ") + owner + "." + name);
        return result;
    }
    template<class... Args> jobject object(jobject target, const char* owner, const char* name,
                                           const char* mapped, const char* desc, Args... args) {
        required(target);
        auto id = method(owner, name, mapped, desc);
        jobject result = env->CallObjectMethod(target, id, args...);
        check();
        return result;
    }
    template<class... Args> jobject stat(const char* owner, const char* name, const char* mapped,
                                         const char* desc, Args... args) {
        auto id = method(owner, name, mapped, desc, true);
        jobject result = env->CallStaticObjectMethod(type(owner), id, args...);
        check();
        return result;
    }
    template<class... Args> jobject make(const char* owner, const char* desc, Args... args) {
        auto ctor = method(owner, "<init>", "<init>", desc);
        return required(env->NewObject(type(owner), ctor, args...));
    }
    jobject get(jobject target, const char* owner, const char* name, const char* mapped, const char* desc) {
        required(target);
        auto id = field(owner, name, mapped, desc);
        jobject result = env->GetObjectField(target, id);
        check();
        return result;
    }
    jint getInt(jobject target, const char* owner, const char* name, const char* mapped) {
        required(target);
        auto id = field(owner, name, mapped, "I");
        jint result = env->GetIntField(target, id);
        check();
        return result;
    }
#define SNAPSHOT_SCALAR(NAME, TYPE, JNI_CALL) \
    template<class... Args> TYPE NAME(jobject target, const char* owner, const char* name, \
                                     const char* mapped, const char* desc, Args... args) { \
        required(target); \
        auto id = method(owner, name, mapped, desc); \
        TYPE result = env->JNI_CALL(target, id, args...); \
        check(); \
        return result; \
    }
    SNAPSHOT_SCALAR(integer, jint, CallIntMethod)
    SNAPSHOT_SCALAR(real, jdouble, CallDoubleMethod)
    SNAPSHOT_SCALAR(decimal, jfloat, CallFloatMethod)
    SNAPSHOT_SCALAR(longValue, jlong, CallLongMethod)
    SNAPSHOT_SCALAR(boolean, jboolean, CallBooleanMethod)
#undef SNAPSHOT_SCALAR
    JNIEnv* env;
private:
    jobject loader_;
    std::unordered_map<std::string, jclass> classes_;
    std::unordered_map<std::string, jmethodID> methods_;
};

class SnapshotLocalFrame {
public:
    explicit SnapshotLocalFrame(JNIEnv* env) : env_(env) {
        if (env_->PushLocalFrame(128) != JNI_OK) throw std::runtime_error("Snapshot local frame allocation failed");
    }
    ~SnapshotLocalFrame() { env_->PopLocalFrame(nullptr); }
private:
    JNIEnv* env_;
};
