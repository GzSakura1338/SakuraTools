#pragma once

#include "proxy.h"

// Descriptor fallbacks for classes whose mapped names are unavailable.
// Class lookup results are local references owned by the caller.
namespace proxy_server {
jmethodID findMethodByDescriptor(jclass klass, const char* desc, bool wantStatic);
jfieldID findFieldByDescriptor(jclass klass, const char* desc, bool wantStatic);
jclass findLoadedBySig(JNIEnv* env, const char* sig);
jclass loadOrFind(JNIEnv* env, jobject mcLoader, const char* dot, const char* sig);
} // namespace proxy_server
std::string JniClassName(JNIEnv* env, jobject object);
