

#include "proxy.h"
#include "random_name.h"
#include "trampolines.h"
#include "relay_handler.h"
#include "connection_hook.h"
#include "b_server.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

std::atomic_bool g_initialised{false};

constexpr size_t kHookSize = 12;

BYTE g_originalNanoBytes[kHookSize] = {};
LPVOID g_nanoAddress = nullptr;

using Fn_JVM_NanoTime = jlong (JNICALL*)(JNIEnv*, jclass);
Fn_JVM_NanoTime g_realNanoTime = nullptr;

static void WriteJumpTo(void* target, void* dest) {
    BYTE stub[kHookSize] = {
        0x48, 0xB8,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xE0
    };
    std::memcpy(stub + 2, &dest, sizeof(dest));

    DWORD oldProtect = 0;
    VirtualProtect(target, kHookSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(target, stub, kHookSize);
    VirtualProtect(target, kHookSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, kHookSize);
}

static void RestoreOriginal(void* target, const BYTE* saved) {
    DWORD oldProtect = 0;
    VirtualProtect(target, kHookSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(target, saved, kHookSize);
    VirtualProtect(target, kHookSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, kHookSize);
}

jlong JNICALL HookedNanoTime(JNIEnv* env, jclass klass) {

    RestoreOriginal(g_nanoAddress, g_originalNanoBytes);

    bool expected = false;
    if (g_initialised.compare_exchange_strong(expected, true)) {
        LogTo("HookedNanoTime fired (first time); unhooked");
        if (env->GetJavaVM(&g_vm) == JNI_OK && g_vm) {
            LogTo("  GetJavaVM OK vm=%p", (void*)g_vm);
            if (g_vm->GetEnv(reinterpret_cast<void**>(&g_jvmti),
                             JVMTI_VERSION_1_2) != JNI_OK) {
                g_jvmti = nullptr;
                LogTo("  GetEnv(JVMTI) FAILED");
            } else {
                LogTo("  GetEnv(JVMTI) OK jvmti=%p", (void*)g_jvmti);
            }
        } else {
            LogTo("  GetJavaVM FAILED");
        }

        LogTo("about to call InitializeProxy");
        InitializeProxy(env);
        LogTo("InitializeProxy returned");
    }

    return g_realNanoTime(env, klass);
}

typedef jint (JNICALL* Fn_JNI_GetCreatedJavaVMs)(JavaVM**, jsize, jsize*);

DWORD WINAPI BootstrapThread(LPVOID) {
    LogTo("BootstrapThread start; waiting for jvm.dll");
    HMODULE jvm = GetModuleHandleA("jvm.dll");
    while (!jvm) {
        Sleep(50);
        jvm = GetModuleHandleA("jvm.dll");
    }
    LogTo("jvm.dll loaded at %p", (void*)jvm);

    auto getVms = reinterpret_cast<Fn_JNI_GetCreatedJavaVMs>(
        GetProcAddress(jvm, "JNI_GetCreatedJavaVMs"));
    if (!getVms) {
        LogTo("GetProcAddress(JNI_GetCreatedJavaVMs) FAILED");
        return 0;
    }
    for (int i = 0; i < 600; ++i) {
        jsize n = 0;
        if (getVms(&g_vm, 1, &n) == JNI_OK && n >= 1 && g_vm) break;
        g_vm = nullptr;
        Sleep(500);
    }
    if (!g_vm) {
        LogTo("BootstrapThread: JVM never appeared");
        return 0;
    }
    LogTo("Got JavaVM=%p", (void*)g_vm);

    JavaVMAttachArgs aa{};
    aa.version = JNI_VERSION_1_8;
    aa.name = const_cast<char*>("MinecraftProxyBoot");
    aa.group = nullptr;
    JNIEnv* env = nullptr;
    if (g_vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), &aa) != JNI_OK
        || !env) {
        LogTo("BootstrapThread: AttachCurrentThreadAsDaemon FAILED");
        return 0;
    }
    LogTo("BootstrapThread: attached, env=%p", (void*)env);

    if (g_vm->GetEnv(reinterpret_cast<void**>(&g_jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
        g_jvmti = nullptr;
        LogTo("GetEnv(JVMTI) FAILED");
        g_vm->DetachCurrentThread();
        return 0;
    }
    LogTo("Got jvmtiEnv=%p", (void*)g_jvmti);

    InitializeProxy(env);
    g_vm->DetachCurrentThread();
    return 0;
}

}

static void RawWriteFile(const char* line) {

    HANDLE h = CreateFileA("D:\\.minecraft\\proxy.log",
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    SetFilePointer(h, 0, NULL, FILE_END);

    DWORD n = 0; while (line[n]) ++n;
    WriteFile(h, line, n, &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        RawWriteFile("[raw] DllMain entry");
        DisableThreadLibraryCalls(hModule);
        RawWriteFile("[raw] DisableThreadLibraryCalls done");
        HANDLE h = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
        RawWriteFile("[raw] CreateThread done");

        LogTo("=== DllMain PROCESS_ATTACH ===");
        RawWriteFile("[raw] first LogTo returned");
    }
    return TRUE;
}

static DWORD WINAPI ProxyInitWorker(LPVOID);

void InitializeProxy(JNIEnv* ) {
    LogTo("InitializeProxy: spawning bootstrap worker");
    HANDLE h = CreateThread(nullptr, 0, ProxyInitWorker, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

static DWORD WINAPI ProxyInitWorker(LPVOID) {
    LogTo("ProxyInitWorker: attaching to JVM as daemon");
    JavaVMAttachArgs args{};
    args.version = JNI_VERSION_1_8;
    args.name = const_cast<char*>("MinecraftProxyInit");
    args.group = nullptr;
    JNIEnv* env = nullptr;
    if (g_vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), &args) != JNI_OK
        || !env) {
        LogTo("ProxyInitWorker: AttachCurrentThreadAsDaemon FAILED");
        return 0;
    }
    LogTo("ProxyInitWorker: attached; polling for MC ClassLoader");

    bool installed = false;
    for (int i = 0; i < 240; ++i) {
        if (InstallHookBridge(env)) { installed = true; break; }
        Sleep(500);
    }
    if (!installed) {
        LogTo("ProxyInitWorker: gave up waiting for MC ClassLoader");
        g_vm->DetachCurrentThread();
        return 0;
    }
    LogTo("ProxyInitWorker: InstallHookBridge OK: %s", g_hook.dotName.c_str());

    if (!InstallRelayHandler(env)) {
        LogTo("ProxyInitWorker: InstallRelayHandler FAILED");
        g_vm->DetachCurrentThread();
        return 0;
    }
    LogTo("ProxyInitWorker: InstallRelayHandler OK: %s", g_relay.dotName.c_str());

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "bridge=%s relay=%s (installed)",
                  g_hook.dotName.c_str(),
                  g_relay.dotName.c_str());
    jstring msg = env->NewStringUTF(buf);
    env->CallStaticVoidMethod(g_hook.klass, g_hook.trace, msg);
    LogAndClearException(env, "InitializeProxy/trace");
    env->DeleteLocalRef(msg);

    if (!InstallConnectionHook(env)) {
        LogTo("ProxyInitWorker: InstallConnectionHook FAILED");
        g_vm->DetachCurrentThread();
        return 0;
    }
    LogTo("ProxyInitWorker: InstallConnectionHook OK");

    if (!InstallBServer(env)) {
        LogTo("ProxyInitWorker: InstallBServer FAILED (B-side proxy unavailable)");

    } else {
        LogTo("ProxyInitWorker: InstallBServer OK — listening on 127.0.0.1:25565");

        if (BServer_TryCaptureLiveConnection(env)) {
            LogTo("ProxyInitWorker: mid-session — A already in-game; live connection captured, gate skipped");
        } else if (BServer_BlockAMainThreadUntilBConnected(env)) {
            LogTo("ProxyInitWorker: Queued A Render thread blocking gate task");
        } else {
            LogTo("ProxyInitWorker: Failed to queue A Render thread blocking gate task");
        }
    }

    LogTo("ProxyInitWorker: done — proxy fully armed");
    g_vm->DetachCurrentThread();
    return 0;
}
