#include "trampolines.h"

#include "classfile.h"
#include "random_name.h"
#include "relay_handler.h"
#include "b_server.h"

#include <cstdio>
#include <string>

HookBridge g_hook;

namespace {

void JNICALL Native_trace(JNIEnv* env, jclass, jstring jmsg) {
    if (!jmsg) return;
    const char* c = env->GetStringUTFChars(jmsg, nullptr);
    if (!c) return;

    jclass sysCls = env->FindClass("java/lang/System");
    jfieldID outFid =
        env->GetStaticFieldID(sysCls, "out", "Ljava/io/PrintStream;");
    jobject out = env->GetStaticObjectField(sysCls, outFid);
    jclass psCls = env->GetObjectClass(out);
    jmethodID pmid =
        env->GetMethodID(psCls, "println", "(Ljava/lang/String;)V");

    std::string line = "[MinecraftProxy][native] ";
    line += c;
    jstring wrapped = env->NewStringUTF(line.c_str());
    env->CallVoidMethod(out, pmid, wrapped);

    env->ReleaseStringUTFChars(jmsg, c);
    env->DeleteLocalRef(wrapped);
    env->DeleteLocalRef(psCls);
    env->DeleteLocalRef(out);
    env->DeleteLocalRef(sysCls);
}

void JNICALL Native_onChannelActive(JNIEnv* env,
                                    jclass,
                                    jobject connection,
                                    jobject ctx) {
    LogTo("Native_onChannelActive fired: conn=%p ctx=%p",
          (void*)connection, (void*)ctx);

    BServer_SetTargetConnection(env, connection);
    RelayHandler_AttachToPipeline(env, ctx);

    LogTo("Native_onChannelActive returning");
}

}

bool InstallHookBridge(JNIEnv* env) {
    if (g_hook.valid()) return true;
    if (!env) return false;

    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader) {

        return false;
    }

    std::string simple = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);
    std::string dotted   = internal;
    for (char& ch : dotted) if (ch == '/') ch = '.';

    ClassBuilder cb(internal, "java/lang/Object", 52);
    cb.addNativeMethod("trace", "(Ljava/lang/String;)V");
    cb.addNativeMethod("onChannelActive",
                       "(Ljava/lang/Object;Ljava/lang/Object;)V");
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(
        internal.c_str(),
        mcLoader,
        reinterpret_cast<const jbyte*>(bytes.data()),
        static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "InstallHookBridge/DefineClass");
        env->DeleteGlobalRef(mcLoader);
        return false;
    }

    JNINativeMethod natives[] = {
        { const_cast<char*>("trace"),
          const_cast<char*>("(Ljava/lang/String;)V"),
          reinterpret_cast<void*>(&Native_trace) },
        { const_cast<char*>("onChannelActive"),
          const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;)V"),
          reinterpret_cast<void*>(&Native_onChannelActive) },
    };
    if (env->RegisterNatives(defined, natives, 2) != 0) {
        LogAndClearException(env, "InstallHookBridge/RegisterNatives");
        env->DeleteLocalRef(defined);
        env->DeleteGlobalRef(mcLoader);
        return false;
    }

    jmethodID traceMid =
        env->GetStaticMethodID(defined, "trace", "(Ljava/lang/String;)V");
    jmethodID onActiveMid =
        env->GetStaticMethodID(defined, "onChannelActive",
                               "(Ljava/lang/Object;Ljava/lang/Object;)V");
    if (!traceMid || !onActiveMid) {
        LogAndClearException(env, "InstallHookBridge/GetStaticMethodID");
        env->DeleteLocalRef(defined);
        env->DeleteGlobalRef(mcLoader);
        return false;
    }

    g_hook.klass = static_cast<jclass>(env->NewGlobalRef(defined));
    g_hook.trace = traceMid;
    g_hook.onChannelActive = onActiveMid;
    g_hook.internalName = std::move(internal);
    g_hook.dotName      = std::move(dotted);

    env->DeleteLocalRef(defined);
    env->DeleteGlobalRef(mcLoader);
    return true;
}
