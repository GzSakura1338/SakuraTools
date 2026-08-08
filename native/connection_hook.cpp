#include "connection_hook.h"

#include "class_edit.h"
#include "classfile.h"
#include "trampolines.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kConnectionInternal = "net/minecraft/network/Connection";
constexpr const char* kChannelActiveName  = "channelActive";
constexpr const char* kChannelActiveDesc  =
    "(Lio/netty/channel/ChannelHandlerContext;)V";

void JNICALL ClassFileLoadHookCallback(
        jvmtiEnv* jvmti,
        JNIEnv*   ,
        jclass    ,
        jobject   ,
        const char* name,
        jobject   ,
        jint      class_data_len,
        const unsigned char* class_data,
        jint*     new_class_data_len,
        unsigned char** new_class_data) {

    if (!name) return;

    if (std::strcmp(name, kConnectionInternal) != 0) return;
    LogTo("ClassFileLoadHook fired for %s (len=%d)", name, (int)class_data_len);
    if (!g_hook.valid()) { LogTo("  ...but g_hook not valid yet, skipping"); return; }

    try {
        ClassFileEditor ed(class_data, static_cast<size_t>(class_data_len));

        u2 mref = ed.addMethodRef(
            g_hook.internalName,
            "onChannelActive",
            "(Ljava/lang/Object;Ljava/lang/Object;)V");

        std::vector<u1> insertion = {
            0x2A,
            0x2B,
            0xB8, static_cast<u1>((mref >> 8) & 0xFF), static_cast<u1>(mref & 0xFF),
        };

        bool ok = ed.prependToMethodCode(
            kChannelActiveName, kChannelActiveDesc,
            insertion, 2);
        if (!ok) {
            LogTo("  channelActive(%s) NOT FOUND in %s", kChannelActiveDesc, name);
            return;
        }
        LogTo("  patched channelActive OK, mref=%u", (unsigned)mref);

        std::vector<u1> patched = ed.serialize();
        unsigned char* buf = nullptr;
        if (jvmti->Allocate(static_cast<jlong>(patched.size()), &buf) != JVMTI_ERROR_NONE) {
            LogTo("  jvmti->Allocate failed");
            return;
        }
        std::memcpy(buf, patched.data(), patched.size());
        *new_class_data     = buf;
        *new_class_data_len = static_cast<jint>(patched.size());
        LogTo("  emitted patched bytes: %u -> %u", (unsigned)class_data_len,
              (unsigned)patched.size());
    } catch (const std::exception& e) {
        LogTo("  exception in patcher: %s", e.what());
    } catch (...) {
        LogTo("  unknown exception in patcher");
    }
}

bool EnableCapabilitiesAndCallbacks(jvmtiEnv* jvmti) {
    jvmtiCapabilities cap{};
    cap.can_generate_all_class_hook_events = 1;
    cap.can_retransform_classes            = 1;
    cap.can_retransform_any_class          = 1;
    cap.can_redefine_classes               = 1;
    cap.can_redefine_any_class             = 1;
    if (jvmti->AddCapabilities(&cap) != JVMTI_ERROR_NONE) {
        Dbg("Connection hook: AddCapabilities failed");
        return false;
    }

    jvmtiEventCallbacks cb{};
    cb.ClassFileLoadHook = &ClassFileLoadHookCallback;
    if (jvmti->SetEventCallbacks(&cb, sizeof(cb)) != JVMTI_ERROR_NONE) {
        Dbg("Connection hook: SetEventCallbacks failed");
        return false;
    }

    if (jvmti->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr)
        != JVMTI_ERROR_NONE) {
        Dbg("Connection hook: SetEventNotificationMode failed");
        return false;
    }
    return true;
}

void RetransformIfLoaded(JNIEnv* env, jvmtiEnv* jvmti) {

    jint count = 0;
    jclass* classes = nullptr;
    if (jvmti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE) return;

    int found = 0, retransformed = 0;
    for (jint i = 0; i < count; ++i) {
        char* sig = nullptr;
        if (jvmti->GetClassSignature(classes[i], &sig, nullptr) != JVMTI_ERROR_NONE) continue;
        if (sig && std::strcmp(sig, "Lnet/minecraft/network/Connection;") == 0) {
            ++found;
            jvmtiError e = jvmti->RetransformClasses(1, &classes[i]);
            if (e == JVMTI_ERROR_NONE) {
                ++retransformed;
            } else {
                LogTo("Connection hook: RetransformClasses failed err=%d", (int)e);
            }
        }
        if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
    }
    LogTo("RetransformIfLoaded: enumerated %d classes, found %d Connection, retransformed %d",
          (int)count, found, retransformed);

    for (jint i = 0; i < count; ++i) env->DeleteLocalRef(classes[i]);
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
}

}

bool InstallConnectionHook(JNIEnv* env) {
    if (!g_jvmti || !env) return false;
    if (!g_hook.valid()) {
        Dbg("Connection hook: called before HookBridge installed");
        return false;
    }
    if (!EnableCapabilitiesAndCallbacks(g_jvmti)) return false;
    RetransformIfLoaded(env, g_jvmti);
    return true;
}
