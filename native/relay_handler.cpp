#include "relay_handler.h"

#include "b_server.h"
#include "classfile.h"
#include "jni_lookup.h"
#include "packet_policy.h"
#include "random_name.h"
#include "runtime_gate.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

RelayHandler g_relay;

namespace {

constexpr const char* kSuperInternal = "io/netty/channel/ChannelDuplexHandler";
constexpr const char* kContextInternal = "io/netty/channel/ChannelHandlerContext";
constexpr const char* kPipelineInternal = "io/netty/channel/ChannelPipeline";
constexpr const char* kHandlerInternal = "io/netty/channel/ChannelHandler";
constexpr const char* kPromiseInternal = "io/netty/channel/ChannelPromise";

constexpr const char* kChannelReadDesc = "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
constexpr const char* kWriteDesc =
    "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;Lio/netty/channel/ChannelPromise;)V";

std::atomic<uint64_t> g_channelSeq{0};

std::mutex g_bypassMu;
std::vector<jobject> g_bypassPending;
std::mutex g_pipelineMu;
std::vector<jweak> g_pipelines;

bool consumeBypassMark(JNIEnv* env, jobject msg) {
    std::lock_guard<std::mutex> lock(g_bypassMu);
    for (size_t i = 0; i < g_bypassPending.size(); ++i) {
        if (env->IsSameObject(g_bypassPending[i], msg)) {
            env->DeleteGlobalRef(g_bypassPending[i]);
            g_bypassPending.erase(g_bypassPending.begin() + i);
            return true;
        }
    }
    return false;
}

void printlnUtf8(JNIEnv* env, const char* line) {
    jclass sysCls = env->FindClass("java/lang/System");
    jfieldID outFid = env->GetStaticFieldID(sysCls, "out", "Ljava/io/PrintStream;");
    jobject out = env->GetStaticObjectField(sysCls, outFid);
    jclass psCls = env->GetObjectClass(out);
    jmethodID pmid = env->GetMethodID(psCls, "println", "(Ljava/lang/String;)V");
    jstring js = env->NewStringUTF(line);
    env->CallVoidMethod(out, pmid, js);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(js);
    env->DeleteLocalRef(psCls);
    env->DeleteLocalRef(out);
    env->DeleteLocalRef(sysCls);
}

void JNICALL Native_RelayChannelRead(JNIEnv* env, jobject, jobject ctx, jobject msg) {
    RuntimeCallback callback;
    if (!callback) {
        env->CallObjectMethod(ctx, g_relay.netty.fireChannelReadMid, msg);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }
    // Queue A's application task before mirroring. The main-thread snapshot can
    // then discard earlier deltas without losing packets still awaiting application.
    std::lock_guard<std::recursive_mutex> dispatch(BServer_DispatchMutex());
    env->CallObjectMethod(ctx, g_relay.netty.fireChannelReadMid, msg);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    BServer_ForwardToB(env, msg);
}

void JNICALL Native_RelayWrite(JNIEnv* env, jobject, jobject ctx, jobject msg, jobject promise) {
    RuntimeCallback callback;
    if (!callback) {
        env->CallObjectMethod(ctx, g_relay.netty.ctxWriteMid, msg, promise);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }
    std::string cls = JniClassName(env, msg);

    bool bypass = consumeBypassMark(env, msg);
    bool allow = bypass || packet_policy::allowFromA(cls, BServer_IsBActive());

    if (allow) {
        env->CallObjectMethod(ctx, g_relay.netty.ctxWriteMid, msg, promise);
    } else {
        if (promise && g_relay.netty.promiseSetSuccessMid) {
            env->CallObjectMethod(promise, g_relay.netty.promiseSetSuccessMid);
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

bool defineRelayClass(JNIEnv* env, jobject mcLoader) {
    std::string simple = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);
    std::string dotted = internal;
    for (char& ch : dotted)
        if (ch == '/')
            ch = '.';

    ClassBuilder cb(internal, kSuperInternal, 52);

    u2 superInitRef = cb.methodRef(kSuperInternal, "<init>", "()V");
    std::vector<u1> ctorCode = {
        0x2A, 0xB7, static_cast<u1>((superInitRef >> 8) & 0xFF), static_cast<u1>(superInitRef & 0xFF), 0xB1,
    };
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctorCode, 1, 1);

    cb.addNativeMethod("channelRead", kChannelReadDesc, ACC_PUBLIC | ACC_NATIVE);
    cb.addNativeMethod("write", kWriteDesc, ACC_PUBLIC | ACC_NATIVE);

    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader, reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "InstallRelayHandler/DefineClass");
        return false;
    }

    JNINativeMethod natives[] = {
        {const_cast<char*>("channelRead"), const_cast<char*>(kChannelReadDesc),
         reinterpret_cast<void*>(&Native_RelayChannelRead)},
        {const_cast<char*>("write"), const_cast<char*>(kWriteDesc), reinterpret_cast<void*>(&Native_RelayWrite)},
    };
    if (env->RegisterNatives(defined, natives, 2) != 0) {
        LogAndClearException(env, "InstallRelayHandler/RegisterNatives");
        env->DeleteLocalRef(defined);
        return false;
    }

    jmethodID ctor = env->GetMethodID(defined, "<init>", "()V");
    if (!ctor) {
        LogAndClearException(env, "InstallRelayHandler/ctor");
        env->DeleteLocalRef(defined);
        return false;
    }

    g_relay.klass = static_cast<jclass>(env->NewGlobalRef(defined));
    g_relay.ctor = ctor;
    g_relay.internalName = std::move(internal);
    g_relay.dotName = std::move(dotted);
    env->DeleteLocalRef(defined);
    return true;
}

bool cacheNettyRefs(JNIEnv* env, jobject mcLoader) {

    jclass ctxCls = LoadClassInLoader(env, mcLoader, "io.netty.channel.ChannelHandlerContext");
    if (!ctxCls) {
        Dbg("Relay: couldn't load ChannelHandlerContext");
        return false;
    }
    g_relay.netty.contextCls = static_cast<jclass>(env->NewGlobalRef(ctxCls));
    g_relay.netty.pipelineMid = env->GetMethodID(ctxCls, "pipeline", "()Lio/netty/channel/ChannelPipeline;");
    g_relay.netty.fireChannelReadMid =
        env->GetMethodID(ctxCls, "fireChannelRead", "(Ljava/lang/Object;)Lio/netty/channel/ChannelHandlerContext;");
    g_relay.netty.ctxWriteMid = env->GetMethodID(
        ctxCls, "write", "(Ljava/lang/Object;Lio/netty/channel/ChannelPromise;)Lio/netty/channel/ChannelFuture;");
    g_relay.netty.ctxWriteFlushMid =
        env->GetMethodID(ctxCls, "writeAndFlush", "(Ljava/lang/Object;)Lio/netty/channel/ChannelFuture;");
    env->DeleteLocalRef(ctxCls);

    jclass pipCls = LoadClassInLoader(env, mcLoader, "io.netty.channel.ChannelPipeline");
    if (!pipCls) {
        Dbg("Relay: couldn't load ChannelPipeline");
        return false;
    }
    g_relay.netty.pipelineCls = static_cast<jclass>(env->NewGlobalRef(pipCls));
    g_relay.netty.addFirstMid = env->GetMethodID(
        pipCls, "addFirst", "(Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
    g_relay.netty.addBeforeMid = env->GetMethodID(
        pipCls, "addBefore",
        "(Ljava/lang/String;Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
    env->DeleteLocalRef(pipCls);

    jclass promCls = LoadClassInLoader(env, mcLoader, "io.netty.channel.ChannelPromise");
    if (!promCls) {
        Dbg("Relay: couldn't load ChannelPromise");
        return false;
    }
    g_relay.netty.promiseCls = static_cast<jclass>(env->NewGlobalRef(promCls));
    g_relay.netty.promiseSetSuccessMid = env->GetMethodID(promCls, "setSuccess", "()Lio/netty/channel/ChannelPromise;");
    env->DeleteLocalRef(promCls);

    if (!g_relay.netty.pipelineMid || !g_relay.netty.fireChannelReadMid || !g_relay.netty.ctxWriteMid ||
        !g_relay.netty.addFirstMid || !g_relay.netty.addBeforeMid || !g_relay.netty.promiseSetSuccessMid) {
        Dbg("Relay: one or more netty method IDs missing");
        return false;
    }
    return true;
}

} // namespace

void RelayFilter_MarkBypass(JNIEnv* env, jobject packet) {
    if (!env || !packet)
        return;
    jobject gref = env->NewGlobalRef(packet);
    if (!gref)
        return;
    std::lock_guard<std::mutex> lock(g_bypassMu);

    if (g_bypassPending.size() > 256) {
        env->DeleteGlobalRef(g_bypassPending.front());
        g_bypassPending.erase(g_bypassPending.begin());
    }
    g_bypassPending.push_back(gref);
}

bool InstallRelayHandler(JNIEnv* env) {
    if (g_relay.valid())
        return true;
    if (!env)
        return false;

    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader)
        return false;

    bool ok = defineRelayClass(env, mcLoader) && cacheNettyRefs(env, mcLoader);
    env->DeleteGlobalRef(mcLoader);
    return ok;
}

static bool attachHandlerToPipeline(JNIEnv* env, jobject pipeline) {
    std::lock_guard<std::mutex> lock(g_pipelineMu);
    for (auto it = g_pipelines.begin(); it != g_pipelines.end();) {
        if (env->IsSameObject(*it, nullptr)) {
            env->DeleteWeakGlobalRef(*it);
            it = g_pipelines.erase(it);
        } else {
            if (env->IsSameObject(*it, pipeline))
                return true;
            ++it;
        }
    }
    uint64_t seq = ++g_channelSeq;
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "proxy_relay_%llu", static_cast<unsigned long long>(seq));
    jstring name = env->NewStringUTF(nameBuf);

    jobject handler = env->NewObject(g_relay.klass, g_relay.ctor);
    if (!handler || env->ExceptionCheck()) {
        LogTo("Attach: NewObject(RelayHandler) FAILED");
        env->ExceptionClear();
        env->DeleteLocalRef(name);
        return false;
    }

    jstring base = env->NewStringUTF("packet_handler");
    jobject unused = env->CallObjectMethod(pipeline, g_relay.netty.addBeforeMid, base, name, handler);
    bool addBeforeFailed = env->ExceptionCheck();
    const char* mode;
    if (addBeforeFailed) {
        env->ExceptionClear();
        LogTo("Attach: addBefore(packet_handler,...) THREW, falling back to addFirst");
        unused = env->CallObjectMethod(pipeline, g_relay.netty.addFirstMid, name, handler);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            LogTo("Attach: addFirst ALSO threw — giving up");
            mode = "FAILED";
        } else {
            mode = "HEAD-fallback";
        }
    } else {
        mode = "before-packet_handler";
    }
    LogTo("Attach: %s as %s (class %s)", mode, nameBuf, g_relay.dotName.c_str());
    if (std::strcmp(mode, "FAILED") != 0) {
        jweak weak = env->NewWeakGlobalRef(pipeline);
        if (weak)
            g_pipelines.push_back(weak);
    }
    if (unused)
        env->DeleteLocalRef(unused);
    env->DeleteLocalRef(base);
    env->DeleteLocalRef(handler);
    env->DeleteLocalRef(name);
    return std::strcmp(mode, "FAILED") != 0;
}

void RelayHandler_AttachToPipeline(JNIEnv* env, jobject ctx) {
    LogTo("Attach: entering ctx=%p", (void*)ctx);
    if (!ctx)
        return;
    if (!g_relay.valid()) {
        LogTo("Attach: g_relay invalid, installing");
        if (!InstallRelayHandler(env)) {
            LogTo("Attach: install failed");
            return;
        }
    }

    jobject pipeline = env->CallObjectMethod(ctx, g_relay.netty.pipelineMid);
    if (!pipeline || env->ExceptionCheck()) {
        LogTo("Attach: ctx.pipeline() FAILED");
        env->ExceptionClear();
        return;
    }
    LogTo("Attach: got pipeline=%p", (void*)pipeline);
    attachHandlerToPipeline(env, pipeline);
    env->DeleteLocalRef(pipeline);
}

bool RelayHandler_AttachToPipelineObject(JNIEnv* env, jobject pipeline) {
    if (!pipeline)
        return false;
    if (!g_relay.valid()) {
        if (!InstallRelayHandler(env)) {
            LogTo("AttachObj: install failed");
            return false;
        }
    }
    return attachHandlerToPipeline(env, pipeline);
}

bool RelayHandler_DetachAll(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(g_pipelineMu);
    std::vector<jweak> pending;
    if (g_relay.netty.pipelineCls) {
        jmethodID remove = env->GetMethodID(g_relay.netty.pipelineCls, "remove",
                                            "(Ljava/lang/Class;)Lio/netty/channel/ChannelHandler;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        jmethodID get =
            env->GetMethodID(g_relay.netty.pipelineCls, "get", "(Ljava/lang/Class;)Lio/netty/channel/ChannelHandler;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        for (jweak weak : g_pipelines) {
            jobject pipeline = env->NewLocalRef(weak);
            bool ok = !env->ExceptionCheck() && (!pipeline || (remove && get));
            if (env->ExceptionCheck())
                env->ExceptionClear();
            if (pipeline && ok) {
                // An earlier removal may have succeeded despite a failed cleanup attempt.
                jobject handler = env->CallObjectMethod(pipeline, get, g_relay.klass);
                ok = !env->ExceptionCheck();
                if (env->ExceptionCheck())
                    env->ExceptionClear();
                if (handler) {
                    env->DeleteLocalRef(handler);
                    handler = env->CallObjectMethod(pipeline, remove, g_relay.klass);
                    ok = !env->ExceptionCheck();
                    if (env->ExceptionCheck())
                        env->ExceptionClear();
                    if (handler)
                        env->DeleteLocalRef(handler);
                }
            }
            if (pipeline)
                env->DeleteLocalRef(pipeline);
            if (ok)
                env->DeleteWeakGlobalRef(weak);
            else
                pending.push_back(weak);
        }
    } else {
        pending.swap(g_pipelines);
    }
    g_pipelines.swap(pending);
    std::lock_guard<std::mutex> bypassLock(g_bypassMu);
    for (jobject packet : g_bypassPending)
        env->DeleteGlobalRef(packet);
    g_bypassPending.clear();
    LogTo("STOP: relay detach pending=%zu", g_pipelines.size());
    return g_pipelines.empty();
}
