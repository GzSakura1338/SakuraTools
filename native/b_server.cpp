#include "b_server.h"

#include "classfile.h"
#include "random_name.h"
#include "relay_handler.h"
#include "world_cache.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr const char* kInitSuper         = "io/netty/channel/ChannelInitializer";
constexpr const char* kHandlerSuper      = "io/netty/channel/ChannelInboundHandlerAdapter";

constexpr const char* kInitChannelDesc     = "(Lio/netty/channel/Channel;)V";
constexpr const char* kChannelReadDesc     = "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
constexpr const char* kChannelActiveDesc   = "(Lio/netty/channel/ChannelHandlerContext;)V";
constexpr const char* kChannelInactiveDesc = "(Lio/netty/channel/ChannelHandlerContext;)V";

enum class BState { AwaitHandshake, AwaitLogin, Play };

struct BServer {

    jclass    initClass    = nullptr;
    jmethodID initCtor     = nullptr;
    jclass    handlerClass = nullptr;
    jmethodID handlerCtor  = nullptr;
    jclass    mainGateClass = nullptr;
    jmethodID mainGateCtor  = nullptr;

    jclass    connectionCls              = nullptr;
    jmethodID connectionConfigureSerMid  = nullptr;
    jmethodID connectionSendMid          = nullptr;
    jfieldID  connectionAttrProtocolFid  = nullptr;

    jobject   protoHandshaking = nullptr;
    jobject   protoLogin       = nullptr;
    jobject   protoPlay        = nullptr;
    jobject   protoStatus      = nullptr;

    jclass    statusResponsePacketCls   = nullptr;
    jmethodID statusResponsePacketCtor  = nullptr;
    jclass    serverStatusCls           = nullptr;
    jmethodID serverStatusCtor          = nullptr;
    jclass    pongResponsePacketCls     = nullptr;
    jmethodID pongResponsePacketCtor    = nullptr;
    jclass    statusRequestPacketCls    = nullptr;
    jclass    pingRequestPacketCls      = nullptr;
    jfieldID  pingRequestPacketTimeFid  = nullptr;
    jfieldID  intentionPacketIntentFid  = nullptr;

    jobject   flowServerbound  = nullptr;
    jobject   flowClientbound  = nullptr;

    jclass    channelCls              = nullptr;
    jmethodID channelPipelineMid      = nullptr;
    jmethodID channelWriteAndFlushMid = nullptr;
    jmethodID channelAttrMid          = nullptr;
    jmethodID channelConfigMid        = nullptr;
    jmethodID channelCloseMid         = nullptr;
    jfieldID  connectionChannelFid    = nullptr;

    jmethodID configSetOptionMid      = nullptr;
    jobject   tcpNoDelayOption        = nullptr;
    jobject   booleanTrue             = nullptr;
    jclass    pipelineCls             = nullptr;
    jmethodID pipelineAddLastMid      = nullptr;
    jmethodID pipelineRemoveNameMid   = nullptr;
    jmethodID pipelineGetHandlerMid   = nullptr;
    jclass    attributeCls            = nullptr;
    jmethodID attributeSetMid         = nullptr;

    jclass    bundlerInfoCls          = nullptr;
    jfieldID  bundlerProviderFid      = nullptr;

    jclass    packetEncoderCls        = nullptr;
    jmethodID packetEncoderSetProtoMid = nullptr;
    jclass    packetDecoderCls        = nullptr;
    jmethodID packetDecoderSetProtoMid = nullptr;

    jclass    gameProfileCls          = nullptr;
    jmethodID gameProfileCtor         = nullptr;

    jclass    minecraftCls            = nullptr;
    jmethodID mcGetInstanceMid        = nullptr;
    jmethodID mcGetProfilePropsMid    = nullptr;
    jclass    friendlyBufCls          = nullptr;
    jmethodID friendlyBufCtor         = nullptr;
    jmethodID fbbWriteByteMid         = nullptr;
    jmethodID fbbWriteBooleanMid      = nullptr;
    jmethodID fbbWriteVarIntMid       = nullptr;
    jmethodID fbbWriteUUIDMid         = nullptr;
    jmethodID fbbWriteUtfMid          = nullptr;
    jmethodID fbbWriteGpPropsMid      = nullptr;
    jclass    unpooledCls             = nullptr;
    jmethodID unpooledBufferMid       = nullptr;
    jmethodID unpooledWrappedMid      = nullptr;
    jclass    playerInfoUpdatePacketCls     = nullptr;
    jmethodID playerInfoUpdatePacketBufCtor = nullptr;
    jmethodID playerInfoUpdatePacketWriteMid = nullptr;

    jmethodID piuEntriesMidA          = nullptr;
    jmethodID piuEntriesMidB          = nullptr;
    jclass    piEntryCls              = nullptr;
    jmethodID piEntryProfileIdMid     = nullptr;
    jmethodID piEntryGameModeMid      = nullptr;
    jmethodID piEntryListedMid        = nullptr;
    jmethodID piEntryLatencyMid       = nullptr;
    jmethodID piEntryDisplayNameMid   = nullptr;
    jmethodID gameTypeGetIdMid        = nullptr;
    jmethodID fbbWriteComponentMid    = nullptr;
    jmethodID listSizeMid             = nullptr;
    jmethodID listGetMid              = nullptr;
    jmethodID byteBufGetByteMid       = nullptr;
    jobject   bUuidObj                = nullptr;

    jclass    addPlayerPacketCls      = nullptr;
    jfieldID  addPlayerPacketUuidFid  = nullptr;

    jclass    customPayloadPacketCls  = nullptr;

    jclass    setPlayerTeamPacketCls  = nullptr;
    jmethodID setPlayerTeamPacketBufCtor = nullptr;
    jfieldID  setPlayerTeamMethodFid  = nullptr;
    jfieldID  setPlayerTeamNameFid    = nullptr;
    jfieldID  setPlayerTeamPlayersFid = nullptr;
    jmethodID collectionContainsMid   = nullptr;

    jmethodID userGetGameProfileMid   = nullptr;
    jmethodID gameProfileGetNameMid   = nullptr;
    jstring   aName                   = nullptr;
    jstring   bName                   = nullptr;

    jmethodID byteBufReadableBytesMid = nullptr;
    jmethodID byteBufReaderIndexMid   = nullptr;
    jmethodID byteBufGetBytesMid      = nullptr;

    jobject   aRealUuid               = nullptr;
    unsigned char aUuidBytes[16]      = {0};
    bool      aUuidReady              = false;
    unsigned char bUuidBytes[16]      = {0};
    bool      bUuidReady              = false;

    jclass    userCls                 = nullptr;
    jmethodID userGetProfileIdMid     = nullptr;
    jmethodID mcGetUserMid            = nullptr;

    jmethodID uuidGetMsbMid           = nullptr;
    jmethodID uuidGetLsbMid           = nullptr;

    jclass    loginFinishedPacketCls  = nullptr;
    jmethodID loginFinishedPacketCtor = nullptr;

    jclass    helloPacketCls          = nullptr;
    jfieldID  helloPacketNameFid      = nullptr;

    jclass    intentPacketCls         = nullptr;

    jclass    playerPositionPacketCls = nullptr;
    jmethodID playerPositionPacketCtor = nullptr;
    jclass    setCls                  = nullptr;
    jmethodID setOfMid                = nullptr;
    jclass    keepAlivePacketCls      = nullptr;
    jmethodID keepAlivePacketCtor     = nullptr;

    jclass    uuidCls                 = nullptr;
    jmethodID uuidNameUuidFromBytesMid = nullptr;

    jclass    bundlePacketCls         = nullptr;
    jmethodID bundleSubPacketsMid     = nullptr;
    jmethodID iterableIteratorMid     = nullptr;
    jmethodID iteratorHasNextMid      = nullptr;
    jmethodID iteratorNextMid         = nullptr;

    std::mutex targetMu;
    jobject   targetAConnection = nullptr;

    std::mutex bMu;
    jobject   bChannel = nullptr;

    std::atomic<BState> bState{BState::AwaitHandshake};

    bool bServerBound = false;

    std::atomic<bool> midSession{false};
    jmethodID mcGetConnectionMid      = nullptr;
    jclass    clientPacketListenerCls = nullptr;
    jmethodID cplGetConnectionMid     = nullptr;
    jmethodID cplLevelsMid            = nullptr;
    jmethodID cplRegistryAccessMid    = nullptr;
    jmethodID registryAccessFreezeMid = nullptr;
    jfieldID  mcPlayerFid             = nullptr;
    jfieldID  mcGameModeFid           = nullptr;
    jfieldID  mcLevelFid              = nullptr;
    jmethodID gameModeGetTypeMid      = nullptr;
    jclass    levelCls                = nullptr;
    jmethodID levelDimMidA            = nullptr;
    jmethodID levelDimMidB            = nullptr;
    jmethodID objToStringMid          = nullptr;
    jclass    loginPacketCls          = nullptr;
    jmethodID loginPacketCtor         = nullptr;
    jclass    optionalCls             = nullptr;
    jmethodID optionalEmptyMid        = nullptr;
};
BServer g_bs;

std::mutex              g_bConnMu;
std::condition_variable g_bConnCv;
bool                    g_bConnected = false;

jmethodID findMethodByDesc(jclass klass, const char* desc, bool wantStatic) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE) return nullptr;
    jmethodID hit = nullptr;
    for (jint i = 0; i < count && !hit; ++i) {
        char *n=nullptr, *s=nullptr, *g=nullptr;
        if (g_jvmti->GetMethodName(mids[i], &n, &s, &g) != JVMTI_ERROR_NONE) continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = mids[i];
            LogTo("  findMethodByDesc(%s, static=%d): '%s'", desc, wantStatic?1:0, n?n:"?");
        }
        if (n) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(n));
        if (s) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(mids));
    return hit;
}

int findMethodsByDesc(jclass klass, const char* desc, bool wantStatic,
                      jmethodID* out, int maxOut) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE) return 0;
    int n = 0;
    for (jint i = 0; i < count && n < maxOut; ++i) {
        char *nm=nullptr, *s=nullptr, *g=nullptr;
        if (g_jvmti->GetMethodName(mids[i], &nm, &s, &g) != JVMTI_ERROR_NONE) continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            out[n++] = mids[i];
            LogTo("  findMethodsByDesc(%s)[%d]: '%s'", desc, n-1, nm?nm:"?");
        }
        if (nm) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(nm));
        if (s)  g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g)  g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(mids));
    return n;
}

jmethodID findMethodByDescExcl(jclass klass, const char* desc, bool wantStatic,
                               const char* const* excl, int nExcl) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE) return nullptr;
    jmethodID hit = nullptr;
    for (jint i = 0; i < count && !hit; ++i) {
        char *nm=nullptr, *s=nullptr, *g=nullptr;
        if (g_jvmti->GetMethodName(mids[i], &nm, &s, &g) != JVMTI_ERROR_NONE) continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        bool excluded = false;
        for (int e = 0; nm && e < nExcl; ++e)
            if (std::strcmp(nm, excl[e]) == 0) { excluded = true; break; }
        if (!excluded && s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = mids[i];
            LogTo("  findMethodByDescExcl(%s): '%s'", desc, nm?nm:"?");
        }
        if (nm) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(nm));
        if (s)  g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g)  g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(mids));
    return hit;
}

jfieldID findFieldByDesc(jclass klass, const char* desc, bool wantStatic) {
    jint count = 0;
    jfieldID* fids = nullptr;
    if (g_jvmti->GetClassFields(klass, &count, &fids) != JVMTI_ERROR_NONE) return nullptr;
    jfieldID hit = nullptr;
    for (jint i = 0; i < count && !hit; ++i) {
        char *n=nullptr, *s=nullptr, *g=nullptr;
        if (g_jvmti->GetFieldName(klass, fids[i], &n, &s, &g) != JVMTI_ERROR_NONE) continue;
        jint mods = 0;
        g_jvmti->GetFieldModifiers(klass, fids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = fids[i];
            LogTo("  findFieldByDesc(%s, static=%d): '%s'", desc, wantStatic?1:0, n?n:"?");
        }
        if (n) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(n));
        if (s) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(fids));
    return hit;
}

jclass findLoadedBySig(JNIEnv* env, const char* sig) {
    jint count = 0;
    jclass* classes = nullptr;
    if (g_jvmti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE) return nullptr;
    jclass hit = nullptr;
    for (jint i = 0; i < count; ++i) {
        char* s = nullptr;
        if (g_jvmti->GetClassSignature(classes[i], &s, nullptr) != JVMTI_ERROR_NONE) continue;
        if (s && std::strcmp(s, sig) == 0) {
            hit = (jclass)env->NewLocalRef(classes[i]);
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
            break;
        }
        if (s) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
    }
    for (jint i = 0; i < count; ++i) env->DeleteLocalRef(classes[i]);
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
    return hit;
}

jclass loadOrFind(JNIEnv* env, jobject mcLoader, const char* dot, const char* sig) {
    jclass c = LoadClassInLoader(env, mcLoader, dot);
    if (!c) c = findLoadedBySig(env, sig);
    if (!c) LogTo("BServer: cannot find class %s", dot);
    return c;
}

std::string classNameForB(JNIEnv* env, jobject o) {
    if (!o || !g_jvmti) return {};
    jclass c = env->GetObjectClass(o);
    if (!c) return {};
    char* sig = nullptr;
    jvmtiError rc = g_jvmti->GetClassSignature(c, &sig, nullptr);
    env->DeleteLocalRef(c);
    if (rc != JVMTI_ERROR_NONE || !sig) return {};
    std::string out;
    const char* p = sig;
    if (*p == 'L') { ++p; for (; *p && *p != ';'; ++p) out.push_back(*p == '/' ? '.' : *p); }
    else out = sig;
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
    return out;
}

void setChannelAttr(JNIEnv* env, jobject channel, jfieldID keyFid, jclass keyOwnerCls,
                    jobject value) {
    jobject key = env->GetStaticObjectField(keyOwnerCls, keyFid);
    if (!key) return;
    jobject attr = env->CallObjectMethod(channel, g_bs.channelAttrMid, key);
    if (env->ExceptionCheck() || !attr) { env->ExceptionClear(); return; }
    env->CallVoidMethod(attr, g_bs.attributeSetMid, value);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(attr);
    env->DeleteLocalRef(key);
}

void setProtocolState(JNIEnv* env, jobject channel, jobject protoValue) {
    if (!channel || !protoValue) return;
    if (g_bs.connectionAttrProtocolFid)
        setChannelAttr(env, channel, g_bs.connectionAttrProtocolFid, g_bs.connectionCls, protoValue);
    if (g_bs.bundlerProviderFid && g_bs.bundlerInfoCls)
        setChannelAttr(env, channel, g_bs.bundlerProviderFid, g_bs.bundlerInfoCls, protoValue);

    jobject pipeline = env->CallObjectMethod(channel, g_bs.channelPipelineMid);
    if (!pipeline || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    for (const char* handlerName : {"encoder", "decoder"}) {
        jstring nm = env->NewStringUTF(handlerName);
        jobject handler = env->CallObjectMethod(pipeline, g_bs.pipelineGetHandlerMid, nm);
        env->DeleteLocalRef(nm);
        if (!handler || env->ExceptionCheck()) { env->ExceptionClear(); continue; }
        jclass hCls = env->GetObjectClass(handler);

        jmethodID sp = findMethodByDesc(hCls,
            "(Lnet/minecraft/network/ConnectionProtocol;)V", false);
        if (!sp) {
            sp = env->GetMethodID(hCls, "setProtocol",
                "(Lnet/minecraft/network/ConnectionProtocol;)V");
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (sp) {
            env->CallVoidMethod(handler, sp, protoValue);
            if (env->ExceptionCheck()) env->ExceptionClear();
            else LogTo("setProtocolState: called %s.setProtocol(PLAY)", handlerName);
        }
        env->DeleteLocalRef(hCls);
        env->DeleteLocalRef(handler);
    }
    env->DeleteLocalRef(pipeline);
}

void JNICALL Native_ServerInit_initChannel(JNIEnv* env, jobject , jobject ch) {
    LogTo("BServer: initChannel for incoming ch=%p", (void*)ch);

    if (g_bs.channelConfigMid && g_bs.configSetOptionMid &&
        g_bs.tcpNoDelayOption && g_bs.booleanTrue) {
        jobject cfg = env->CallObjectMethod(ch, g_bs.channelConfigMid);
        if (cfg && !env->ExceptionCheck()) {
            env->CallBooleanMethod(cfg, g_bs.configSetOptionMid,
                                   g_bs.tcpNoDelayOption, g_bs.booleanTrue);
            if (env->ExceptionCheck()) env->ExceptionClear();
            else LogTo("BServer: TCP_NODELAY set on B channel");
            env->DeleteLocalRef(cfg);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jobject pipeline = env->CallObjectMethod(ch, g_bs.channelPipelineMid);
    if (env->ExceptionCheck() || !pipeline) {
        env->ExceptionClear(); LogTo("  pipeline() failed"); return;
    }

    setProtocolState(env, ch, g_bs.protoHandshaking);

    env->CallStaticVoidMethod(g_bs.connectionCls, g_bs.connectionConfigureSerMid,
                              pipeline, g_bs.flowServerbound);
    if (env->ExceptionCheck()) { LogAndClearException(env, "  configureSerialization"); }

    bool haveBundlerInfo = (g_bs.bundlerProviderFid != nullptr &&
                            g_bs.bundlerInfoCls   != nullptr);
    if (!haveBundlerInfo) {
        for (const char* h : {"unbundler", "bundler"}) {
            jstring nm = env->NewStringUTF(h);
            env->CallObjectMethod(pipeline, g_bs.pipelineRemoveNameMid, nm);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(nm);
        }
        LogTo("  bundle handlers removed (no BundlerInfo)");
    } else {
        LogTo("  bundle handlers kept (BundlerInfo available)");
    }

    jobject handler = env->NewObject(g_bs.handlerClass, g_bs.handlerCtor);
    jstring name = env->NewStringUTF("bside");
    env->CallObjectMethod(pipeline, g_bs.pipelineAddLastMid, name, handler);
    if (env->ExceptionCheck()) LogAndClearException(env, "  addLast(bside)");
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(handler);
    env->DeleteLocalRef(pipeline);

    {
        std::lock_guard<std::mutex> l(g_bs.bMu);
        if (g_bs.bChannel) env->DeleteGlobalRef(g_bs.bChannel);
        g_bs.bChannel = env->NewGlobalRef(ch);
        g_bs.bState.store(BState::AwaitHandshake, std::memory_order_release);
    }

    LogTo("  B channel captured, state=AwaitHandshake");
}

void JNICALL Native_BSide_channelActive(JNIEnv* , jobject , jobject ) {
    LogTo("BServer: B channelActive");
}

void closeARemoteConnection(JNIEnv* env) {
    if (!g_bs.connectionChannelFid || !g_bs.channelCloseMid) return;
    jobject conn;
    {
        std::lock_guard<std::mutex> l(g_bs.targetMu);
        conn = g_bs.targetAConnection
                    ? env->NewLocalRef(g_bs.targetAConnection)
                    : nullptr;
    }
    if (!conn) { LogTo("[A-CLOSE] no target A connection cached; nothing to close"); return; }
    jobject aChannel = env->GetObjectField(conn, g_bs.connectionChannelFid);
    env->DeleteLocalRef(conn);
    if (!aChannel) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LogTo("[A-CLOSE] Connection.channel is null (not yet channelActive?)");
        return;
    }
    jobject future = env->CallObjectMethod(aChannel, g_bs.channelCloseMid);
    if (env->ExceptionCheck()) LogAndClearException(env, "[A-CLOSE] channel.close");
    else LogTo("[A-CLOSE] A's netty channel to remote server closed directly");
    if (future) env->DeleteLocalRef(future);
    env->DeleteLocalRef(aChannel);

    std::lock_guard<std::mutex> l(g_bs.targetMu);
    if (g_bs.targetAConnection) {
        env->DeleteGlobalRef(g_bs.targetAConnection);
        g_bs.targetAConnection = nullptr;
    }
}

void JNICALL Native_BSide_channelInactive(JNIEnv* env, jobject , jobject ) {
    LogTo("BServer: B channelInactive");
    {
        std::lock_guard<std::mutex> l(g_bs.bMu);
        if (g_bs.bChannel) { env->DeleteGlobalRef(g_bs.bChannel); g_bs.bChannel = nullptr; }
        g_bs.bState.store(BState::AwaitHandshake, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> gateLock(g_bConnMu);
        g_bConnected = false;
    }

    if (g_bs.midSession.load(std::memory_order_acquire)) {
        LogTo("BServer: B gone; mid-session — leaving A's connection intact, A resumes control");
    } else {

        closeARemoteConnection(env);
    }
}

void JNICALL Native_MainThreadGate_run(JNIEnv*, jobject ) {
    if (BServer_WaitForBConnected(0)) return;
    LogTo("[MAIN-GATE] blocking A Render thread until B reaches PLAY");
    BServer_WaitForBConnected(-1);
    LogTo("[MAIN-GATE] B reached PLAY; resuming A Render thread");
}

}
void BSide_OnPacket(JNIEnv* env, jobject ctx, jobject msg);
namespace {

void JNICALL Native_BSide_channelRead(JNIEnv* env, jobject , jobject ctx, jobject msg) {
    ::BSide_OnPacket(env, ctx, msg);
}

bool defineInitClass(JNIEnv* env, jobject mcLoader) {
    std::string simple   = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);

    ClassBuilder cb(internal, kInitSuper, 52);
    u2 superInit = cb.methodRef(kInitSuper, "<init>", "()V");
    std::vector<u1> ctor = {
        0x2A, 0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF), 0xB1
    };
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
    cb.addNativeMethod("initChannel", kInitChannelDesc, ACC_PUBLIC | ACC_NATIVE);
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader,
                                      reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) { LogAndClearException(env, "BServer/DefineInit"); return false; }
    JNINativeMethod nats[] = {
        {const_cast<char*>("initChannel"), const_cast<char*>(kInitChannelDesc),
         reinterpret_cast<void*>(&Native_ServerInit_initChannel)},
    };
    if (env->RegisterNatives(defined, nats, 1) != 0) {
        LogAndClearException(env, "BServer/RegisterInit"); env->DeleteLocalRef(defined); return false;
    }
    g_bs.initCtor = env->GetMethodID(defined, "<init>", "()V");
    g_bs.initClass = static_cast<jclass>(env->NewGlobalRef(defined));
    env->DeleteLocalRef(defined);
    LogTo("BServer: defined ServerChannelInit as %s", internal.c_str());
    return true;
}

bool defineHandlerClass(JNIEnv* env, jobject mcLoader) {
    std::string simple   = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);

    ClassBuilder cb(internal, kHandlerSuper, 52);
    u2 superInit = cb.methodRef(kHandlerSuper, "<init>", "()V");
    std::vector<u1> ctor = {
        0x2A, 0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF), 0xB1
    };
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
    cb.addNativeMethod("channelActive",   kChannelActiveDesc,   ACC_PUBLIC | ACC_NATIVE);
    cb.addNativeMethod("channelInactive", kChannelInactiveDesc, ACC_PUBLIC | ACC_NATIVE);
    cb.addNativeMethod("channelRead",     kChannelReadDesc,     ACC_PUBLIC | ACC_NATIVE);
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader,
                                      reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) { LogAndClearException(env, "BServer/DefineHandler"); return false; }
    JNINativeMethod nats[] = {
        {const_cast<char*>("channelActive"),   const_cast<char*>(kChannelActiveDesc),
         reinterpret_cast<void*>(&Native_BSide_channelActive)},
        {const_cast<char*>("channelInactive"), const_cast<char*>(kChannelInactiveDesc),
         reinterpret_cast<void*>(&Native_BSide_channelInactive)},
        {const_cast<char*>("channelRead"),     const_cast<char*>(kChannelReadDesc),
         reinterpret_cast<void*>(&Native_BSide_channelRead)},
    };
    if (env->RegisterNatives(defined, nats, 3) != 0) {
        LogAndClearException(env, "BServer/RegisterHandler"); env->DeleteLocalRef(defined); return false;
    }
    g_bs.handlerCtor = env->GetMethodID(defined, "<init>", "()V");
    g_bs.handlerClass = static_cast<jclass>(env->NewGlobalRef(defined));
    env->DeleteLocalRef(defined);
    LogTo("BServer: defined BSideHandler as %s", internal.c_str());
    return true;
}

bool defineMainGateClass(JNIEnv* env, jobject mcLoader) {
    if (g_bs.mainGateClass && g_bs.mainGateCtor) return true;

    std::string simple   = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);

    ClassBuilder cb(internal, "java/lang/Thread", 52);
    u2 superInit = cb.methodRef("java/lang/Thread", "<init>", "()V");
    std::vector<u1> ctor = {
        0x2A, 0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF), 0xB1
    };
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
    cb.addNativeMethod("run", "()V", ACC_PUBLIC | ACC_NATIVE);
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader,
                                      reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "BServer/DefineMainGate");
        return false;
    }
    JNINativeMethod nats[] = {
        {const_cast<char*>("run"), const_cast<char*>("()V"),
         reinterpret_cast<void*>(&Native_MainThreadGate_run)},
    };
    if (env->RegisterNatives(defined, nats, 1) != 0) {
        LogAndClearException(env, "BServer/RegisterMainGate");
        env->DeleteLocalRef(defined);
        return false;
    }

    g_bs.mainGateCtor = env->GetMethodID(defined, "<init>", "()V");
    g_bs.mainGateClass = static_cast<jclass>(env->NewGlobalRef(defined));
    env->DeleteLocalRef(defined);
    if (!g_bs.mainGateCtor || !g_bs.mainGateClass) {
        LogAndClearException(env, "BServer/MainGateCtor");
        return false;
    }
    LogTo("BServer: defined A main-thread gate as %s", internal.c_str());
    return true;
}

bool cacheJavaRefs(JNIEnv* env, jobject mcLoader) {

    jclass connCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.Connection", "Lnet/minecraft/network/Connection;");
    if (!connCls) return false;
    g_bs.connectionCls = static_cast<jclass>(env->NewGlobalRef(connCls));
    g_bs.connectionConfigureSerMid = findMethodByDesc(connCls,
        "(Lio/netty/channel/ChannelPipeline;Lnet/minecraft/network/protocol/PacketFlow;)V", true);
    g_bs.connectionSendMid = findMethodByDesc(connCls,
        "(Lnet/minecraft/network/protocol/Packet;)V", false);
    g_bs.connectionAttrProtocolFid = findFieldByDesc(connCls,
        "Lio/netty/util/AttributeKey;", true);

    g_bs.connectionChannelFid = findFieldByDesc(connCls,
        "Lio/netty/channel/Channel;", false);
    env->DeleteLocalRef(connCls);

    jclass bundlerCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.BundlerInfo", "Lnet/minecraft/network/protocol/BundlerInfo;");
    if (bundlerCls) {
        g_bs.bundlerInfoCls = static_cast<jclass>(env->NewGlobalRef(bundlerCls));
        g_bs.bundlerProviderFid = findFieldByDesc(bundlerCls, "Lio/netty/util/AttributeKey;", true);
        env->DeleteLocalRef(bundlerCls);
    } else {
        LogTo("BServer: BundlerInfo not found (bundle attr not set — may cause issues on modern servers)");
    }

    jclass protoCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.ConnectionProtocol", "Lnet/minecraft/network/ConnectionProtocol;");
    if (protoCls) {
        auto readEnum = [&](const char* n) -> jobject {
            jfieldID f = env->GetStaticFieldID(protoCls, n, "Lnet/minecraft/network/ConnectionProtocol;");
            if (!f) return nullptr;
            jobject v = env->GetStaticObjectField(protoCls, f);
            return v ? env->NewGlobalRef(v) : nullptr;
        };
        g_bs.protoHandshaking = readEnum("HANDSHAKING");
        g_bs.protoLogin       = readEnum("LOGIN");
        g_bs.protoPlay        = readEnum("PLAY");
        g_bs.protoStatus      = readEnum("STATUS");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(protoCls);
    }

    jclass flowCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.PacketFlow", "Lnet/minecraft/network/protocol/PacketFlow;");
    if (!flowCls) return false;
    jfieldID fSB = env->GetStaticFieldID(flowCls, "SERVERBOUND", "Lnet/minecraft/network/protocol/PacketFlow;");
    jfieldID fCB = env->GetStaticFieldID(flowCls, "CLIENTBOUND", "Lnet/minecraft/network/protocol/PacketFlow;");
    if (fSB) g_bs.flowServerbound = env->NewGlobalRef(env->GetStaticObjectField(flowCls, fSB));
    if (fCB) g_bs.flowClientbound = env->NewGlobalRef(env->GetStaticObjectField(flowCls, fCB));
    env->DeleteLocalRef(flowCls);

    jclass chCls = loadOrFind(env, mcLoader, "io.netty.channel.Channel", "Lio/netty/channel/Channel;");
    if (!chCls) return false;
    g_bs.channelCls = static_cast<jclass>(env->NewGlobalRef(chCls));
    g_bs.channelPipelineMid      = env->GetMethodID(chCls, "pipeline",      "()Lio/netty/channel/ChannelPipeline;");
    g_bs.channelWriteAndFlushMid = env->GetMethodID(chCls, "writeAndFlush", "(Ljava/lang/Object;)Lio/netty/channel/ChannelFuture;");
    g_bs.channelAttrMid          = env->GetMethodID(chCls, "attr",          "(Lio/netty/util/AttributeKey;)Lio/netty/util/Attribute;");
    g_bs.channelConfigMid        = env->GetMethodID(chCls, "config",        "()Lio/netty/channel/ChannelConfig;");
    g_bs.channelCloseMid         = env->GetMethodID(chCls, "close",         "()Lio/netty/channel/ChannelFuture;");
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(chCls);

    jclass cfgCls = loadOrFind(env, mcLoader, "io.netty.channel.ChannelConfig",
                               "Lio/netty/channel/ChannelConfig;");
    if (cfgCls) {
        g_bs.configSetOptionMid = env->GetMethodID(cfgCls, "setOption",
            "(Lio/netty/channel/ChannelOption;Ljava/lang/Object;)Z");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cfgCls);
    }
    jclass optCls = loadOrFind(env, mcLoader, "io.netty.channel.ChannelOption",
                               "Lio/netty/channel/ChannelOption;");
    if (optCls) {
        jfieldID f = env->GetStaticFieldID(optCls, "TCP_NODELAY", "Lio/netty/channel/ChannelOption;");
        if (f) {
            jobject v = env->GetStaticObjectField(optCls, f);
            if (v) g_bs.tcpNoDelayOption = env->NewGlobalRef(v);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(optCls);
    }
    jclass boolCls = env->FindClass("java/lang/Boolean");
    if (boolCls) {
        jfieldID f = env->GetStaticFieldID(boolCls, "TRUE", "Ljava/lang/Boolean;");
        if (f) {
            jobject v = env->GetStaticObjectField(boolCls, f);
            if (v) g_bs.booleanTrue = env->NewGlobalRef(v);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(boolCls);
    }

    jclass pipCls = loadOrFind(env, mcLoader, "io.netty.channel.ChannelPipeline",
                               "Lio/netty/channel/ChannelPipeline;");
    if (!pipCls) return false;
    g_bs.pipelineCls = static_cast<jclass>(env->NewGlobalRef(pipCls));
    g_bs.pipelineAddLastMid = env->GetMethodID(pipCls, "addLast",
        "(Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
    g_bs.pipelineRemoveNameMid = env->GetMethodID(pipCls, "remove",
        "(Ljava/lang/String;)Lio/netty/channel/ChannelHandler;");
    g_bs.pipelineGetHandlerMid = env->GetMethodID(pipCls, "get",
        "(Ljava/lang/String;)Lio/netty/channel/ChannelHandler;");
    env->DeleteLocalRef(pipCls);

    jclass attrCls = loadOrFind(env, mcLoader, "io.netty.util.Attribute", "Lio/netty/util/Attribute;");
    if (!attrCls) return false;
    g_bs.attributeCls = static_cast<jclass>(env->NewGlobalRef(attrCls));
    g_bs.attributeSetMid = env->GetMethodID(attrCls, "set", "(Ljava/lang/Object;)V");
    env->DeleteLocalRef(attrCls);

    jclass gpCls = loadOrFind(env, mcLoader, "com.mojang.authlib.GameProfile",
                              "Lcom/mojang/authlib/GameProfile;");
    if (gpCls) {
        g_bs.gameProfileCls = static_cast<jclass>(env->NewGlobalRef(gpCls));
        g_bs.gameProfileCtor = env->GetMethodID(gpCls, "<init>", "(Ljava/util/UUID;Ljava/lang/String;)V");

        g_bs.gameProfileGetNameMid = env->GetMethodID(gpCls, "getName", "()Ljava/lang/String;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(gpCls);
    }

    jclass mcCls = loadOrFind(env, mcLoader, "net.minecraft.client.Minecraft",
                              "Lnet/minecraft/client/Minecraft;");
    if (mcCls) {
        g_bs.minecraftCls = static_cast<jclass>(env->NewGlobalRef(mcCls));
        g_bs.mcGetInstanceMid = findMethodByDesc(mcCls, "()Lnet/minecraft/client/Minecraft;", true);
        g_bs.mcGetProfilePropsMid = findMethodByDesc(mcCls,
            "()Lcom/mojang/authlib/properties/PropertyMap;", false);
        g_bs.mcGetUserMid = findMethodByDesc(mcCls,
            "()Lnet/minecraft/client/User;", false);
        env->DeleteLocalRef(mcCls);
    }
    jclass userCls = loadOrFind(env, mcLoader, "net.minecraft.client.User",
                                "Lnet/minecraft/client/User;");
    if (userCls) {
        g_bs.userCls = static_cast<jclass>(env->NewGlobalRef(userCls));
        g_bs.userGetProfileIdMid = findMethodByDesc(userCls,
            "()Ljava/util/UUID;", false);

        g_bs.userGetGameProfileMid = findMethodByDesc(userCls,
            "()Lcom/mojang/authlib/GameProfile;", false);
        env->DeleteLocalRef(userCls);
    }
    jclass fbbCls = loadOrFind(env, mcLoader, "net.minecraft.network.FriendlyByteBuf",
                               "Lnet/minecraft/network/FriendlyByteBuf;");
    if (fbbCls) {
        g_bs.friendlyBufCls = static_cast<jclass>(env->NewGlobalRef(fbbCls));
        g_bs.friendlyBufCtor = env->GetMethodID(fbbCls, "<init>", "(Lio/netty/buffer/ByteBuf;)V");

        g_bs.fbbWriteByteMid    = env->GetMethodID(fbbCls, "writeByte",    "(I)Lio/netty/buffer/ByteBuf;");
        g_bs.fbbWriteBooleanMid = env->GetMethodID(fbbCls, "writeBoolean", "(Z)Lio/netty/buffer/ByteBuf;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        g_bs.fbbWriteVarIntMid = findMethodByDesc(fbbCls,
            "(I)Lnet/minecraft/network/FriendlyByteBuf;", false);
        g_bs.fbbWriteUUIDMid = findMethodByDesc(fbbCls,
            "(Ljava/util/UUID;)Lnet/minecraft/network/FriendlyByteBuf;", false);
        g_bs.fbbWriteUtfMid = findMethodByDesc(fbbCls,
            "(Ljava/lang/String;I)Lnet/minecraft/network/FriendlyByteBuf;", false);
        g_bs.fbbWriteGpPropsMid = findMethodByDesc(fbbCls,
            "(Lcom/mojang/authlib/properties/PropertyMap;)V", false);
        env->DeleteLocalRef(fbbCls);
    }
    jclass byteBufCls = loadOrFind(env, mcLoader, "io.netty.buffer.ByteBuf",
                                   "Lio/netty/buffer/ByteBuf;");
    if (byteBufCls) {
        g_bs.byteBufReadableBytesMid = env->GetMethodID(byteBufCls, "readableBytes",  "()I");
        g_bs.byteBufReaderIndexMid   = env->GetMethodID(byteBufCls, "readerIndex",    "()I");
        g_bs.byteBufGetBytesMid      = env->GetMethodID(byteBufCls, "getBytes",       "(I[B)Lio/netty/buffer/ByteBuf;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(byteBufCls);
    }
    jclass unpCls = loadOrFind(env, mcLoader, "io.netty.buffer.Unpooled",
                               "Lio/netty/buffer/Unpooled;");
    if (unpCls) {
        g_bs.unpooledCls = static_cast<jclass>(env->NewGlobalRef(unpCls));
        g_bs.unpooledBufferMid  = env->GetStaticMethodID(unpCls, "buffer",        "()Lio/netty/buffer/ByteBuf;");
        g_bs.unpooledWrappedMid = env->GetStaticMethodID(unpCls, "wrappedBuffer", "([B)Lio/netty/buffer/ByteBuf;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(unpCls);
    }
    jclass piuCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket",
        "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket;");
    if (piuCls) {
        g_bs.playerInfoUpdatePacketCls = static_cast<jclass>(env->NewGlobalRef(piuCls));
        g_bs.playerInfoUpdatePacketBufCtor = env->GetMethodID(piuCls, "<init>",
            "(Lnet/minecraft/network/FriendlyByteBuf;)V");

        g_bs.playerInfoUpdatePacketWriteMid = findMethodByDesc(piuCls,
            "(Lnet/minecraft/network/FriendlyByteBuf;)V", false);

        jmethodID listMids[2] = {nullptr, nullptr};
        int nList = findMethodsByDesc(piuCls, "()Ljava/util/List;", false, listMids, 2);
        g_bs.piuEntriesMidA = listMids[0];
        g_bs.piuEntriesMidB = listMids[1];
        LogTo("  piu: %d ()List accessor(s) cached", nList);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(piuCls);
    }

    jclass piEntryCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Entry",
        "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket$Entry;");
    if (piEntryCls) {
        g_bs.piEntryCls = static_cast<jclass>(env->NewGlobalRef(piEntryCls));
        g_bs.piEntryProfileIdMid   = findMethodByDesc(piEntryCls, "()Ljava/util/UUID;", false);
        g_bs.piEntryGameModeMid     = findMethodByDesc(piEntryCls,
            "()Lnet/minecraft/world/level/GameType;", false);
        g_bs.piEntryListedMid       = findMethodByDesc(piEntryCls, "()Z", false);
        static const char* const kHashExcl[] = { "hashCode" };
        g_bs.piEntryLatencyMid      = findMethodByDescExcl(piEntryCls, "()I", false, kHashExcl, 1);
        g_bs.piEntryDisplayNameMid  = findMethodByDesc(piEntryCls,
            "()Lnet/minecraft/network/chat/Component;", false);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(piEntryCls);
    }

    jclass gameTypeCls = loadOrFind(env, mcLoader, "net.minecraft.world.level.GameType",
                                    "Lnet/minecraft/world/level/GameType;");
    if (gameTypeCls) {
        static const char* const kIdExcl[] = { "ordinal", "hashCode" };
        g_bs.gameTypeGetIdMid = findMethodByDescExcl(gameTypeCls, "()I", false, kIdExcl, 2);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(gameTypeCls);
    }

    if (g_bs.friendlyBufCls) {
        g_bs.fbbWriteComponentMid = findMethodByDesc(g_bs.friendlyBufCls,
            "(Lnet/minecraft/network/chat/Component;)Lnet/minecraft/network/FriendlyByteBuf;", false);
    }

    jclass listCls = env->FindClass("java/util/List");
    if (listCls) {
        g_bs.listSizeMid = env->GetMethodID(listCls, "size", "()I");
        g_bs.listGetMid  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(listCls);
    }

    {
        jclass bbCls = loadOrFind(env, mcLoader, "io.netty.buffer.ByteBuf",
                                  "Lio/netty/buffer/ByteBuf;");
        if (bbCls) {
            g_bs.byteBufGetByteMid = env->GetMethodID(bbCls, "getByte", "(I)B");
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(bbCls);
        }
    }

    jclass appCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundAddPlayerPacket",
        "Lnet/minecraft/network/protocol/game/ClientboundAddPlayerPacket;");
    if (appCls) {
        g_bs.addPlayerPacketCls = static_cast<jclass>(env->NewGlobalRef(appCls));
        g_bs.addPlayerPacketUuidFid = findFieldByDesc(appCls, "Ljava/util/UUID;", false);
        env->DeleteLocalRef(appCls);
    }

    jclass cpp = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundCustomPayloadPacket",
        "Lnet/minecraft/network/protocol/game/ClientboundCustomPayloadPacket;");
    if (cpp) {
        g_bs.customPayloadPacketCls = static_cast<jclass>(env->NewGlobalRef(cpp));
        env->DeleteLocalRef(cpp);
    }

    jclass sptCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundSetPlayerTeamPacket",
        "Lnet/minecraft/network/protocol/game/ClientboundSetPlayerTeamPacket;");
    if (sptCls) {
        g_bs.setPlayerTeamPacketCls = static_cast<jclass>(env->NewGlobalRef(sptCls));
        g_bs.setPlayerTeamPacketBufCtor = env->GetMethodID(sptCls, "<init>",
            "(Lnet/minecraft/network/FriendlyByteBuf;)V");
        g_bs.setPlayerTeamMethodFid  = findFieldByDesc(sptCls, "I", false);
        g_bs.setPlayerTeamNameFid    = findFieldByDesc(sptCls, "Ljava/lang/String;", false);
        g_bs.setPlayerTeamPlayersFid = findFieldByDesc(sptCls, "Ljava/util/Collection;", false);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(sptCls);
    }
    jclass collCls = env->FindClass("java/util/Collection");
    if (collCls) {
        g_bs.collectionContainsMid = env->GetMethodID(collCls, "contains", "(Ljava/lang/Object;)Z");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(collCls);
    }

    jclass uuidClsL = env->FindClass("java/util/UUID");
    if (uuidClsL) {
        g_bs.uuidGetMsbMid = env->GetMethodID(uuidClsL, "getMostSignificantBits",  "()J");
        g_bs.uuidGetLsbMid = env->GetMethodID(uuidClsL, "getLeastSignificantBits", "()J");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(uuidClsL);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass lfp = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.login.ClientboundGameProfilePacket",
        "Lnet/minecraft/network/protocol/login/ClientboundGameProfilePacket;");
    if (lfp) {
        g_bs.loginFinishedPacketCls = static_cast<jclass>(env->NewGlobalRef(lfp));

        g_bs.loginFinishedPacketCtor = findMethodByDesc(lfp,
            "(Lcom/mojang/authlib/GameProfile;)V", false);
        env->DeleteLocalRef(lfp);
    }

    jclass hello = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.login.ServerboundHelloPacket",
        "Lnet/minecraft/network/protocol/login/ServerboundHelloPacket;");
    if (hello) {
        g_bs.helloPacketCls = static_cast<jclass>(env->NewGlobalRef(hello));
        g_bs.helloPacketNameFid = findFieldByDesc(hello, "Ljava/lang/String;", false);
        env->DeleteLocalRef(hello);
    }

    jclass intent = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.handshake.ClientIntentionPacket",
        "Lnet/minecraft/network/protocol/handshake/ClientIntentionPacket;");
    if (intent) {
        g_bs.intentPacketCls = static_cast<jclass>(env->NewGlobalRef(intent));
        g_bs.intentionPacketIntentFid = findFieldByDesc(intent,
            "Lnet/minecraft/network/ConnectionProtocol;", false);
        env->DeleteLocalRef(intent);
    }

    jclass sReq = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.status.ServerboundStatusRequestPacket",
        "Lnet/minecraft/network/protocol/status/ServerboundStatusRequestPacket;");
    if (sReq) { g_bs.statusRequestPacketCls = static_cast<jclass>(env->NewGlobalRef(sReq)); env->DeleteLocalRef(sReq); }

    jclass pReq = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.status.ServerboundPingRequestPacket",
        "Lnet/minecraft/network/protocol/status/ServerboundPingRequestPacket;");
    if (pReq) {
        g_bs.pingRequestPacketCls = static_cast<jclass>(env->NewGlobalRef(pReq));
        g_bs.pingRequestPacketTimeFid = findFieldByDesc(pReq, "J", false);
        env->DeleteLocalRef(pReq);
    }

    jclass pong = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.status.ClientboundPongResponsePacket",
        "Lnet/minecraft/network/protocol/status/ClientboundPongResponsePacket;");
    if (pong) {
        g_bs.pongResponsePacketCls = static_cast<jclass>(env->NewGlobalRef(pong));
        g_bs.pongResponsePacketCtor = findMethodByDesc(pong, "(J)V", false);
        env->DeleteLocalRef(pong);
    }

    jclass sResp = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.status.ClientboundStatusResponsePacket",
        "Lnet/minecraft/network/protocol/status/ClientboundStatusResponsePacket;");
    if (sResp) {
        g_bs.statusResponsePacketCls = static_cast<jclass>(env->NewGlobalRef(sResp));

        g_bs.statusResponsePacketCtor = findMethodByDesc(sResp,
            "(Lnet/minecraft/network/protocol/status/ServerStatus;)V", false);
        env->DeleteLocalRef(sResp);
    }
    jclass ss = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.status.ServerStatus",
        "Lnet/minecraft/network/protocol/status/ServerStatus;");
    if (ss) {
        g_bs.serverStatusCls = static_cast<jclass>(env->NewGlobalRef(ss));

        env->DeleteLocalRef(ss);
    }

    jclass pp = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundPlayerPositionPacket",
        "Lnet/minecraft/network/protocol/game/ClientboundPlayerPositionPacket;");
    if (pp) {
        g_bs.playerPositionPacketCls = static_cast<jclass>(env->NewGlobalRef(pp));
        g_bs.playerPositionPacketCtor = findMethodByDesc(pp,
            "(DDDFFLjava/util/Set;I)V", false);
        env->DeleteLocalRef(pp);
    }

    jclass setC = env->FindClass("java/util/Set");
    if (setC) {
        g_bs.setCls = static_cast<jclass>(env->NewGlobalRef(setC));
        g_bs.setOfMid = env->GetStaticMethodID(setC, "of", "()Ljava/util/Set;");
        env->DeleteLocalRef(setC);
    }

    jclass kap = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundKeepAlivePacket",
        "Lnet/minecraft/network/protocol/game/ClientboundKeepAlivePacket;");
    if (kap) {
        g_bs.keepAlivePacketCls = static_cast<jclass>(env->NewGlobalRef(kap));
        g_bs.keepAlivePacketCtor = findMethodByDesc(kap, "(J)V", false);
        env->DeleteLocalRef(kap);
    }

    jclass cbp = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundBundlePacket",
        "Lnet/minecraft/network/protocol/game/ClientboundBundlePacket;");
    if (cbp) {
        g_bs.bundlePacketCls = static_cast<jclass>(env->NewGlobalRef(cbp));
        jclass bp = loadOrFind(env, mcLoader,
            "net.minecraft.network.protocol.BundlePacket",
            "Lnet/minecraft/network/protocol/BundlePacket;");
        if (bp) {
            g_bs.bundleSubPacketsMid = findMethodByDesc(bp, "()Ljava/lang/Iterable;", false);
            env->DeleteLocalRef(bp);
        }
        env->DeleteLocalRef(cbp);
    } else {
        LogTo("BServer: ClientboundBundlePacket not found — bundles won't be expanded");
    }
    jclass iterableCls = env->FindClass("java/lang/Iterable");
    if (iterableCls) {
        g_bs.iterableIteratorMid = env->GetMethodID(iterableCls, "iterator", "()Ljava/util/Iterator;");
        env->DeleteLocalRef(iterableCls);
    }
    jclass iteratorCls = env->FindClass("java/util/Iterator");
    if (iteratorCls) {
        g_bs.iteratorHasNextMid = env->GetMethodID(iteratorCls, "hasNext", "()Z");
        g_bs.iteratorNextMid    = env->GetMethodID(iteratorCls, "next", "()Ljava/lang/Object;");
        env->DeleteLocalRef(iteratorCls);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass uuidCls = env->FindClass("java/util/UUID");
    if (uuidCls) {
        g_bs.uuidCls = static_cast<jclass>(env->NewGlobalRef(uuidCls));
        g_bs.uuidNameUuidFromBytesMid = env->GetStaticMethodID(uuidCls,
            "nameUUIDFromBytes", "([B)Ljava/util/UUID;");
        env->DeleteLocalRef(uuidCls);
    }

    LogTo("cacheJavaRefs: configureSer=%p send=%p attrProtoFid=%p bundlerFid=%p "
          "protoHS=%p protoLOGIN=%p protoPLAY=%p flowSB=%p pipMid=%p addLast=%p "
          "attrSet=%p gpCtor=%p lfpCtor=%p helloName=%p uuidFromBytes=%p",
          (void*)g_bs.connectionConfigureSerMid, (void*)g_bs.connectionSendMid,
          (void*)g_bs.connectionAttrProtocolFid, (void*)g_bs.bundlerProviderFid,
          (void*)g_bs.protoHandshaking, (void*)g_bs.protoLogin, (void*)g_bs.protoPlay,
          (void*)g_bs.flowServerbound, (void*)g_bs.channelPipelineMid,
          (void*)g_bs.pipelineAddLastMid, (void*)g_bs.attributeSetMid,
          (void*)g_bs.gameProfileCtor, (void*)g_bs.loginFinishedPacketCtor,
          (void*)g_bs.helloPacketNameFid, (void*)g_bs.uuidNameUuidFromBytesMid);
    if (env->ExceptionCheck()) env->ExceptionClear();

    if (g_bs.minecraftCls) {
        g_bs.mcGetConnectionMid = findMethodByDesc(g_bs.minecraftCls,
            "()Lnet/minecraft/client/multiplayer/ClientPacketListener;", false);
        g_bs.mcPlayerFid   = findFieldByDesc(g_bs.minecraftCls,
            "Lnet/minecraft/client/player/LocalPlayer;", false);
        g_bs.mcGameModeFid = findFieldByDesc(g_bs.minecraftCls,
            "Lnet/minecraft/client/multiplayer/MultiPlayerGameMode;", false);
        g_bs.mcLevelFid    = findFieldByDesc(g_bs.minecraftCls,
            "Lnet/minecraft/client/multiplayer/ClientLevel;", false);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    jclass cplCls = loadOrFind(env, mcLoader,
        "net.minecraft.client.multiplayer.ClientPacketListener",
        "Lnet/minecraft/client/multiplayer/ClientPacketListener;");
    if (cplCls) {
        g_bs.clientPacketListenerCls = static_cast<jclass>(env->NewGlobalRef(cplCls));
        g_bs.cplGetConnectionMid  = findMethodByDesc(cplCls, "()Lnet/minecraft/network/Connection;", false);
        g_bs.cplLevelsMid         = findMethodByDesc(cplCls, "()Ljava/util/Set;", false);
        g_bs.cplRegistryAccessMid = findMethodByDesc(cplCls, "()Lnet/minecraft/core/RegistryAccess;", false);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cplCls);
    }
    jclass raCls = loadOrFind(env, mcLoader, "net.minecraft.core.RegistryAccess",
                              "Lnet/minecraft/core/RegistryAccess;");
    if (raCls) {
        g_bs.registryAccessFreezeMid = findMethodByDesc(raCls,
            "()Lnet/minecraft/core/RegistryAccess$Frozen;", false);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(raCls);
    }
    jclass mpgmCls = loadOrFind(env, mcLoader,
        "net.minecraft.client.multiplayer.MultiPlayerGameMode",
        "Lnet/minecraft/client/multiplayer/MultiPlayerGameMode;");
    if (mpgmCls) {

        g_bs.gameModeGetTypeMid = findMethodByDesc(mpgmCls,
            "()Lnet/minecraft/world/level/GameType;", false);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(mpgmCls);
    }
    jclass lvlCls = loadOrFind(env, mcLoader, "net.minecraft.world.level.Level",
                               "Lnet/minecraft/world/level/Level;");
    if (lvlCls) {
        g_bs.levelCls = static_cast<jclass>(env->NewGlobalRef(lvlCls));

        jmethodID dimMids[2] = {nullptr, nullptr};
        int nDim = findMethodsByDesc(lvlCls, "()Lnet/minecraft/resources/ResourceKey;", false, dimMids, 2);
        g_bs.levelDimMidA = dimMids[0];
        g_bs.levelDimMidB = dimMids[1];
        LogTo("  level: %d ()ResourceKey method(s)", nDim);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(lvlCls);
    }
    jclass objCls = env->FindClass("java/lang/Object");
    if (objCls) {
        g_bs.objToStringMid = env->GetMethodID(objCls, "toString", "()Ljava/lang/String;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(objCls);
    }
    jclass optionalClsL = env->FindClass("java/util/Optional");
    if (optionalClsL) {
        g_bs.optionalCls = static_cast<jclass>(env->NewGlobalRef(optionalClsL));
        g_bs.optionalEmptyMid = env->GetStaticMethodID(optionalClsL, "empty", "()Ljava/util/Optional;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(optionalClsL);
    }
    jclass loginCls = loadOrFind(env, mcLoader,
        "net.minecraft.network.protocol.game.ClientboundLoginPacket",
        "Lnet/minecraft/network/protocol/game/ClientboundLoginPacket;");
    if (loginCls) {
        g_bs.loginPacketCls = static_cast<jclass>(env->NewGlobalRef(loginCls));

        g_bs.loginPacketCtor = env->GetMethodID(loginCls, "<init>",
            "(IZLnet/minecraft/world/level/GameType;Lnet/minecraft/world/level/GameType;"
            "Ljava/util/Set;Lnet/minecraft/core/RegistryAccess$Frozen;"
            "Lnet/minecraft/resources/ResourceKey;Lnet/minecraft/resources/ResourceKey;"
            "JIIIZZZZLjava/util/Optional;I)V");
        if (env->ExceptionCheck()) env->ExceptionClear();
        LogTo("  login ctor %s", g_bs.loginPacketCtor ? "resolved" : "MISSING");
        env->DeleteLocalRef(loginCls);
    }

    return g_bs.connectionConfigureSerMid && g_bs.connectionSendMid &&
           g_bs.flowServerbound && g_bs.channelPipelineMid && g_bs.pipelineAddLastMid &&
           g_bs.channelWriteAndFlushMid && g_bs.channelAttrMid && g_bs.attributeSetMid;
}

bool bindServer(JNIEnv* env, jobject mcLoader) {
    jclass elgCls  = loadOrFind(env, mcLoader, "io.netty.channel.nio.NioEventLoopGroup",
                                "Lio/netty/channel/nio/NioEventLoopGroup;");
    jclass sbCls   = loadOrFind(env, mcLoader, "io.netty.bootstrap.ServerBootstrap",
                                "Lio/netty/bootstrap/ServerBootstrap;");
    jclass sscCls  = loadOrFind(env, mcLoader, "io.netty.channel.socket.nio.NioServerSocketChannel",
                                "Lio/netty/channel/socket/nio/NioServerSocketChannel;");
    jclass isaCls  = env->FindClass("java/net/InetSocketAddress");
    if (!elgCls || !sbCls || !sscCls || !isaCls) return false;

    jobject elg = env->NewObject(elgCls, env->GetMethodID(elgCls, "<init>", "()V"));
    if (env->ExceptionCheck()) { LogAndClearException(env, "NioELG"); return false; }

    jobject sb  = env->NewObject(sbCls, env->GetMethodID(sbCls, "<init>", "()V"));
    env->CallObjectMethod(sb,
        env->GetMethodID(sbCls, "group",
                         "(Lio/netty/channel/EventLoopGroup;)Lio/netty/bootstrap/ServerBootstrap;"),
        elg);
    env->CallObjectMethod(sb,
        env->GetMethodID(sbCls, "channel",
                         "(Ljava/lang/Class;)Lio/netty/bootstrap/AbstractBootstrap;"),
        sscCls);
    jobject init = env->NewObject(g_bs.initClass, g_bs.initCtor);
    env->CallObjectMethod(sb,
        env->GetMethodID(sbCls, "childHandler",
                         "(Lio/netty/channel/ChannelHandler;)Lio/netty/bootstrap/ServerBootstrap;"),
        init);
    env->DeleteLocalRef(init);

    jstring host = env->NewStringUTF("0.0.0.0");
    jobject isa  = env->NewObject(isaCls,
        env->GetMethodID(isaCls, "<init>", "(Ljava/lang/String;I)V"),
        host, (jint)25565);
    env->DeleteLocalRef(host);

    jobject future = env->CallObjectMethod(sb,
        env->GetMethodID(sbCls, "bind",
                         "(Ljava/net/SocketAddress;)Lio/netty/channel/ChannelFuture;"),
        isa);
    env->DeleteLocalRef(isa);
    if (env->ExceptionCheck()) { LogAndClearException(env, "bind"); return false; }
    if (future) {
        jclass fCls = env->GetObjectClass(future);
        env->CallObjectMethod(future, env->GetMethodID(fCls, "sync", "()Lio/netty/channel/ChannelFuture;"));
        if (env->ExceptionCheck()) { LogAndClearException(env, "bind-sync"); return false; }
        env->DeleteLocalRef(fCls);
        env->DeleteLocalRef(future);
    }
    env->DeleteLocalRef(sb);
    env->DeleteLocalRef(elg);
    LogTo("BServer: bound 0.0.0.0:25565 (all interfaces, LAN-wide)");
    return true;
}

}

bool InstallBServer(JNIEnv* env) {
    if (g_bs.bServerBound) return true;
    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader) return false;
    bool ok = defineInitClass(env, mcLoader) && defineHandlerClass(env, mcLoader)
           && cacheJavaRefs(env, mcLoader) && bindServer(env, mcLoader);
    env->DeleteGlobalRef(mcLoader);
    if (ok) g_bs.bServerBound = true;
    return ok;
}

bool BServer_IsBActive() {
    return g_bs.bState.load(std::memory_order_acquire) == BState::Play;
}

bool BServer_IsLoginIntention(JNIEnv* env, jobject packet) {
    if (!env || !packet) return false;
    if (!g_bs.intentPacketCls || !env->IsInstanceOf(packet, g_bs.intentPacketCls))
        return false;
    if (!g_bs.intentionPacketIntentFid || !g_bs.protoLogin) return false;
    jobject intent = env->GetObjectField(packet, g_bs.intentionPacketIntentFid);
    if (!intent) return false;
    bool isLogin = env->IsSameObject(intent, g_bs.protoLogin);
    env->DeleteLocalRef(intent);
    return isLogin;
}

bool BServer_WaitForBConnected(int timeoutMs) {
    std::unique_lock<std::mutex> lock(g_bConnMu);
    if (g_bConnected) return true;
    if (timeoutMs < 0) {
        g_bConnCv.wait(lock, [] { return g_bConnected; });
        return true;
    }
    g_bConnCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                       [] { return g_bConnected; });
    return g_bConnected;
}

bool BServer_BlockAMainThreadUntilBConnected(JNIEnv* env) {
    if (!env || !g_jvmti) return false;
    if (BServer_WaitForBConnected(0)) return true;

    jobject mcLoader = GetMinecraftClassLoader(env, g_jvmti);
    if (!mcLoader) {
        LogTo("[MAIN-GATE] Minecraft ClassLoader unavailable");
        return false;
    }
    if (!defineMainGateClass(env, mcLoader)) {
        env->DeleteGlobalRef(mcLoader);
        return false;
    }

    jclass minecraftCls = LoadClassInLoader(
        env, mcLoader, "net.minecraft.client.Minecraft");
    env->DeleteGlobalRef(mcLoader);
    if (!minecraftCls) {
        LogTo("[MAIN-GATE] Minecraft class unavailable");
        return false;
    }

    jmethodID getInstanceMid = findMethodByDesc(
        minecraftCls, "()Lnet/minecraft/client/Minecraft;", true);
    jmethodID executeMid = env->GetMethodID(
        minecraftCls, "execute", "(Ljava/lang/Runnable;)V");
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!getInstanceMid || !executeMid) {
        LogTo("[MAIN-GATE] Minecraft getInstance/execute lookup failed");
        env->DeleteLocalRef(minecraftCls);
        return false;
    }

    jobject minecraft = env->CallStaticObjectMethod(minecraftCls, getInstanceMid);
    jobject gate = env->NewObject(g_bs.mainGateClass, g_bs.mainGateCtor);
    if (!minecraft || !gate || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LogTo("[MAIN-GATE] failed to create Minecraft gate task");
        if (gate) env->DeleteLocalRef(gate);
        if (minecraft) env->DeleteLocalRef(minecraft);
        env->DeleteLocalRef(minecraftCls);
        return false;
    }

    env->CallVoidMethod(minecraft, executeMid, gate);
    bool ok = !env->ExceptionCheck();
    if (!ok) env->ExceptionClear();
    LogTo("[MAIN-GATE] task %s on A Render thread", ok ? "queued" : "queue failed");

    env->DeleteLocalRef(gate);
    env->DeleteLocalRef(minecraft);
    env->DeleteLocalRef(minecraftCls);
    return ok;
}

void BServer_SetTargetConnection(JNIEnv* env, jobject connection) {
    std::lock_guard<std::mutex> l(g_bs.targetMu);
    if (g_bs.targetAConnection) env->DeleteGlobalRef(g_bs.targetAConnection);
    g_bs.targetAConnection = connection ? env->NewGlobalRef(connection) : nullptr;
    LogTo("BServer: target A connection = %p", (void*)g_bs.targetAConnection);
}

bool BServer_TryCaptureLiveConnection(JNIEnv* env) {
    if (!env) return false;
    if (!g_bs.minecraftCls || !g_bs.mcGetInstanceMid || !g_bs.mcGetConnectionMid ||
        !g_bs.cplGetConnectionMid || !g_bs.connectionChannelFid ||
        !g_bs.channelPipelineMid) {
        LogTo("mid-session: refs missing, cannot capture live connection");
        return false;
    }
    jobject mc = env->CallStaticObjectMethod(g_bs.minecraftCls, g_bs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    jobject cpl = env->CallObjectMethod(mc, g_bs.mcGetConnectionMid);
    env->DeleteLocalRef(mc);
    if (!cpl || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    jobject conn = env->CallObjectMethod(cpl, g_bs.cplGetConnectionMid);
    env->DeleteLocalRef(cpl);
    if (!conn || env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    BServer_SetTargetConnection(env, conn);

    jobject channel = env->GetObjectField(conn, g_bs.connectionChannelFid);
    env->DeleteLocalRef(conn);
    if (!channel || env->ExceptionCheck()) { env->ExceptionClear(); LogTo("mid-session: connection.channel null"); return false; }
    jobject pipeline = env->CallObjectMethod(channel, g_bs.channelPipelineMid);
    env->DeleteLocalRef(channel);
    if (!pipeline || env->ExceptionCheck()) { env->ExceptionClear(); LogTo("mid-session: channel.pipeline() null"); return false; }
    RelayHandler_AttachToPipelineObject(env, pipeline);
    env->DeleteLocalRef(pipeline);

    g_bs.midSession.store(true, std::memory_order_release);
    LogTo("mid-session: captured A's live Connection + attached relay (A already in-game)");
    return true;
}

namespace {

void writeToB(JNIEnv* env, jobject packet) {
    jobject ch;
    { std::lock_guard<std::mutex> l(g_bs.bMu); ch = g_bs.bChannel ? env->NewLocalRef(g_bs.bChannel) : nullptr; }
    if (!ch) return;
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, packet);
    if (env->ExceptionCheck()) LogAndClearException(env, "writeAndFlush");
    env->DeleteLocalRef(ch);
}

void routeToA(JNIEnv* env, jobject packet) {
    jobject target;
    {
        std::lock_guard<std::mutex> l(g_bs.targetMu);
        target = g_bs.targetAConnection
            ? env->NewLocalRef(g_bs.targetAConnection)
            : nullptr;
    }
    if (!target) return;
    if (!g_bs.connectionSendMid) {
        env->DeleteLocalRef(target);
        return;
    }

    RelayFilter_MarkBypass(env, packet);
    env->CallVoidMethod(target, g_bs.connectionSendMid, packet);
    if (env->ExceptionCheck()) LogAndClearException(env, "routeToA");
    env->DeleteLocalRef(target);
}

bool shouldRouteBToA(const std::string& fqcn) {
    static constexpr const char kGamePrefix[] =
        "net.minecraft.network.protocol.game.";

    if (fqcn.rfind(kGamePrefix, 0) != 0) return false;
    const char* simple = fqcn.c_str() + (sizeof(kGamePrefix) - 1);
    if (std::strcmp(simple, "ServerboundCustomPayloadPacket") == 0) return false;

    return true;
}

bool uuidToBytes(JNIEnv* env, jobject uuid, unsigned char out[16]) {
    if (!uuid || !g_bs.uuidGetMsbMid || !g_bs.uuidGetLsbMid) return false;
    jlong msb = env->CallLongMethod(uuid, g_bs.uuidGetMsbMid);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    jlong lsb = env->CallLongMethod(uuid, g_bs.uuidGetLsbMid);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    for (int i = 0; i < 8; ++i) out[i]     = (unsigned char)((msb >> ((7 - i) * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) out[8 + i] = (unsigned char)((lsb >> ((7 - i) * 8)) & 0xFF);
    return true;
}

void ensureARealUuid(JNIEnv* env) {
    if (g_bs.aUuidReady && g_bs.aName) return;
    if (!g_bs.minecraftCls || !g_bs.mcGetInstanceMid || !g_bs.mcGetUserMid) return;
    jobject mc = env->CallStaticObjectMethod(g_bs.minecraftCls, g_bs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject user = env->CallObjectMethod(mc, g_bs.mcGetUserMid);
    env->DeleteLocalRef(mc);
    if (!user || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    if (!g_bs.aUuidReady && g_bs.userGetProfileIdMid) {
        jobject uuid = env->CallObjectMethod(user, g_bs.userGetProfileIdMid);
        if (uuid && !env->ExceptionCheck()) {
            unsigned char bytes[16];
            if (uuidToBytes(env, uuid, bytes)) {
                g_bs.aRealUuid = env->NewGlobalRef(uuid);
                std::memcpy(g_bs.aUuidBytes, bytes, 16);
                g_bs.aUuidReady = true;
                LogTo("mirror: cached A's real UUID for tab-list mirroring");
            }
            env->DeleteLocalRef(uuid);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (!g_bs.aName && g_bs.userGetGameProfileMid && g_bs.gameProfileGetNameMid) {
        jobject profile = env->CallObjectMethod(user, g_bs.userGetGameProfileMid);
        if (profile && !env->ExceptionCheck()) {
            jstring nm = (jstring)env->CallObjectMethod(profile, g_bs.gameProfileGetNameMid);
            if (nm && !env->ExceptionCheck()) {
                g_bs.aName = (jstring)env->NewGlobalRef(nm);
                const char* utf = env->GetStringUTFChars(nm, nullptr);
                LogTo("team-mirror: cached A's name = '%s'", utf ? utf : "?");
                if (utf) env->ReleaseStringUTFChars(nm, utf);
                env->DeleteLocalRef(nm);
            } else if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(profile);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(user);
}

void sendSelfInfoToB(JNIEnv* env, jobject offlineUuid, jstring name) {
    if (!g_bs.minecraftCls || !g_bs.mcGetInstanceMid || !g_bs.mcGetProfilePropsMid ||
        !g_bs.friendlyBufCls || !g_bs.friendlyBufCtor || !g_bs.fbbWriteByteMid ||
        !g_bs.fbbWriteBooleanMid || !g_bs.fbbWriteVarIntMid ||
        !g_bs.fbbWriteUUIDMid || !g_bs.fbbWriteUtfMid || !g_bs.fbbWriteGpPropsMid ||
        !g_bs.unpooledCls || !g_bs.unpooledBufferMid ||
        !g_bs.playerInfoUpdatePacketCls || !g_bs.playerInfoUpdatePacketBufCtor) {
        LogTo("self-info: missing refs, skipping push");
        return;
    }
    jobject mc = env->CallStaticObjectMethod(g_bs.minecraftCls, g_bs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); LogTo("self-info: no Minecraft"); return; }
    jobject props = env->CallObjectMethod(mc, g_bs.mcGetProfilePropsMid);
    env->DeleteLocalRef(mc);
    if (!props || env->ExceptionCheck()) { env->ExceptionClear(); LogTo("self-info: no profile props"); return; }

    jobject bb = env->CallStaticObjectMethod(g_bs.unpooledCls, g_bs.unpooledBufferMid);
    if (!bb || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(props); return; }
    jobject buf = env->NewObject(g_bs.friendlyBufCls, g_bs.friendlyBufCtor, bb);
    env->DeleteLocalRef(bb);
    if (!buf || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(props); return; }

    env->CallObjectMethod(buf, g_bs.fbbWriteByteMid, (jint)0x1D);
    env->CallObjectMethod(buf, g_bs.fbbWriteByteMid, (jint)0x01);
    env->CallObjectMethod(buf, g_bs.fbbWriteUUIDMid, offlineUuid);

    env->CallObjectMethod(buf, g_bs.fbbWriteUtfMid, name, (jint)16);
    env->CallVoidMethod  (buf, g_bs.fbbWriteGpPropsMid, props);

    env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, (jint)0);

    env->CallObjectMethod(buf, g_bs.fbbWriteBooleanMid, (jboolean)JNI_TRUE);

    env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, (jint)0);
    env->DeleteLocalRef(props);
    if (env->ExceptionCheck()) { LogAndClearException(env, "self-info: buffer build"); env->DeleteLocalRef(buf); return; }

    jobject pkt = env->NewObject(g_bs.playerInfoUpdatePacketCls,
                                 g_bs.playerInfoUpdatePacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) { LogAndClearException(env, "self-info: packet ctor"); return; }
    writeToB(env, pkt);
    env->DeleteLocalRef(pkt);
    LogTo("self-info: pushed B's own player-info to B (ADD_PLAYER+LISTED+GAME_MODE+LATENCY, A's skin)");
}

bool reconstructAndSendLoginToB(JNIEnv* env, jobject ch) {
    if (!g_bs.loginPacketCtor || !g_bs.minecraftCls || !g_bs.mcGetInstanceMid ||
        !g_bs.mcGetConnectionMid || !g_bs.cplLevelsMid || !g_bs.cplRegistryAccessMid ||
        !g_bs.registryAccessFreezeMid || !g_bs.mcLevelFid || !g_bs.mcGameModeFid ||
        !g_bs.gameModeGetTypeMid || !g_bs.levelDimMidA || !g_bs.objToStringMid ||
        !g_bs.optionalCls || !g_bs.optionalEmptyMid) {
        LogTo("mid-login: refs missing, cannot rebuild login");
        return false;
    }
    jobject mc = env->CallStaticObjectMethod(g_bs.minecraftCls, g_bs.mcGetInstanceMid);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    jobject cpl = env->CallObjectMethod(mc, g_bs.mcGetConnectionMid);
    if (!cpl || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(mc); return false; }

    jobject levels = env->CallObjectMethod(cpl, g_bs.cplLevelsMid);
    jobject ra     = (levels && !env->ExceptionCheck()) ? env->CallObjectMethod(cpl, g_bs.cplRegistryAccessMid) : nullptr;
    env->DeleteLocalRef(cpl);
    jobject frozen = (ra && !env->ExceptionCheck()) ? env->CallObjectMethod(ra, g_bs.registryAccessFreezeMid) : nullptr;
    if (ra) env->DeleteLocalRef(ra);
    if (!levels || !frozen || env->ExceptionCheck()) {
        env->ExceptionClear();
        LogTo("mid-login: levels/registry unavailable");
        if (levels) env->DeleteLocalRef(levels);
        if (frozen) env->DeleteLocalRef(frozen);
        env->DeleteLocalRef(mc);
        return false;
    }

    jobject gameMode = env->GetObjectField(mc, g_bs.mcGameModeFid);
    jobject gameType = (gameMode && !env->ExceptionCheck()) ?
        env->CallObjectMethod(gameMode, g_bs.gameModeGetTypeMid) : nullptr;
    if (gameMode) env->DeleteLocalRef(gameMode);
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject level = env->GetObjectField(mc, g_bs.mcLevelFid);
    env->DeleteLocalRef(mc);
    jobject dimKey = nullptr, dimTypeKey = nullptr;
    if (level && !env->ExceptionCheck()) {
        jmethodID mids[2] = { g_bs.levelDimMidA, g_bs.levelDimMidB };
        for (int i = 0; i < 2 && mids[i]; ++i) {
            jobject rk = env->CallObjectMethod(level, mids[i]);
            if (!rk || env->ExceptionCheck()) { env->ExceptionClear(); if (rk) env->DeleteLocalRef(rk); continue; }
            jstring s = (jstring)env->CallObjectMethod(rk, g_bs.objToStringMid);
            bool isDimType = false;
            if (s && !env->ExceptionCheck()) {
                const char* c = env->GetStringUTFChars(s, nullptr);
                if (c) { isDimType = std::strstr(c, "dimension_type") != nullptr; env->ReleaseStringUTFChars(s, c); }
                env->DeleteLocalRef(s);
            } else if (env->ExceptionCheck()) env->ExceptionClear();
            if (isDimType) { if (dimTypeKey) env->DeleteLocalRef(dimTypeKey); dimTypeKey = rk; }
            else           { if (dimKey)     env->DeleteLocalRef(dimKey);     dimKey = rk; }
        }
    }
    if (level) env->DeleteLocalRef(level);

    if (!gameType || !dimKey || !dimTypeKey) {
        LogTo("mid-login: incomplete (gameType=%p dim=%p dimType=%p), abort",
              (void*)gameType, (void*)dimKey, (void*)dimTypeKey);
        if (levels) env->DeleteLocalRef(levels);
        if (frozen) env->DeleteLocalRef(frozen);
        if (gameType) env->DeleteLocalRef(gameType);
        if (dimKey) env->DeleteLocalRef(dimKey);
        if (dimTypeKey) env->DeleteLocalRef(dimTypeKey);
        return false;
    }

    jobject empty = env->CallStaticObjectMethod(g_bs.optionalCls, g_bs.optionalEmptyMid);
    if (!empty || env->ExceptionCheck()) {

        env->ExceptionClear();
        LogTo("mid-login: Optional.empty() failed, abort");
        if (empty) env->DeleteLocalRef(empty);
        env->DeleteLocalRef(levels); env->DeleteLocalRef(frozen);
        env->DeleteLocalRef(gameType); env->DeleteLocalRef(dimKey); env->DeleteLocalRef(dimTypeKey);
        return false;
    }

    const jint     playerId    = 2000000000;
    const jboolean hardcore    = JNI_FALSE;
    const jlong    seed        = 0;
    const jint     maxPlayers  = 20;
    const jint     chunkRadius = 16;
    const jint     simDist     = 16;
    jobject login = env->NewObject(g_bs.loginPacketCls, g_bs.loginPacketCtor,
        playerId, hardcore, gameType, (jobject)nullptr, levels, frozen,
        dimTypeKey, dimKey, seed, maxPlayers, chunkRadius, simDist,
        (jboolean)JNI_FALSE, (jboolean)JNI_TRUE, (jboolean)JNI_FALSE, (jboolean)JNI_FALSE,
        empty, (jint)0);

    env->DeleteLocalRef(levels);
    env->DeleteLocalRef(frozen);
    env->DeleteLocalRef(gameType);
    env->DeleteLocalRef(dimKey);
    env->DeleteLocalRef(dimTypeKey);
    if (empty) env->DeleteLocalRef(empty);

    if (!login || env->ExceptionCheck()) { LogAndClearException(env, "mid-login: ctor"); return false; }
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, login);
    bool sent = !env->ExceptionCheck();
    if (!sent) LogAndClearException(env, "mid-login: writeAndFlush");
    env->DeleteLocalRef(login);
    if (!sent) return false;
    LogTo("mid-login: rebuilt ClientboundLoginPacket sent to B");

    if (g_bs.playerPositionPacketCtor && g_bs.setOfMid && g_bs.setCls) {
        jobject relSet = env->CallStaticObjectMethod(g_bs.setCls, g_bs.setOfMid);
        if (relSet && !env->ExceptionCheck()) {
            jobject pos = env->NewObject(g_bs.playerPositionPacketCls, g_bs.playerPositionPacketCtor,
                (jdouble)0.0, (jdouble)100.0, (jdouble)0.0, (jfloat)0.0f, (jfloat)0.0f, relSet, (jint)0);
            if (pos && !env->ExceptionCheck()) {
                env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, pos);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(pos);
                LogTo("mid-login: sent placeholder spawn position to B");
            } else if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(relSet);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return true;
}

void completeLogin(JNIEnv* env, jobject hello) {

    if (!g_bs.helloPacketNameFid) { LogTo("login: no hello.name field"); return; }
    jstring jname = (jstring)env->GetObjectField(hello, g_bs.helloPacketNameFid);
    if (!jname) { LogTo("login: hello.name is null"); return; }
    const char* utfName = env->GetStringUTFChars(jname, nullptr);
    LogTo("login: B says name='%s'", utfName ? utfName : "?");

    std::string composite = std::string("OfflinePlayer:") + (utfName ? utfName : "");
    if (utfName) env->ReleaseStringUTFChars(jname, utfName);
    jbyteArray arr = env->NewByteArray((jsize)composite.size());
    env->SetByteArrayRegion(arr, 0, (jsize)composite.size(),
                            reinterpret_cast<const jbyte*>(composite.data()));
    jobject uuid = env->CallStaticObjectMethod(
        g_bs.uuidCls, g_bs.uuidNameUuidFromBytesMid, arr);
    env->DeleteLocalRef(arr);
    if (!uuid || env->ExceptionCheck()) {
        LogAndClearException(env, "login: nameUUIDFromBytes"); env->DeleteLocalRef(jname); return;
    }
    LogTo("login: computed offline UUID");

    if (!g_bs.gameProfileCtor) { LogTo("login: no GameProfile ctor"); env->DeleteLocalRef(uuid); env->DeleteLocalRef(jname); return; }
    jobject profile = env->NewObject(g_bs.gameProfileCls, g_bs.gameProfileCtor, uuid, jname);
    if (!profile || env->ExceptionCheck()) {
        LogAndClearException(env, "login: GameProfile ctor"); env->DeleteLocalRef(uuid); env->DeleteLocalRef(jname); return;
    }

    if (!g_bs.loginFinishedPacketCtor) { LogTo("login: no LoginFinished ctor"); return; }
    jobject lfp = env->NewObject(g_bs.loginFinishedPacketCls, g_bs.loginFinishedPacketCtor, profile);
    env->DeleteLocalRef(profile);
    if (!lfp || env->ExceptionCheck()) {
        LogAndClearException(env, "login: LoginFinished ctor"); return;
    }

    writeToB(env, lfp);
    env->DeleteLocalRef(lfp);
    LogTo("login: sent ClientboundGameProfilePacket to B");

    jobject ch;
    { std::lock_guard<std::mutex> l(g_bs.bMu); ch = g_bs.bChannel ? env->NewLocalRef(g_bs.bChannel) : nullptr; }
    if (ch) {
        setProtocolState(env, ch, g_bs.protoPlay);
        LogTo("login: protocol switched to PLAY");

        if (g_bs.midSession.load(std::memory_order_acquire)) {
            LogTo("login: mid-session — rebuilding login for B from A's state");
            reconstructAndSendLoginToB(env, ch);
        }
        env->DeleteLocalRef(ch);
    }

    if (g_bs.keepAlivePacketCtor && g_bs.bChannel) {
        jobject ka = env->NewObject(g_bs.keepAlivePacketCls,
                                    g_bs.keepAlivePacketCtor, (jlong)1);
        if (ka) { writeToB(env, ka); env->DeleteLocalRef(ka); }
    }

    sendSelfInfoToB(env, uuid, jname);

    unsigned char bBytes[16];
    if (uuidToBytes(env, uuid, bBytes)) {
        std::memcpy(g_bs.bUuidBytes, bBytes, 16);
        g_bs.bUuidReady = true;
    }

    if (g_bs.bUuidObj) env->DeleteGlobalRef(g_bs.bUuidObj);
    g_bs.bUuidObj = env->NewGlobalRef(uuid);

    if (g_bs.bName) env->DeleteGlobalRef(g_bs.bName);
    g_bs.bName = (jstring)env->NewGlobalRef(jname);
    ensureARealUuid(env);

    env->DeleteLocalRef(uuid);
    env->DeleteLocalRef(jname);

    g_bs.bState.store(BState::Play, std::memory_order_release);
    {
        std::lock_guard<std::mutex> l(g_bConnMu);
        g_bConnected = true;
    }
    g_bConnCv.notify_all();
    LogTo("login: B in PLAY (empty world); released A's Render thread — A now "
          "connects and its live join stream feeds B");
}

}

void hideAFromBTab(JNIEnv* env, jobject ch) {
    if (!g_bs.aRealUuid || !g_bs.friendlyBufCls || !g_bs.friendlyBufCtor ||
        !g_bs.unpooledCls || !g_bs.unpooledBufferMid || !g_bs.fbbWriteByteMid ||
        !g_bs.fbbWriteVarIntMid || !g_bs.fbbWriteUUIDMid || !g_bs.fbbWriteBooleanMid ||
        !g_bs.playerInfoUpdatePacketCls || !g_bs.playerInfoUpdatePacketBufCtor) return;

    jobject bb = env->CallStaticObjectMethod(g_bs.unpooledCls, g_bs.unpooledBufferMid);
    if (!bb || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject buf = env->NewObject(g_bs.friendlyBufCls, g_bs.friendlyBufCtor, bb);
    env->DeleteLocalRef(bb);
    if (!buf || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    env->CallObjectMethod(buf, g_bs.fbbWriteByteMid, (jint)0x08);
    env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, (jint)1);
    env->CallObjectMethod(buf, g_bs.fbbWriteUUIDMid, g_bs.aRealUuid);
    env->CallObjectMethod(buf, g_bs.fbbWriteBooleanMid, (jboolean)JNI_FALSE);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(buf); return; }

    jobject pkt = env->NewObject(g_bs.playerInfoUpdatePacketCls,
                                 g_bs.playerInfoUpdatePacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) { LogAndClearException(env, "hide-A: ctor"); return; }
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, pkt);
    if (env->ExceptionCheck()) LogAndClearException(env, "hide-A: writeAndFlush");
    env->DeleteLocalRef(pkt);
}

void mirrorPlayerInfoUpdateToB(JNIEnv* env, jobject ch, jobject packet) {
    if (!g_bs.playerInfoUpdatePacketCls || !g_bs.playerInfoUpdatePacketBufCtor ||
        !g_bs.playerInfoUpdatePacketWriteMid || !g_bs.friendlyBufCls ||
        !g_bs.friendlyBufCtor || !g_bs.unpooledCls || !g_bs.unpooledBufferMid ||
        !g_bs.byteBufGetByteMid || !g_bs.listSizeMid || !g_bs.listGetMid ||
        !g_bs.piuEntriesMidA || !g_bs.piEntryProfileIdMid ||
        !g_bs.fbbWriteByteMid || !g_bs.fbbWriteVarIntMid || !g_bs.fbbWriteUUIDMid ||
        !g_bs.fbbWriteBooleanMid) return;
    if (!g_bs.aUuidReady || !g_bs.bUuidReady || !g_bs.bUuidObj) return;
    if (!env->IsInstanceOf(packet, g_bs.playerInfoUpdatePacketCls)) return;

    jobject bb0 = env->CallStaticObjectMethod(g_bs.unpooledCls, g_bs.unpooledBufferMid);
    if (!bb0 || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject sbuf = env->NewObject(g_bs.friendlyBufCls, g_bs.friendlyBufCtor, bb0);
    env->DeleteLocalRef(bb0);
    if (!sbuf || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    env->CallVoidMethod(packet, g_bs.playerInfoUpdatePacketWriteMid, sbuf);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(sbuf); return; }
    jint bits = env->CallByteMethod(sbuf, g_bs.byteBufGetByteMid, (jint)0) & 0xFF;
    env->DeleteLocalRef(sbuf);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return; }

    bool hasGameMode = bits & 0x04;
    bool hasListed   = bits & 0x08;
    bool hasLatency  = bits & 0x10;
    bool hasDisplay  = bits & 0x20;
    int outBits = (hasGameMode ? 0x04 : 0) | (hasListed ? 0x08 : 0) |
                  (hasLatency ? 0x10 : 0) | (hasDisplay ? 0x20 : 0);

    jobject listA = env->CallObjectMethod(packet, g_bs.piuEntriesMidA);
    if (env->ExceptionCheck()) { env->ExceptionClear(); listA = nullptr; }
    jobject listB = g_bs.piuEntriesMidB ? env->CallObjectMethod(packet, g_bs.piuEntriesMidB) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); listB = nullptr; }
    jint sizeA = -1, sizeB = -1;
    if (listA) { sizeA = env->CallIntMethod(listA, g_bs.listSizeMid);
                 if (env->ExceptionCheck()) { env->ExceptionClear(); sizeA = -1; } }
    if (listB) { sizeB = env->CallIntMethod(listB, g_bs.listSizeMid);
                 if (env->ExceptionCheck()) { env->ExceptionClear(); sizeB = -1; } }
    jobject entries = (sizeB > sizeA) ? listB : listA;
    jint    nEntries = (sizeB > sizeA) ? sizeB : sizeA;

    jobject aEntry = nullptr;
    for (jint i = 0; i < nEntries && !aEntry; ++i) {
        jobject e = env->CallObjectMethod(entries, g_bs.listGetMid, i);
        if (!e || env->ExceptionCheck()) { env->ExceptionClear(); if (e) env->DeleteLocalRef(e); continue; }
        jobject euuid = env->CallObjectMethod(e, g_bs.piEntryProfileIdMid);
        if (euuid && !env->ExceptionCheck()) {
            unsigned char eb[16];
            if (uuidToBytes(env, euuid, eb) && std::memcmp(eb, g_bs.aUuidBytes, 16) == 0)
                aEntry = env->NewLocalRef(e);
            env->DeleteLocalRef(euuid);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(e);
    }
    if (listA) env->DeleteLocalRef(listA);
    if (listB) env->DeleteLocalRef(listB);
    if (!aEntry) return;

    hideAFromBTab(env, ch);

    if (outBits == 0) { env->DeleteLocalRef(aEntry); return; }

    jobject bb = env->CallStaticObjectMethod(g_bs.unpooledCls, g_bs.unpooledBufferMid);
    if (!bb || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(aEntry); return; }
    jobject buf = env->NewObject(g_bs.friendlyBufCls, g_bs.friendlyBufCtor, bb);
    env->DeleteLocalRef(bb);
    if (!buf || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(aEntry); return; }

    env->CallObjectMethod(buf, g_bs.fbbWriteByteMid, (jint)outBits);
    env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, (jint)1);
    env->CallObjectMethod(buf, g_bs.fbbWriteUUIDMid, g_bs.bUuidObj);

    if (hasGameMode && g_bs.piEntryGameModeMid && g_bs.gameTypeGetIdMid) {
        jobject gm = env->CallObjectMethod(aEntry, g_bs.piEntryGameModeMid);
        jint id = 0;
        if (gm && !env->ExceptionCheck()) id = env->CallIntMethod(gm, g_bs.gameTypeGetIdMid);
        if (gm) env->DeleteLocalRef(gm);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, id);
    }
    if (hasListed && g_bs.piEntryListedMid) {
        jboolean listed = env->CallBooleanMethod(aEntry, g_bs.piEntryListedMid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); listed = JNI_TRUE; }
        env->CallObjectMethod(buf, g_bs.fbbWriteBooleanMid, listed);
    }
    if (hasLatency && g_bs.piEntryLatencyMid) {
        jint lat = env->CallIntMethod(aEntry, g_bs.piEntryLatencyMid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); lat = 0; }
        env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, lat);
    }
    if (hasDisplay) {
        jobject dn = g_bs.piEntryDisplayNameMid ?
            env->CallObjectMethod(aEntry, g_bs.piEntryDisplayNameMid) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); dn = nullptr; }

        if (dn && g_bs.fbbWriteComponentMid) {
            env->CallObjectMethod(buf, g_bs.fbbWriteBooleanMid, (jboolean)JNI_TRUE);
            env->CallObjectMethod(buf, g_bs.fbbWriteComponentMid, dn);
        } else {
            env->CallObjectMethod(buf, g_bs.fbbWriteBooleanMid, (jboolean)JNI_FALSE);
        }
        if (dn) env->DeleteLocalRef(dn);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(aEntry);

    jobject pkt = env->NewObject(g_bs.playerInfoUpdatePacketCls,
                                 g_bs.playerInfoUpdatePacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) { LogAndClearException(env, "mirror: single-entry ctor"); return; }
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, pkt);
    if (env->ExceptionCheck()) LogAndClearException(env, "mirror: writeAndFlush");
    env->DeleteLocalRef(pkt);
    LogTo("mirror: single-entry PlayerInfoUpdate to B (bits=0x%02x)", outBits);
}

void synthesiseMinimalPlayerInfoForUuid(JNIEnv* env, jobject ch,
                                        const unsigned char uuidBytes[16]) {
    if (!g_bs.unpooledCls || !g_bs.unpooledWrappedMid || !g_bs.friendlyBufCls ||
        !g_bs.friendlyBufCtor || !g_bs.playerInfoUpdatePacketCls ||
        !g_bs.playerInfoUpdatePacketBufCtor) return;

    unsigned char wire[20];
    wire[0] = 0x01;
    wire[1] = 0x01;
    std::memcpy(wire + 2, uuidBytes, 16);
    wire[18] = 0x00;
    wire[19] = 0x00;

    jbyteArray raw = env->NewByteArray(20);
    if (!raw) return;
    env->SetByteArrayRegion(raw, 0, 20, reinterpret_cast<const jbyte*>(wire));

    jobject wrapped = env->CallStaticObjectMethod(g_bs.unpooledCls,
                                                  g_bs.unpooledWrappedMid, raw);
    env->DeleteLocalRef(raw);
    if (!wrapped || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject buf = env->NewObject(g_bs.friendlyBufCls, g_bs.friendlyBufCtor, wrapped);
    env->DeleteLocalRef(wrapped);
    if (!buf || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject pkt = env->NewObject(g_bs.playerInfoUpdatePacketCls,
                                 g_bs.playerInfoUpdatePacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) {
        LogAndClearException(env, "synth-tab: packet ctor"); return;
    }
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, pkt);
    if (env->ExceptionCheck()) LogAndClearException(env, "synth-tab: writeAndFlush");
    env->DeleteLocalRef(pkt);
}

void ensureTabEntryBeforeAddPlayer(JNIEnv* env, jobject ch, jobject packet) {
    if (!g_bs.addPlayerPacketCls || !g_bs.addPlayerPacketUuidFid) return;
    if (!env->IsInstanceOf(packet, g_bs.addPlayerPacketCls)) return;
    jobject uuid = env->GetObjectField(packet, g_bs.addPlayerPacketUuidFid);
    if (!uuid) { if (env->ExceptionCheck()) env->ExceptionClear(); return; }
    unsigned char bytes[16];
    bool ok = uuidToBytes(env, uuid, bytes);
    env->DeleteLocalRef(uuid);
    if (!ok) return;
    synthesiseMinimalPlayerInfoForUuid(env, ch, bytes);
}

void mirrorTeamPacketToB(JNIEnv* env, jobject ch, jobject packet) {
    if (!g_bs.setPlayerTeamPacketCls || !g_bs.setPlayerTeamPacketBufCtor ||
        !g_bs.setPlayerTeamMethodFid || !g_bs.setPlayerTeamNameFid ||
        !g_bs.setPlayerTeamPlayersFid || !g_bs.collectionContainsMid ||
        !g_bs.friendlyBufCls || !g_bs.friendlyBufCtor ||
        !g_bs.fbbWriteByteMid || !g_bs.fbbWriteVarIntMid || !g_bs.fbbWriteUtfMid ||
        !g_bs.unpooledCls || !g_bs.unpooledBufferMid) return;
    if (!g_bs.aName || !g_bs.bName) return;
    if (!env->IsInstanceOf(packet, g_bs.setPlayerTeamPacketCls)) return;

    jint method = env->GetIntField(packet, g_bs.setPlayerTeamMethodFid);

    if (method != 0 && method != 3 && method != 4) return;

    jobject players = env->GetObjectField(packet, g_bs.setPlayerTeamPlayersFid);
    if (!players) return;
    jboolean hasA = env->CallBooleanMethod(players, g_bs.collectionContainsMid, g_bs.aName);
    env->DeleteLocalRef(players);
    if (env->ExceptionCheck() || !hasA) { env->ExceptionClear(); return; }

    jstring teamName = (jstring)env->GetObjectField(packet, g_bs.setPlayerTeamNameFid);
    if (!teamName) return;

    jint outMethod = (method == 4) ? 4 : 3;

    jobject bb = env->CallStaticObjectMethod(g_bs.unpooledCls, g_bs.unpooledBufferMid);
    if (!bb || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(teamName); return; }
    jobject buf = env->NewObject(g_bs.friendlyBufCls, g_bs.friendlyBufCtor, bb);
    env->DeleteLocalRef(bb);
    if (!buf || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(teamName); return; }

    env->CallObjectMethod(buf, g_bs.fbbWriteUtfMid, teamName, (jint)32767);
    env->CallObjectMethod(buf, g_bs.fbbWriteByteMid, (jint)outMethod);
    env->CallObjectMethod(buf, g_bs.fbbWriteVarIntMid, (jint)1);
    env->CallObjectMethod(buf, g_bs.fbbWriteUtfMid, g_bs.bName, (jint)32767);
    env->DeleteLocalRef(teamName);
    if (env->ExceptionCheck()) { LogAndClearException(env, "team-mirror: buffer build"); env->DeleteLocalRef(buf); return; }

    jobject pkt = env->NewObject(g_bs.setPlayerTeamPacketCls,
                                 g_bs.setPlayerTeamPacketBufCtor, buf);
    env->DeleteLocalRef(buf);
    if (!pkt || env->ExceptionCheck()) { LogAndClearException(env, "team-mirror: packet ctor"); return; }
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, pkt);
    if (env->ExceptionCheck()) LogAndClearException(env, "team-mirror: writeAndFlush");
    env->DeleteLocalRef(pkt);
    LogTo("team-mirror: synthesised %s for B (server method=%d)",
          outMethod == 3 ? "JOIN" : "LEAVE", method);
}

void writeOneToBWithTabGuard(JNIEnv* env, jobject ch, jobject packet) {

    if (g_bs.customPayloadPacketCls &&
        env->IsInstanceOf(packet, g_bs.customPayloadPacketCls)) {
        static std::atomic<int> dropped{0};
        int n = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n & 0x3FF) == 0)
            LogTo("ForwardToB: skipping ClientboundCustomPayloadPacket (mod channel, count=%d)", n);
        return;
    }
    ensureTabEntryBeforeAddPlayer(env, ch, packet);
    env->CallObjectMethod(ch, g_bs.channelWriteAndFlushMid, packet);
    if (env->ExceptionCheck()) LogAndClearException(env, "ForwardToB/writeOne");
    mirrorPlayerInfoUpdateToB(env, ch, packet);
    mirrorTeamPacketToB(env, ch, packet);
}

bool forwardBundleExpanded(JNIEnv* env, jobject ch, jobject packet) {
    if (!g_bs.bundlePacketCls || !g_bs.bundleSubPacketsMid ||
        !g_bs.iterableIteratorMid || !g_bs.iteratorHasNextMid ||
        !g_bs.iteratorNextMid)
        return false;
    if (!env->IsInstanceOf(packet, g_bs.bundlePacketCls)) return false;

    jobject iterable = env->CallObjectMethod(packet, g_bs.bundleSubPacketsMid);
    if (env->ExceptionCheck() || !iterable) {
        LogAndClearException(env, "bundle.subPackets");
        return true;
    }
    jobject it = env->CallObjectMethod(iterable, g_bs.iterableIteratorMid);
    env->DeleteLocalRef(iterable);
    if (env->ExceptionCheck() || !it) {
        LogAndClearException(env, "bundle.iterator");
        return true;
    }

    int forwarded = 0, nulls = 0;
    while (true) {
        jboolean more = env->CallBooleanMethod(it, g_bs.iteratorHasNextMid);
        if (env->ExceptionCheck()) {
            LogAndClearException(env, "bundle.iter.hasNext");
            break;
        }
        if (!more) break;

        jobject sub = env->CallObjectMethod(it, g_bs.iteratorNextMid);
        if (env->ExceptionCheck()) {
            LogAndClearException(env, "bundle.iter.next");
            break;
        }
        if (!sub) { ++nulls; continue; }

        if (!forwardBundleExpanded(env, ch, sub)) {
            writeOneToBWithTabGuard(env, ch, sub);
        }
        env->DeleteLocalRef(sub);
        ++forwarded;
    }
    env->DeleteLocalRef(it);
    if (nulls > 0)
        LogTo("bundle: forwarded=%d, null-subs=%d (skipped)", forwarded, nulls);
    else
        LogTo("bundle: forwarded=%d sub-packets", forwarded);
    return true;
}

void BServer_ForwardToB(JNIEnv* env, jobject packet) {
    if (!packet) return;
    if (g_bs.bState.load(std::memory_order_acquire) != BState::Play) return;

    std::string cls = classNameForB(env, packet);
    if (cls.rfind("net.minecraft.network.protocol.game.", 0) != 0) return;

    jobject ch;
    { std::lock_guard<std::mutex> l(g_bs.bMu);
      ch = g_bs.bChannel ? env->NewLocalRef(g_bs.bChannel) : nullptr; }
    if (!ch) return;

    if (!forwardBundleExpanded(env, ch, packet)) {
        writeOneToBWithTabGuard(env, ch, packet);
    }

    env->DeleteLocalRef(ch);
}

void BSide_OnPacket(JNIEnv* env, jobject , jobject msg) {
    std::string cls = classNameForB(env, msg);
    BState state = g_bs.bState.load(std::memory_order_acquire);
    LogTo("BServer: RX %s (state=%d)", cls.c_str(), (int)state);

    if (g_bs.intentPacketCls && env->IsInstanceOf(msg, g_bs.intentPacketCls)) {
        jobject nextProto = g_bs.protoLogin;
        if (g_bs.intentionPacketIntentFid) {
            jobject intent = env->GetObjectField(msg, g_bs.intentionPacketIntentFid);
            if (intent) {
                if (env->IsSameObject(intent, g_bs.protoStatus)) nextProto = g_bs.protoStatus;
                else                                             nextProto = g_bs.protoLogin;
                env->DeleteLocalRef(intent);
            }
        }
        jobject ch;
        { std::lock_guard<std::mutex> l(g_bs.bMu); ch = g_bs.bChannel ? env->NewLocalRef(g_bs.bChannel) : nullptr; }
        if (ch) {
            setProtocolState(env, ch, nextProto);
            env->DeleteLocalRef(ch);
        }
        g_bs.bState.store(
            (nextProto == g_bs.protoStatus) ? BState::AwaitHandshake
                                             : BState::AwaitLogin,
            std::memory_order_release);
        LogTo("BServer: intention → %s",
              nextProto == g_bs.protoStatus ? "STATUS" : "LOGIN");
        return;
    }

    if (g_bs.pingRequestPacketCls && env->IsInstanceOf(msg, g_bs.pingRequestPacketCls)) {
        jlong t = 0;
        if (g_bs.pingRequestPacketTimeFid)
            t = env->GetLongField(msg, g_bs.pingRequestPacketTimeFid);
        if (g_bs.pongResponsePacketCtor) {
            jobject pong = env->NewObject(g_bs.pongResponsePacketCls,
                                          g_bs.pongResponsePacketCtor, t);
            if (pong) { writeToB(env, pong); env->DeleteLocalRef(pong); }
            LogTo("BServer: replied Pong(%lld)", (long long)t);
        }
        return;
    }

    if (g_bs.helloPacketCls && env->IsInstanceOf(msg, g_bs.helloPacketCls)) {
        completeLogin(env, msg);
        return;
    }

    if (g_bs.bState.load(std::memory_order_acquire) == BState::Play) {
        if (shouldRouteBToA(cls)) {
            routeToA(env, msg);
        }
    }
}
