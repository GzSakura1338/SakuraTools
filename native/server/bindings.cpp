#include "internal.h"
#include "mapped_method.h"
#include "status_response.h"
#include "world_snapshot.h"

namespace proxy_server {

static jfieldID mappedField(JNIEnv* env, jclass type, const char* named, const char* srg, const char* descriptor) {
    jfieldID field = env->GetFieldID(type, named, descriptor);
    if (!field) {
        env->ExceptionClear();
        field = env->GetFieldID(type, srg, descriptor);
    }
    if (!field) LogAndClearException(env, named);
    return field;
}

std::u16string currentServerMotd(JNIEnv* env) {
    std::u16string name, address;
    if (env->PushLocalFrame(8) != JNI_OK) {
        env->ExceptionClear();
        return FormatServerMotd(name, address);
    }
    if (server.refs.minecraftCls && server.refs.mcGetInstanceMid && server.refs.mcGetCurrentServerMid) {
        jobject mc = env->CallStaticObjectMethod(server.refs.minecraftCls, server.refs.mcGetInstanceMid);
        if (mc && !env->ExceptionCheck()) {
            jobject data = env->CallObjectMethod(mc, server.refs.mcGetCurrentServerMid);
            if (data && !env->ExceptionCheck()) {
                auto read = [&](jfieldID field) -> std::u16string {
                    if (!field || env->ExceptionCheck())
                        return {};
                    jstring value = static_cast<jstring>(env->GetObjectField(data, field));
                    if (!value || env->ExceptionCheck())
                        return {};
                    const jsize length = env->GetStringLength(value);
                    const jchar* chars = env->GetStringChars(value, nullptr);
                    if (!chars)
                        return {};
                    std::u16string result(chars, chars + length);
                    env->ReleaseStringChars(value, chars);
                    return result;
                };
                name = read(server.refs.serverDataNameFid);
                address = read(server.refs.serverDataAddressFid);
            }
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->PopLocalFrame(nullptr);
    return FormatServerMotd(name, address);
}

bool cacheStatusResponse(JNIEnv* env, jobject mcLoader) {
    server.refs.statusClassLoader = env->NewGlobalRef(mcLoader);
    if (!server.refs.statusClassLoader) {
        env->ExceptionClear();
        return false;
    }
    jobject response = BuildStatusResponse(env, mcLoader, currentServerMotd(env));
    if (response) {
        server.refs.statusResponse = env->NewGlobalRef(response);
        env->DeleteLocalRef(response);
    }
    if (!server.refs.statusResponse) {
        LogTo("STATUS: response unavailable; gameplay listener will still start");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
    return server.refs.statusResponse != nullptr;
}

bool refreshMidSessionRefs(JNIEnv* env, jobject mcLoader) {
    if (!InstallWorldSnapshot(env, mcLoader))
        return false;
    jclass teleport =
        LoadClassInLoader(env, mcLoader, "net.minecraft.network.protocol.game.ServerboundAcceptTeleportationPacket");
    if (!teleport)
        return false;
    server.refs.teleportAckId = FindMappedMethod(env, teleport, "getId", "m_133795_", "()I");
    env->DeleteLocalRef(teleport);
    if (!server.refs.teleportAckId) {
        LogAndClearException(env, "snapshot/teleport acknowledgement");
        return false;
    }

    if (!server.refs.minecraftCls)
        return false;
    server.refs.mcGetConnectionMid = FindMappedMethod(env, server.refs.minecraftCls, "getConnection", "m_91403_",
                                                      "()Lnet/minecraft/client/multiplayer/ClientPacketListener;");
    if (!server.refs.mcGetConnectionMid) {
        LogAndClearException(env, "mid-session/Minecraft.getConnection");
        return false;
    }
    jclass listener = loadOrFind(env, mcLoader, "net.minecraft.client.multiplayer.ClientPacketListener",
                                 "Lnet/minecraft/client/multiplayer/ClientPacketListener;");
    if (!listener)
        return false;
    server.refs.cplGetConnectionMid =
        FindMappedMethod(env, listener, "getConnection", "m_104910_", "()Lnet/minecraft/network/Connection;");
    env->DeleteLocalRef(listener);
    if (!server.refs.cplGetConnectionMid) {
        LogAndClearException(env, "mid-session/ClientPacketListener.getConnection");
        return false;
    }
    return server.refs.mcGetInstanceMid && server.refs.connectionChannelFid && server.refs.channelPipelineMid;
}

static bool cacheProtocolBindings(JNIEnv* env, jobject mcLoader) {
    jclass connCls =
        loadOrFind(env, mcLoader, "net.minecraft.network.Connection", "Lnet/minecraft/network/Connection;");
    if (!connCls)
        return false;
    server.refs.connectionCls = static_cast<jclass>(env->NewGlobalRef(connCls));
    server.refs.connectionConfigureSerMid = FindMappedMethod(env,
        connCls, "configureSerialization", "m_264299_", "(Lio/netty/channel/ChannelPipeline;Lnet/minecraft/network/protocol/PacketFlow;)V", true);
    server.refs.connectionSendMid =
        FindMappedMethod(env, connCls, "send", "m_129512_", "(Lnet/minecraft/network/protocol/Packet;)V");
    server.refs.connectionAttrProtocolFid = findFieldByDescriptor(connCls, "Lio/netty/util/AttributeKey;", true);

    server.refs.connectionChannelFid = mappedField(env, connCls, "channel", "f_129468_", "Lio/netty/channel/Channel;");
    env->DeleteLocalRef(connCls);

    jclass bundlerCls = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.BundlerInfo",
                                   "Lnet/minecraft/network/protocol/BundlerInfo;");
    if (bundlerCls) {
        server.refs.bundlerInfoCls = static_cast<jclass>(env->NewGlobalRef(bundlerCls));
        server.refs.bundlerProviderFid = findFieldByDescriptor(bundlerCls, "Lio/netty/util/AttributeKey;", true);
        env->DeleteLocalRef(bundlerCls);
    } else {
        LogTo("BServer: BundlerInfo not found (bundle attr not set — may cause issues on modern servers)");
    }

    jclass protoCls = loadOrFind(env, mcLoader, "net.minecraft.network.ConnectionProtocol",
                                 "Lnet/minecraft/network/ConnectionProtocol;");
    if (protoCls) {
        auto readEnum = [&](const char* n) -> jobject {
            jfieldID f = env->GetStaticFieldID(protoCls, n, "Lnet/minecraft/network/ConnectionProtocol;");
            if (!f)
                return nullptr;
            jobject v = env->GetStaticObjectField(protoCls, f);
            return v ? env->NewGlobalRef(v) : nullptr;
        };
        server.refs.protoHandshaking = readEnum("HANDSHAKING");
        server.refs.protoLogin = readEnum("LOGIN");
        server.refs.protoPlay = readEnum("PLAY");
        server.refs.protoStatus = readEnum("STATUS");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(protoCls);
    }

    jclass flowCls = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.PacketFlow",
                                "Lnet/minecraft/network/protocol/PacketFlow;");
    if (!flowCls)
        return false;
    jfieldID fSB = env->GetStaticFieldID(flowCls, "SERVERBOUND", "Lnet/minecraft/network/protocol/PacketFlow;");
    jfieldID fCB = env->GetStaticFieldID(flowCls, "CLIENTBOUND", "Lnet/minecraft/network/protocol/PacketFlow;");
    if (fSB)
        server.refs.flowServerbound = env->NewGlobalRef(env->GetStaticObjectField(flowCls, fSB));
    if (fCB)
        server.refs.flowClientbound = env->NewGlobalRef(env->GetStaticObjectField(flowCls, fCB));
    env->DeleteLocalRef(flowCls);
    return true;
}

static bool cacheChannelBindings(JNIEnv* env, jobject mcLoader) {
    jclass chCls = loadOrFind(env, mcLoader, "io.netty.channel.Channel", "Lio/netty/channel/Channel;");
    if (!chCls)
        return false;
    server.refs.channelCls = static_cast<jclass>(env->NewGlobalRef(chCls));
    server.refs.channelPipelineMid = env->GetMethodID(chCls, "pipeline", "()Lio/netty/channel/ChannelPipeline;");
    server.refs.channelWriteAndFlushMid =
        env->GetMethodID(chCls, "writeAndFlush", "(Ljava/lang/Object;)Lio/netty/channel/ChannelFuture;");
    server.refs.channelAttrMid =
        env->GetMethodID(chCls, "attr", "(Lio/netty/util/AttributeKey;)Lio/netty/util/Attribute;");
    server.refs.channelConfigMid = env->GetMethodID(chCls, "config", "()Lio/netty/channel/ChannelConfig;");
    server.refs.channelCloseMid = env->GetMethodID(chCls, "close", "()Lio/netty/channel/ChannelFuture;");
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(chCls);

    jclass contextCls = LoadClassInLoader(env, mcLoader, "io.netty.channel.ChannelHandlerContext");
    if (!contextCls)
        return false;
    server.refs.contextChannelMid = env->GetMethodID(contextCls, "channel", "()Lio/netty/channel/Channel;");
    env->DeleteLocalRef(contextCls);
    if (!server.refs.contextChannelMid || env->ExceptionCheck())
        return false;

    jclass cfgCls = loadOrFind(env, mcLoader, "io.netty.channel.ChannelConfig", "Lio/netty/channel/ChannelConfig;");
    if (cfgCls) {
        server.refs.configSetOptionMid =
            env->GetMethodID(cfgCls, "setOption", "(Lio/netty/channel/ChannelOption;Ljava/lang/Object;)Z");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(cfgCls);
    }
    jclass optCls = loadOrFind(env, mcLoader, "io.netty.channel.ChannelOption", "Lio/netty/channel/ChannelOption;");
    if (optCls) {
        jfieldID f = env->GetStaticFieldID(optCls, "TCP_NODELAY", "Lio/netty/channel/ChannelOption;");
        if (f) {
            jobject v = env->GetStaticObjectField(optCls, f);
            if (v)
                server.refs.tcpNoDelayOption = env->NewGlobalRef(v);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(optCls);
    }
    jclass boolCls = env->FindClass("java/lang/Boolean");
    if (boolCls) {
        jfieldID f = env->GetStaticFieldID(boolCls, "TRUE", "Ljava/lang/Boolean;");
        if (f) {
            jobject v = env->GetStaticObjectField(boolCls, f);
            if (v)
                server.refs.booleanTrue = env->NewGlobalRef(v);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(boolCls);
    }

    jclass pipCls = loadOrFind(env, mcLoader, "io.netty.channel.ChannelPipeline", "Lio/netty/channel/ChannelPipeline;");
    if (!pipCls)
        return false;
    server.refs.pipelineCls = static_cast<jclass>(env->NewGlobalRef(pipCls));
    server.refs.pipelineAddLastMid = env->GetMethodID(
        pipCls, "addLast", "(Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
    server.refs.pipelineRemoveNameMid =
        env->GetMethodID(pipCls, "remove", "(Ljava/lang/String;)Lio/netty/channel/ChannelHandler;");
    server.refs.pipelineGetHandlerMid =
        env->GetMethodID(pipCls, "get", "(Ljava/lang/String;)Lio/netty/channel/ChannelHandler;");
    env->DeleteLocalRef(pipCls);

    jclass attrCls = loadOrFind(env, mcLoader, "io.netty.util.Attribute", "Lio/netty/util/Attribute;");
    if (!attrCls)
        return false;
    server.refs.attributeCls = static_cast<jclass>(env->NewGlobalRef(attrCls));
    server.refs.attributeSetMid = env->GetMethodID(attrCls, "set", "(Ljava/lang/Object;)V");
    env->DeleteLocalRef(attrCls);
    return true;
}

static void cachePlayerBindings(JNIEnv* env, jobject mcLoader) {
    jclass gpCls = loadOrFind(env, mcLoader, "com.mojang.authlib.GameProfile", "Lcom/mojang/authlib/GameProfile;");
    if (gpCls) {
        server.refs.gameProfileCls = static_cast<jclass>(env->NewGlobalRef(gpCls));
        server.refs.gameProfileCtor = env->GetMethodID(gpCls, "<init>", "(Ljava/util/UUID;Ljava/lang/String;)V");

        server.refs.gameProfileGetNameMid = env->GetMethodID(gpCls, "getName", "()Ljava/lang/String;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(gpCls);
    }

    jclass mcCls = loadOrFind(env, mcLoader, "net.minecraft.client.Minecraft", "Lnet/minecraft/client/Minecraft;");
    if (mcCls) {
        server.refs.minecraftCls = static_cast<jclass>(env->NewGlobalRef(mcCls));
        server.refs.mcGetInstanceMid = FindMappedMethod(env, mcCls, "getInstance", "m_91087_", "()Lnet/minecraft/client/Minecraft;", true);
        server.refs.mcGetProfilePropsMid =
            FindMappedMethod(env, mcCls, "getProfileProperties", "m_91095_", "()Lcom/mojang/authlib/properties/PropertyMap;");
        server.refs.mcGetUserMid = FindMappedMethod(env, mcCls, "getUser", "m_91094_", "()Lnet/minecraft/client/User;");
        server.refs.mcGetCurrentServerMid =
            env->GetMethodID(mcCls, "getCurrentServer", "()Lnet/minecraft/client/multiplayer/ServerData;");
        if (!server.refs.mcGetCurrentServerMid) {
            env->ExceptionClear();
            server.refs.mcGetCurrentServerMid =
                env->GetMethodID(mcCls, "m_91089_", "()Lnet/minecraft/client/multiplayer/ServerData;");
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(mcCls);
    }
    jclass serverData = LoadClassInLoader(env, mcLoader, "net.minecraft.client.multiplayer.ServerData");
    if (serverData) {
        auto field = [&](const char* named, const char* srg) {
            jfieldID id = env->GetFieldID(serverData, named, "Ljava/lang/String;");
            if (!id) {
                env->ExceptionClear();
                id = env->GetFieldID(serverData, srg, "Ljava/lang/String;");
            }
            if (env->ExceptionCheck())
                env->ExceptionClear();
            return id;
        };
        server.refs.serverDataNameFid = field("name", "f_105362_");
        server.refs.serverDataAddressFid = field("ip", "f_105363_");
        env->DeleteLocalRef(serverData);
    }
    jclass userCls = loadOrFind(env, mcLoader, "net.minecraft.client.User", "Lnet/minecraft/client/User;");
    if (userCls) {
        server.refs.userCls = static_cast<jclass>(env->NewGlobalRef(userCls));
        server.refs.userGetProfileIdMid = FindMappedMethod(env, userCls, "getProfileId", "m_240411_", "()Ljava/util/UUID;");

        server.refs.userGetGameProfileMid =
            FindMappedMethod(env, userCls, "getGameProfile", "m_92548_", "()Lcom/mojang/authlib/GameProfile;");
        env->DeleteLocalRef(userCls);
    }
}


static void cacheLoginStatusBindings(JNIEnv* env, jobject mcLoader) {
    jclass lfp = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.login.ClientboundGameProfilePacket",
                            "Lnet/minecraft/network/protocol/login/ClientboundGameProfilePacket;");
    if (lfp) {
        server.refs.loginFinishedPacketCls = static_cast<jclass>(env->NewGlobalRef(lfp));

        server.refs.loginFinishedPacketCtor = findMethodByDescriptor(lfp, "(Lcom/mojang/authlib/GameProfile;)V", false);
        env->DeleteLocalRef(lfp);
    }

    jclass hello = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.login.ServerboundHelloPacket",
                              "Lnet/minecraft/network/protocol/login/ServerboundHelloPacket;");
    if (hello) {
        server.refs.helloPacketCls = static_cast<jclass>(env->NewGlobalRef(hello));
        server.refs.helloPacketNameFid = findFieldByDescriptor(hello, "Ljava/lang/String;", false);
        env->DeleteLocalRef(hello);
    }

    jclass intent = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.handshake.ClientIntentionPacket",
                               "Lnet/minecraft/network/protocol/handshake/ClientIntentionPacket;");
    if (intent) {
        server.refs.intentPacketCls = static_cast<jclass>(env->NewGlobalRef(intent));
        server.refs.intentionPacketIntentFid =
            findFieldByDescriptor(intent, "Lnet/minecraft/network/ConnectionProtocol;", false);
        env->DeleteLocalRef(intent);
    }

    jclass sReq = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.status.ServerboundStatusRequestPacket",
                             "Lnet/minecraft/network/protocol/status/ServerboundStatusRequestPacket;");
    if (sReq) {
        server.refs.statusRequestPacketCls = static_cast<jclass>(env->NewGlobalRef(sReq));
        env->DeleteLocalRef(sReq);
    }

    jclass pReq = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.status.ServerboundPingRequestPacket",
                             "Lnet/minecraft/network/protocol/status/ServerboundPingRequestPacket;");
    if (pReq) {
        server.refs.pingRequestPacketCls = static_cast<jclass>(env->NewGlobalRef(pReq));
        server.refs.pingRequestPacketTimeFid = findFieldByDescriptor(pReq, "J", false);
        env->DeleteLocalRef(pReq);
    }

    jclass pong = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.status.ClientboundPongResponsePacket",
                             "Lnet/minecraft/network/protocol/status/ClientboundPongResponsePacket;");
    if (pong) {
        server.refs.pongResponsePacketCls = static_cast<jclass>(env->NewGlobalRef(pong));
        server.refs.pongResponsePacketCtor = findMethodByDescriptor(pong, "(J)V", false);
        env->DeleteLocalRef(pong);
    }
}

static void cacheBundleBindings(JNIEnv* env, jobject mcLoader) {
    jclass cbp = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.game.ClientboundBundlePacket",
                            "Lnet/minecraft/network/protocol/game/ClientboundBundlePacket;");
    if (cbp) {
        server.refs.bundlePacketCls = static_cast<jclass>(env->NewGlobalRef(cbp));
        jclass bp = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.BundlePacket",
                               "Lnet/minecraft/network/protocol/BundlePacket;");
        if (bp) {
            server.refs.bundleSubPacketsMid = findMethodByDescriptor(bp, "()Ljava/lang/Iterable;", false);
            env->DeleteLocalRef(bp);
        }
        env->DeleteLocalRef(cbp);
    } else {
        LogTo("BServer: ClientboundBundlePacket not found — bundles won't be expanded");
    }
    jclass iterableCls = env->FindClass("java/lang/Iterable");
    if (iterableCls) {
        server.refs.iterableIteratorMid = env->GetMethodID(iterableCls, "iterator", "()Ljava/util/Iterator;");
        env->DeleteLocalRef(iterableCls);
    }
    jclass iteratorCls = env->FindClass("java/util/Iterator");
    if (iteratorCls) {
        server.refs.iteratorHasNextMid = env->GetMethodID(iteratorCls, "hasNext", "()Z");
        server.refs.iteratorNextMid = env->GetMethodID(iteratorCls, "next", "()Ljava/lang/Object;");
        env->DeleteLocalRef(iteratorCls);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();

    jclass uuidCls = env->FindClass("java/util/UUID");
    if (uuidCls) {
        server.refs.uuidCls = static_cast<jclass>(env->NewGlobalRef(uuidCls));
        server.refs.uuidNameUuidFromBytesMid =
            env->GetStaticMethodID(uuidCls, "nameUUIDFromBytes", "([B)Ljava/util/UUID;");
        env->DeleteLocalRef(uuidCls);
    }
}

bool cacheJavaRefs(JNIEnv* env, jobject mcLoader) {
    if (!cacheProtocolBindings(env, mcLoader))
        return false;
    if (!cacheChannelBindings(env, mcLoader))
        return false;
    if (!cachePacketWriter(env, mcLoader))
        return false;
    cachePlayerBindings(env, mcLoader);
    cachePacketBindings(env, mcLoader);
    cacheLoginStatusBindings(env, mcLoader);
    cacheBundleBindings(env, mcLoader);

    LogTo("cacheJavaRefs: configureSer=%p send=%p attrProtoFid=%p bundlerFid=%p "
          "protoHS=%p protoLOGIN=%p protoPLAY=%p flowSB=%p pipMid=%p addLast=%p "
          "attrSet=%p gpCtor=%p lfpCtor=%p helloName=%p uuidFromBytes=%p",
          (void*)server.refs.connectionConfigureSerMid, (void*)server.refs.connectionSendMid,
          (void*)server.refs.connectionAttrProtocolFid, (void*)server.refs.bundlerProviderFid,
          (void*)server.refs.protoHandshaking, (void*)server.refs.protoLogin, (void*)server.refs.protoPlay,
          (void*)server.refs.flowServerbound, (void*)server.refs.channelPipelineMid,
          (void*)server.refs.pipelineAddLastMid, (void*)server.refs.attributeSetMid, (void*)server.refs.gameProfileCtor,
          (void*)server.refs.loginFinishedPacketCtor, (void*)server.refs.helloPacketNameFid,
          (void*)server.refs.uuidNameUuidFromBytesMid);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (!validateRequiredBindings()) return false;
    cacheStatusResponse(env, mcLoader);
    return true;
}

} // namespace proxy_server
