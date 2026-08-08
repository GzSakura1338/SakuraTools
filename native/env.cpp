#include "proxy.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

JavaVM*   g_vm    = nullptr;
jvmtiEnv* g_jvmti = nullptr;

void Dbg(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    MessageBoxA(nullptr, buf, "MinecraftProxy", MB_OK);
    LogTo("[DBG] %s", buf);
}

void LogTo(const char* fmt, ...) {

    static CRITICAL_SECTION cs;
    static bool cs_init = false;
    if (!cs_init) {
        InitializeCriticalSection(&cs);
        cs_init = true;
    }
    EnterCriticalSection(&cs);

    static HANDLE h = INVALID_HANDLE_VALUE;
    static bool   tried = false;
    if (!tried) {
        tried = true;

        auto tryOpen = [](const char* path) -> HANDLE {
            if (!path || !*path) return INVALID_HANDLE_VALUE;
            return CreateFileA(path, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        };
        char buf[MAX_PATH];

        if (h == INVALID_HANDLE_VALUE) {
            const char* appdata = getenv("APPDATA");
            if (appdata && strlen(appdata) + 32 < MAX_PATH) {
                lstrcpynA(buf, appdata, MAX_PATH);
                lstrcatA(buf, "\\.minecraft\\proxy.log");
                h = tryOpen(buf);
            }
        }

        if (h == INVALID_HANDLE_VALUE) {
            char tmp[MAX_PATH];
            DWORD n = GetTempPathA(MAX_PATH, tmp);
            if (n > 0 && n < MAX_PATH - 24) {
                lstrcatA(tmp, "MinecraftProxy.log");
                h = tryOpen(tmp);
            }
        }

        if (h == INVALID_HANDLE_VALUE) {
            h = tryOpen("C:\\MinecraftProxy.log");
        }
    }

    if (h != INVALID_HANDLE_VALUE) {
        char msg[1200];
        va_list ap;
        va_start(ap, fmt);
        int base = snprintf(msg, sizeof(msg),
                            "[%08lu] ", (unsigned long)GetTickCount());
        vsnprintf(msg + base, sizeof(msg) - base - 2, fmt, ap);
        va_end(ap);
        size_t len = strlen(msg);
        if (len < sizeof(msg) - 2) {
            msg[len++] = '\r';
            msg[len++] = '\n';
        }

        DWORD wrote = 0;
        WriteFile(h, msg, (DWORD)len, &wrote, NULL);
    }
    LeaveCriticalSection(&cs);
}

void LogAndClearException(JNIEnv* env, const char* where) {
    if (!env || !env->ExceptionCheck()) return;
    jthrowable t = env->ExceptionOccurred();
    env->ExceptionClear();

    jclass thrCls = env->FindClass("java/lang/Throwable");
    jmethodID toStringMid =
        env->GetMethodID(thrCls, "toString", "()Ljava/lang/String;");
    jstring msg = (jstring)env->CallObjectMethod(t, toStringMid);

    const char* c = msg ? env->GetStringUTFChars(msg, nullptr) : "<unknown>";
    Dbg("[%s] %s", where, c);
    if (msg) env->ReleaseStringUTFChars(msg, c);

    jmethodID pst = env->GetMethodID(thrCls, "printStackTrace", "()V");
    if (pst) env->CallVoidMethod(t, pst);
    if (env->ExceptionCheck()) env->ExceptionClear();

    env->DeleteLocalRef(t);
    env->DeleteLocalRef(thrCls);
    if (msg) env->DeleteLocalRef(msg);
}

JniAttach::JniAttach() {
    if (!g_vm) return;
    jint rc = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    if (rc == JNI_EDETACHED) {
        JavaVMAttachArgs args{};
        args.version = JNI_VERSION_1_8;
        args.name = const_cast<char*>("MinecraftProxyNative");
        args.group = nullptr;
        if (g_vm->AttachCurrentThreadAsDaemon(
                reinterpret_cast<void**>(&env), &args) == JNI_OK) {
            attached = true;
        } else {
            env = nullptr;
        }
    } else if (rc != JNI_OK) {
        env = nullptr;
    }
}

JniAttach::~JniAttach() {
    if (attached && g_vm) g_vm->DetachCurrentThread();
}

jobject GetMinecraftClassLoader(JNIEnv* env, jvmtiEnv* jvmti) {
    if (!env || !jvmti) return nullptr;

    jint count = 0;
    jthread* threads = nullptr;
    if (jvmti->GetAllThreads(&count, &threads) != JVMTI_ERROR_NONE) {
        return nullptr;
    }

    static int s_dump_count = 0;
    bool dump = (s_dump_count++ < 3);
    if (dump) LogTo("GetMinecraftClassLoader: enumerating %d threads", (int)count);

    jobject result = nullptr;
    for (jint i = 0; i < count; ++i) {
        jvmtiThreadInfo info{};
        if (jvmti->GetThreadInfo(threads[i], &info) != JVMTI_ERROR_NONE) continue;

        bool hasLoader = (info.context_class_loader != nullptr);
        if (dump) {
            LogTo("  thread[%d] name='%s' has_loader=%d",
                  (int)i,
                  info.name ? info.name : "(null)",
                  hasLoader ? 1 : 0);
        }

        bool hit = info.name && hasLoader &&
                   std::strcmp(info.name, "Render thread") == 0;

        if (!hit && info.name && hasLoader) {
            jclass conn = LoadClassInLoader(env, info.context_class_loader,
                                            "net.minecraft.network.Connection");
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (conn) {
                if (dump) LogTo("     -> can load MC.Connection, PICKED");
                hit = true;
                env->DeleteLocalRef(conn);
            }
        }

        if (hit) result = env->NewGlobalRef(info.context_class_loader);

        if (info.name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(info.name));
        if (result) break;
    }

    jvmti->Deallocate(reinterpret_cast<unsigned char*>(threads));
    return result;
}

jclass LoadClassInLoader(JNIEnv* env, jobject classLoader, const char* dotName) {
    if (!env || !classLoader || !dotName) return nullptr;

    jclass clCls = env->GetObjectClass(classLoader);
    jmethodID mid =
        env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!mid) {
        env->ExceptionClear();
        env->DeleteLocalRef(clCls);
        return nullptr;
    }

    jstring jname = env->NewStringUTF(dotName);
    jclass  result = (jclass)env->CallObjectMethod(classLoader, mid, jname);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = nullptr;
    }

    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(clCls);
    return result;
}
