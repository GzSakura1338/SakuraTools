#include "relay_handler.h"

#include "b_server.h"
#include "classfile.h"
#include "random_name.h"
#include "world_cache.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

RelayHandler g_relay;

namespace {

constexpr const char* kSuperInternal     = "io/netty/channel/ChannelDuplexHandler";
constexpr const char* kContextInternal   = "io/netty/channel/ChannelHandlerContext";
constexpr const char* kPipelineInternal  = "io/netty/channel/ChannelPipeline";
constexpr const char* kHandlerInternal   = "io/netty/channel/ChannelHandler";
constexpr const char* kPromiseInternal   = "io/netty/channel/ChannelPromise";

constexpr const char* kChannelReadDesc =
    "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
constexpr const char* kWriteDesc =
    "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;Lio/netty/channel/ChannelPromise;)V";

std::atomic<uint64_t> g_channelSeq{0};

std::mutex g_bypassMu;
std::vector<jobject> g_bypassPending;

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
    jfieldID outFid =
        env->GetStaticFieldID(sysCls, "out", "Ljava/io/PrintStream;");
    jobject out = env->GetStaticObjectField(sysCls, outFid);
    jclass psCls = env->GetObjectClass(out);
    jmethodID pmid =
        env->GetMethodID(psCls, "println", "(Ljava/lang/String;)V");
    jstring js = env->NewStringUTF(line);
    env->CallVoidMethod(out, pmid, js);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(js);
    env->DeleteLocalRef(psCls);
    env->DeleteLocalRef(out);
    env->DeleteLocalRef(sysCls);
}

std::string javaClassName(JNIEnv* env, jobject o) {
    if (!o) return "null";
    if (!g_jvmti) return "<no-jvmti>";
    jclass c = env->GetObjectClass(o);
    if (!c) return "<no-class>";

    char* sig = nullptr;
    jvmtiError rc = g_jvmti->GetClassSignature(c, &sig, nullptr);
    env->DeleteLocalRef(c);
    if (rc != JVMTI_ERROR_NONE || !sig) return "<?>";

    std::string out;
    const char* p = sig;
    if (*p == 'L') {
        ++p;
        for (; *p && *p != ';'; ++p) out.push_back(*p == '/' ? '.' : *p);
    } else {
        out = sig;
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
    return out;
}

void JNICALL Native_RelayChannelRead(JNIEnv* env,
                                     jobject ,
                                     jobject ctx,
                                     jobject msg) {

    std::string cls = javaClassName(env, msg);
    bool mirrorToB = BServer_IsBActive() &&
        cls.rfind("net.minecraft.network.protocol.game.", 0) == 0;
    if (mirrorToB) {
        BServer_ForwardToB(env, msg);
    }

    env->CallObjectMethod(ctx, g_relay.netty.fireChannelReadMid, msg);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

bool shouldAllowC2S(const std::string& fqcn) {

    static constexpr const char kGamePrefix[] =
        "net.minecraft.network.protocol.game.";
    if (fqcn.rfind(kGamePrefix, 0) != 0) return true;

    if (!BServer_IsBActive()) return true;

    const char* simple = fqcn.c_str() + (sizeof(kGamePrefix) - 1);
    return std::strcmp(simple, "ServerboundCustomPayloadPacket") == 0;
}

void JNICALL Native_RelayWrite(JNIEnv* env,
                               jobject ,
                               jobject ctx,
                               jobject msg,
                               jobject promise) {
    std::string cls = javaClassName(env, msg);

    bool bypass = consumeBypassMark(env, msg);
    bool allow = bypass || shouldAllowC2S(cls);
    LogTo("[C2S %s] %s", bypass ? "ROUTE" : (allow ? "PASS" : "DROP"), cls.c_str());

    if (allow) {
        env->CallObjectMethod(ctx, g_relay.netty.ctxWriteMid, msg, promise);
    } else {
        if (promise && g_relay.netty.promiseSetSuccessMid) {
            env->CallObjectMethod(promise, g_relay.netty.promiseSetSuccessMid);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

bool defineRelayClass(JNIEnv* env, jobject mcLoader) {
    std::string simple   = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);
    std::string dotted   = internal;
    for (char& ch : dotted) if (ch == '/') ch = '.';

    ClassBuilder cb(internal, kSuperInternal, 52);

    u2 superInitRef = cb.methodRef(kSuperInternal, "<init>", "()V");
    std::vector<u1> ctorCode = {
        0x2A,
        0xB7, static_cast<u1>((superInitRef >> 8) & 0xFF), static_cast<u1>(superInitRef & 0xFF),
        0xB1,
    };
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctorCode, 1, 1);

    cb.addNativeMethod("channelRead", kChannelReadDesc,
                       ACC_PUBLIC | ACC_NATIVE);
    cb.addNativeMethod("write", kWriteDesc,
                       ACC_PUBLIC | ACC_NATIVE);

    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(
        internal.c_str(),
        mcLoader,
        reinterpret_cast<const jbyte*>(bytes.data()),
        static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "InstallRelayHandler/DefineClass");
        return false;
    }

    JNINativeMethod natives[] = {
        { const_cast<char*>("channelRead"),
          const_cast<char*>(kChannelReadDesc),
          reinterpret_cast<void*>(&Native_RelayChannelRead) },
        { const_cast<char*>("write"),
          const_cast<char*>(kWriteDesc),
          reinterpret_cast<void*>(&Native_RelayWrite) },
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
    g_relay.ctor  = ctor;
    g_relay.internalName = std::move(internal);
    g_relay.dotName      = std::move(dotted);
    env->DeleteLocalRef(defined);
    return true;
}

bool cacheNettyRefs(JNIEnv* env, jobject mcLoader) {

    jclass ctxCls = LoadClassInLoader(env, mcLoader,
                                      "io.netty.channel.ChannelHandlerContext");
    if (!ctxCls) { Dbg("Relay: couldn't load ChannelHandlerContext"); return false; }
    g_relay.netty.contextCls = static_cast<jclass>(env->NewGlobalRef(ctxCls));
    g_relay.netty.pipelineMid = env->GetMethodID(
        ctxCls, "pipeline", "()Lio/netty/channel/ChannelPipeline;");
    g_relay.netty.fireChannelReadMid = env->GetMethodID(
        ctxCls, "fireChannelRead",
        "(Ljava/lang/Object;)Lio/netty/channel/ChannelHandlerContext;");
    g_relay.netty.ctxWriteMid = env->GetMethodID(
        ctxCls, "write",
        "(Ljava/lang/Object;Lio/netty/channel/ChannelPromise;)Lio/netty/channel/ChannelFuture;");
    g_relay.netty.ctxWriteFlushMid = env->GetMethodID(
        ctxCls, "writeAndFlush",
        "(Ljava/lang/Object;)Lio/netty/channel/ChannelFuture;");
    env->DeleteLocalRef(ctxCls);

    jclass pipCls = LoadClassInLoader(env, mcLoader,
                                      "io.netty.channel.ChannelPipeline");
    if (!pipCls) { Dbg("Relay: couldn't load ChannelPipeline"); return false; }
    g_relay.netty.pipelineCls = static_cast<jclass>(env->NewGlobalRef(pipCls));
    g_relay.netty.addFirstMid = env->GetMethodID(
        pipCls, "addFirst",
        "(Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
    g_relay.netty.addBeforeMid = env->GetMethodID(
        pipCls, "addBefore",
        "(Ljava/lang/String;Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
    env->DeleteLocalRef(pipCls);

    jclass promCls = LoadClassInLoader(env, mcLoader,
                                       "io.netty.channel.ChannelPromise");
    if (!promCls) { Dbg("Relay: couldn't load ChannelPromise"); return false; }
    g_relay.netty.promiseCls = static_cast<jclass>(env->NewGlobalRef(promCls));
    g_relay.netty.promiseSetSuccessMid = env->GetMethodID(
        promCls, "setSuccess", "()Lio/netty/channel/ChannelPromise;");
    env->DeleteLocalRef(promCls);

    if (!g_relay.netty.pipelineMid || !g_relay.netty.fireChannelReadMid ||
        !g_relay.netty.ctxWriteMid  || !g_relay.netty.addFirstMid ||
        !g_relay.netty.addBeforeMid || !g_relay.netty.promiseSetSuccessMid) {
        Dbg("Relay: one or more netty method IDs missing");
        return false;
    }
    return true;
}

}

void RelayFilter_MarkBypass(JNIEnv* env, jobject packet) {
    if (!env || !packet) return;
    jobject gref = env->NewGlobalRef(packet);
    if (!gref) return;
    std::lock_guard<std::mutex> lock(g_bypassMu);

    if (g_bypassPending.size() > 256) {
        env->DeleteGlobalRef(g_bypassPending.front());
        g_bypassPending.erase(g_bypassPending.begin());
    }
    g_bypassPending.push_back(gref);
}

bool InstallRelayHandler(JNIEnv* env) {
    if (g_relay.valid()) return true;
    if (!env) return false;

    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader) return false;

    bool ok = defineRelayClass(env, mcLoader) && cacheNettyRefs(env, mcLoader);
    env->DeleteGlobalRef(mcLoader);
    return ok;
}

static void attachHandlerToPipeline(JNIEnv* env, jobject pipeline) {
    uint64_t seq = ++g_channelSeq;
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "proxy_relay_%llu",
                  static_cast<unsigned long long>(seq));
    jstring name = env->NewStringUTF(nameBuf);

    jobject handler = env->NewObject(g_relay.klass, g_relay.ctor);
    if (!handler || env->ExceptionCheck()) {
        LogTo("Attach: NewObject(RelayHandler) FAILED");
        env->ExceptionClear();
        env->DeleteLocalRef(name);
        return;
    }

    jstring base = env->NewStringUTF("packet_handler");
    jobject unused = env->CallObjectMethod(
        pipeline, g_relay.netty.addBeforeMid, base, name, handler);
    bool addBeforeFailed = env->ExceptionCheck();
    const char* mode;
    if (addBeforeFailed) {
        env->ExceptionClear();
        LogTo("Attach: addBefore(packet_handler,...) THREW, falling back to addFirst");
        unused = env->CallObjectMethod(
            pipeline, g_relay.netty.addFirstMid, name, handler);
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
    if (unused) env->DeleteLocalRef(unused);
    env->DeleteLocalRef(base);
    env->DeleteLocalRef(handler);
    env->DeleteLocalRef(name);
}

void RelayHandler_AttachToPipeline(JNIEnv* env, jobject ctx) {
    LogTo("Attach: entering ctx=%p", (void*)ctx);
    if (!ctx) return;
    if (!g_relay.valid()) {
        LogTo("Attach: g_relay invalid, installing");
        if (!InstallRelayHandler(env)) { LogTo("Attach: install failed"); return; }
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

void RelayHandler_AttachToPipelineObject(JNIEnv* env, jobject pipeline) {
    if (!pipeline) return;
    if (!g_relay.valid()) {
        if (!InstallRelayHandler(env)) { LogTo("AttachObj: install failed"); return; }
    }
    attachHandlerToPipeline(env, pipeline);
}
