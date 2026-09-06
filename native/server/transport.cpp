#include "classfile.h"
#include "internal.h"
#include "random_name.h"
#include "runtime_gate.h"

namespace proxy_server {

void setChannelAttr(JNIEnv* env, jobject channel, jfieldID keyFid, jclass keyOwnerCls, jobject value) {
    jobject key = env->GetStaticObjectField(keyOwnerCls, keyFid);
    if (!key)
        return;
    jobject attr = env->CallObjectMethod(channel, server.refs.channelAttrMid, key);
    if (env->ExceptionCheck() || !attr) {
        env->ExceptionClear();
        return;
    }
    env->CallVoidMethod(attr, server.refs.attributeSetMid, value);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(attr);
    env->DeleteLocalRef(key);
}

void setProtocolState(JNIEnv* env, jobject channel, jobject protoValue) {
    if (!channel || !protoValue)
        return;
    if (server.refs.connectionAttrProtocolFid)
        setChannelAttr(env, channel, server.refs.connectionAttrProtocolFid, server.refs.connectionCls, protoValue);
    if (server.refs.bundlerProviderFid && server.refs.bundlerInfoCls)
        setChannelAttr(env, channel, server.refs.bundlerProviderFid, server.refs.bundlerInfoCls, protoValue);

    jobject pipeline = env->CallObjectMethod(channel, server.refs.channelPipelineMid);
    if (!pipeline || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    for (const char* handlerName : {"encoder", "decoder"}) {
        jstring nm = env->NewStringUTF(handlerName);
        jobject handler = env->CallObjectMethod(pipeline, server.refs.pipelineGetHandlerMid, nm);
        env->DeleteLocalRef(nm);
        if (!handler || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }
        jclass hCls = env->GetObjectClass(handler);

        jmethodID sp = findMethodByDescriptor(hCls, "(Lnet/minecraft/network/ConnectionProtocol;)V", false);
        if (!sp) {
            sp = env->GetMethodID(hCls, "setProtocol", "(Lnet/minecraft/network/ConnectionProtocol;)V");
            if (env->ExceptionCheck())
                env->ExceptionClear();
        }
        if (sp) {
            env->CallVoidMethod(handler, sp, protoValue);
            if (env->ExceptionCheck())
                env->ExceptionClear();
            else
                LogTo("setProtocolState: called %s.setProtocol(PLAY)", handlerName);
        }
        env->DeleteLocalRef(hCls);
        env->DeleteLocalRef(handler);
    }
    env->DeleteLocalRef(pipeline);
}

void JNICALL Native_ServerInit_initChannel(JNIEnv* env, jobject, jobject ch) {
    RuntimeCallback callback;
    if (!callback || server.stopping.load()) {
        if (server.refs.channelCloseMid)
            env->CallObjectMethod(ch, server.refs.channelCloseMid);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(server.childrenMutex);
        for (auto it = server.children.begin(); it != server.children.end();) {
            if (env->IsSameObject(*it, nullptr)) {
                env->DeleteWeakGlobalRef(*it);
                it = server.children.erase(it);
            } else
                ++it;
        }
        jweak weak = env->NewWeakGlobalRef(ch);
        if (weak)
            server.children.push_back(weak);
    }
    LogTo("BServer: initChannel for incoming ch=%p", (void*)ch);

    if (server.refs.channelConfigMid && server.refs.configSetOptionMid && server.refs.tcpNoDelayOption &&
        server.refs.booleanTrue) {
        jobject cfg = env->CallObjectMethod(ch, server.refs.channelConfigMid);
        if (cfg && !env->ExceptionCheck()) {
            env->CallBooleanMethod(cfg, server.refs.configSetOptionMid, server.refs.tcpNoDelayOption,
                                   server.refs.booleanTrue);
            if (env->ExceptionCheck())
                env->ExceptionClear();
            else
                LogTo("BServer: TCP_NODELAY set on B channel");
            env->DeleteLocalRef(cfg);
        } else if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    jobject pipeline = env->CallObjectMethod(ch, server.refs.channelPipelineMid);
    if (env->ExceptionCheck() || !pipeline) {
        env->ExceptionClear();
        LogTo("  pipeline() failed");
        return;
    }

    setProtocolState(env, ch, server.refs.protoHandshaking);

    env->CallStaticVoidMethod(server.refs.connectionCls, server.refs.connectionConfigureSerMid, pipeline,
                              server.refs.flowServerbound);
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "  configureSerialization");
    }

    bool haveBundlerInfo = (server.refs.bundlerProviderFid != nullptr && server.refs.bundlerInfoCls != nullptr);
    if (!haveBundlerInfo) {
        for (const char* h : {"unbundler", "bundler"}) {
            jstring nm = env->NewStringUTF(h);
            env->CallObjectMethod(pipeline, server.refs.pipelineRemoveNameMid, nm);
            if (env->ExceptionCheck())
                env->ExceptionClear();
            env->DeleteLocalRef(nm);
        }
        LogTo("  bundle handlers removed (no BundlerInfo)");
    } else {
        LogTo("  bundle handlers kept (BundlerInfo available)");
    }

    jobject handler = env->NewObject(server.refs.handlerClass, server.refs.handlerCtor);
    jstring name = env->NewStringUTF("bside");
    env->CallObjectMethod(pipeline, server.refs.pipelineAddLastMid, name, handler);
    if (env->ExceptionCheck())
        LogAndClearException(env, "  addLast(bside)");
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(handler);
    env->DeleteLocalRef(pipeline);

    LogTo("  Incoming channel ready; waiting for STATUS or LOGIN intention");
}

void JNICALL Native_BSide_channelActive(JNIEnv*, jobject, jobject) {
    LogTo("BServer: B channelActive");
}

void JNICALL Native_BSide_channelInactive(JNIEnv* env, jobject, jobject ctx) {
    RuntimeCallback callback;
    if (!callback || server.stopping.load())
        return;
    std::lock_guard<std::recursive_mutex> dispatch(server.dispatchMutex);
    jobject ch = env->CallObjectMethod(ctx, server.refs.contextChannelMid);
    if (!ch || env->ExceptionCheck()) {
        LogAndClearException(env, "channelInactive/channel");
        if (ch)
            env->DeleteLocalRef(ch);
        return;
    }
    LogTo("BServer: B channelInactive");
    {
        std::lock_guard<std::mutex> l(server.clientMutex);
        bool ownsSession = server.clientChannel && env->IsSameObject(ch, server.clientChannel);
        env->DeleteLocalRef(ch);
        if (!ownsSession)
            return;
        if (server.clientChannel) {
            env->DeleteGlobalRef(server.clientChannel);
            server.clientChannel = nullptr;
        }
        server.clientState.store(ClientState::AwaitHandshake, std::memory_order_release);
    }
    server.handoff.reset();
    clearPendingPackets(env);
    {
        std::lock_guard<std::mutex> gateLock(server.gateMutex);
        server.clientConnected = false;
    }

    if (server.midSession.load(std::memory_order_acquire)) {
        LogTo("BServer: B gone; mid-session — leaving A's connection intact, A resumes control");
    } else {

        closeARemoteConnection(env);
    }
}

void JNICALL Native_MainThreadGate_run(JNIEnv*, jobject) {
    std::unique_lock<std::mutex> lock(server.gateMutex);
    server.gateChanged.wait(lock, [] { return server.clientConnected || server.gateCancelled; });
    if (server.gateTasks)
        --server.gateTasks;
    server.gateChanged.notify_all();
    LogTo("[MAIN-GATE] released (connected=%d cancelled=%d)", server.clientConnected, server.gateCancelled);
}

void JNICALL Native_BSide_channelRead(JNIEnv* env, jobject, jobject ctx, jobject msg) {
    RuntimeCallback callback;
    if (!callback || server.stopping.load())
        return;
    if (env->PushLocalFrame(32) != JNI_OK) {
        LogAndClearException(env, "channelRead/local frame");
        return;
    }
    handleClientPacket(env, ctx, msg);
    LogAndClearException(env, "channelRead");
    env->PopLocalFrame(nullptr);
}

bool defineInitClass(JNIEnv* env, jobject mcLoader) {
    if (server.refs.initClass)
        return true;
    std::string simple = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);

    ClassBuilder cb(internal, kInitSuper, 52);
    u2 superInit = cb.methodRef(kInitSuper, "<init>", "()V");
    std::vector<u1> ctor = {0x2A, 0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF), 0xB1};
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
    cb.addNativeMethod("initChannel", kInitChannelDesc, ACC_PUBLIC | ACC_NATIVE);
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader, reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "BServer/DefineInit");
        return false;
    }
    JNINativeMethod nats[] = {
        {const_cast<char*>("initChannel"), const_cast<char*>(kInitChannelDesc),
         reinterpret_cast<void*>(&Native_ServerInit_initChannel)},
    };
    if (env->RegisterNatives(defined, nats, 1) != 0) {
        LogAndClearException(env, "BServer/RegisterInit");
        env->DeleteLocalRef(defined);
        return false;
    }
    server.refs.initCtor = env->GetMethodID(defined, "<init>", "()V");
    server.refs.initClass = static_cast<jclass>(env->NewGlobalRef(defined));
    env->DeleteLocalRef(defined);
    LogTo("BServer: defined ServerChannelInit as %s", internal.c_str());
    return true;
}

bool defineHandlerClass(JNIEnv* env, jobject mcLoader) {
    if (server.refs.handlerClass)
        return true;
    std::string simple = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);

    ClassBuilder cb(internal, kHandlerSuper, 52);
    u2 superInit = cb.methodRef(kHandlerSuper, "<init>", "()V");
    std::vector<u1> ctor = {0x2A, 0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF), 0xB1};
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
    cb.addNativeMethod("channelActive", kChannelActiveDesc, ACC_PUBLIC | ACC_NATIVE);
    cb.addNativeMethod("channelInactive", kChannelInactiveDesc, ACC_PUBLIC | ACC_NATIVE);
    cb.addNativeMethod("channelRead", kChannelReadDesc, ACC_PUBLIC | ACC_NATIVE);
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader, reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "BServer/DefineHandler");
        return false;
    }
    JNINativeMethod nats[] = {
        {const_cast<char*>("channelActive"), const_cast<char*>(kChannelActiveDesc),
         reinterpret_cast<void*>(&Native_BSide_channelActive)},
        {const_cast<char*>("channelInactive"), const_cast<char*>(kChannelInactiveDesc),
         reinterpret_cast<void*>(&Native_BSide_channelInactive)},
        {const_cast<char*>("channelRead"), const_cast<char*>(kChannelReadDesc),
         reinterpret_cast<void*>(&Native_BSide_channelRead)},
    };
    if (env->RegisterNatives(defined, nats, 3) != 0) {
        LogAndClearException(env, "BServer/RegisterHandler");
        env->DeleteLocalRef(defined);
        return false;
    }
    server.refs.handlerCtor = env->GetMethodID(defined, "<init>", "()V");
    server.refs.handlerClass = static_cast<jclass>(env->NewGlobalRef(defined));
    env->DeleteLocalRef(defined);
    LogTo("BServer: defined BSideHandler as %s", internal.c_str());
    return true;
}

bool defineMainGateClass(JNIEnv* env, jobject mcLoader) {
    if (server.refs.mainGateClass && server.refs.mainGateCtor)
        return true;

    std::string simple = GenerateRandomClassName(2, 3);
    std::string internal = MakeInternalName(GetTrampolinePackage(), simple);

    ClassBuilder cb(internal, "java/lang/Thread", 52);
    u2 superInit = cb.methodRef("java/lang/Thread", "<init>", "()V");
    std::vector<u1> ctor = {0x2A, 0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF), 0xB1};
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
    cb.addNativeMethod("run", "()V", ACC_PUBLIC | ACC_NATIVE);
    std::vector<u1> bytes = cb.build();

    jclass defined = env->DefineClass(internal.c_str(), mcLoader, reinterpret_cast<const jbyte*>(bytes.data()),
                                      static_cast<jsize>(bytes.size()));
    if (!defined) {
        LogAndClearException(env, "BServer/DefineMainGate");
        return false;
    }
    JNINativeMethod nats[] = {
        {const_cast<char*>("run"), const_cast<char*>("()V"), reinterpret_cast<void*>(&Native_MainThreadGate_run)},
    };
    if (env->RegisterNatives(defined, nats, 1) != 0) {
        LogAndClearException(env, "BServer/RegisterMainGate");
        env->DeleteLocalRef(defined);
        return false;
    }

    server.refs.mainGateCtor = env->GetMethodID(defined, "<init>", "()V");
    server.refs.mainGateClass = static_cast<jclass>(env->NewGlobalRef(defined));
    env->DeleteLocalRef(defined);
    if (!server.refs.mainGateCtor || !server.refs.mainGateClass) {
        LogAndClearException(env, "BServer/MainGateCtor");
        return false;
    }
    LogTo("BServer: defined A main-thread gate as %s", internal.c_str());
    return true;
}

bool startLanAnnouncement(JNIEnv* env, jobject mcLoader) {
    // Use Minecraft's own multicast protocol and daemon-thread lifecycle.
    if (env->PushLocalFrame(8) != JNI_OK) {
        LogAndClearException(env, "LAN/local frame");
        return false;
    }
    bool started = false;
    jclass pingerCls = loadOrFind(env, mcLoader, "net.minecraft.client.server.LanServerPinger",
                                  "Lnet/minecraft/client/server/LanServerPinger;");
    if (pingerCls && !env->ExceptionCheck()) {
        jmethodID ctor = env->GetMethodID(pingerCls, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
        jmethodID start = nullptr;
        if (ctor && !env->ExceptionCheck())
            start = env->GetMethodID(pingerCls, "start", "()V");
        if (start && !env->ExceptionCheck()) {
            char portText[8];
            std::snprintf(portText, sizeof(portText), "%d", (int)kProxyPort);
            jstring motd = env->NewStringUTF(kLanMotd);
            jstring port = nullptr;
            if (motd && !env->ExceptionCheck())
                port = env->NewStringUTF(portText);
            if (port && !env->ExceptionCheck()) {
                jobject pinger = env->NewObject(pingerCls, ctor, motd, port);
                if (pinger && !env->ExceptionCheck()) {
                    env->CallVoidMethod(pinger, start);
                    started = !env->ExceptionCheck();
                    if (started)
                        server.lanPinger = env->NewGlobalRef(pinger);
                }
            }
        }
    }
    LogAndClearException(env, "LAN/start announcement");
    env->PopLocalFrame(nullptr);
    if (started)
        LogTo("LAN: announcing MOTD '%s', port %d", kLanMotd, (int)kProxyPort);
    else
        LogTo("LAN: announcement unavailable; direct connections remain available");
    return started;
}

bool bindServer(JNIEnv* env, jobject mcLoader) {
    jclass elgCls =
        loadOrFind(env, mcLoader, "io.netty.channel.nio.NioEventLoopGroup", "Lio/netty/channel/nio/NioEventLoopGroup;");
    jclass sbCls =
        loadOrFind(env, mcLoader, "io.netty.bootstrap.ServerBootstrap", "Lio/netty/bootstrap/ServerBootstrap;");
    jclass sscCls = loadOrFind(env, mcLoader, "io.netty.channel.socket.nio.NioServerSocketChannel",
                               "Lio/netty/channel/socket/nio/NioServerSocketChannel;");
    jclass isaCls = env->FindClass("java/net/InetSocketAddress");
    if (!elgCls || !sbCls || !sscCls || !isaCls)
        return false;

    jobject elg = env->NewObject(elgCls, env->GetMethodID(elgCls, "<init>", "()V"));
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "NioELG");
        return false;
    }
    server.eventLoop = env->NewGlobalRef(elg);
    if (!server.eventLoop)
        return false;

    jobject sb = env->NewObject(sbCls, env->GetMethodID(sbCls, "<init>", "()V"));
    env->CallObjectMethod(
        sb, env->GetMethodID(sbCls, "group", "(Lio/netty/channel/EventLoopGroup;)Lio/netty/bootstrap/ServerBootstrap;"),
        elg);
    env->CallObjectMethod(
        sb, env->GetMethodID(sbCls, "channel", "(Ljava/lang/Class;)Lio/netty/bootstrap/AbstractBootstrap;"), sscCls);
    jobject init = env->NewObject(server.refs.initClass, server.refs.initCtor);
    env->CallObjectMethod(sb,
                          env->GetMethodID(sbCls, "childHandler",
                                           "(Lio/netty/channel/ChannelHandler;)Lio/netty/bootstrap/ServerBootstrap;"),
                          init);
    env->DeleteLocalRef(init);

    jstring host = env->NewStringUTF("0.0.0.0");
    jobject isa =
        env->NewObject(isaCls, env->GetMethodID(isaCls, "<init>", "(Ljava/lang/String;I)V"), host, kProxyPort);
    env->DeleteLocalRef(host);

    jobject future = env->CallObjectMethod(
        sb, env->GetMethodID(sbCls, "bind", "(Ljava/net/SocketAddress;)Lio/netty/channel/ChannelFuture;"), isa);
    env->DeleteLocalRef(isa);
    if (env->ExceptionCheck()) {
        LogAndClearException(env, "bind");
        return false;
    }
    if (!future)
        return false;
    if (future) {
        jclass fCls = env->GetObjectClass(future);
        jobject listener =
            env->CallObjectMethod(future, env->GetMethodID(fCls, "channel", "()Lio/netty/channel/Channel;"));
        if (listener && !env->ExceptionCheck()) {
            server.listener = env->NewGlobalRef(listener);
            env->DeleteLocalRef(listener);
        }
        if (env->ExceptionCheck() || !server.listener)
            return false;
        env->CallObjectMethod(future, env->GetMethodID(fCls, "sync", "()Lio/netty/channel/ChannelFuture;"));
        if (env->ExceptionCheck()) {
            LogAndClearException(env, "bind-sync");
            return false;
        }
        env->DeleteLocalRef(fCls);
        env->DeleteLocalRef(future);
    }
    env->DeleteLocalRef(sb);
    env->DeleteLocalRef(elg);
    LogTo("BServer: bound 0.0.0.0:25565 (all interfaces, LAN-wide)");
    return true;
}

} // namespace proxy_server
