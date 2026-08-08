

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "include/jni.h"
#include "include/jvmti.h"

#include <string>

extern JavaVM*    g_vm;
extern jvmtiEnv*  g_jvmti;

struct JniAttach {
    JNIEnv* env = nullptr;
    bool    attached = false;

    JniAttach();
    ~JniAttach();

    JNIEnv* operator->() const { return env; }
    explicit operator bool() const { return env != nullptr; }
};

void Dbg(const char* fmt, ...);

void LogTo(const char* fmt, ...);

void LogAndClearException(JNIEnv* env, const char* where);

jobject GetMinecraftClassLoader(JNIEnv* env, jvmtiEnv* jvmti);

jclass LoadClassInLoader(JNIEnv* env, jobject classLoader, const char* dotName);

void InitializeProxy(JNIEnv* env);
