#pragma once

#include "include/jni.h"

// JNI method lookup prepares the class and searches inherited methods, unlike
// JVMTI GetClassMethods on a class that has only been loaded.
inline jmethodID FindMappedMethod(JNIEnv* env, jclass type, const char* name,
                                 const char* forgeName, const char* descriptor,
                                 bool isStatic = false) {
    if (!env || !type || env->ExceptionCheck()) return nullptr;
    auto lookup = [&](const char* candidate) {
        return isStatic ? env->GetStaticMethodID(type, candidate, descriptor)
                        : env->GetMethodID(type, candidate, descriptor);
    };
    jmethodID method = lookup(name);
    if (method || !env->ExceptionCheck()) return method;
    jthrowable error = env->ExceptionOccurred();
    env->ExceptionClear();
    jclass missing = env->FindClass("java/lang/NoSuchMethodError");
    bool canFallback = missing && env->IsInstanceOf(error, missing);
    if (missing) env->DeleteLocalRef(missing);
    if (!canFallback) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->Throw(error);
    }
    env->DeleteLocalRef(error);
    return canFallback ? lookup(forgeName) : nullptr;
}
