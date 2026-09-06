#include "internal.h"
#include "mapped_method.h"
#include "status_response.h"
#include "world_snapshot.h"

namespace proxy_server {

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
    server.refs.connectionConfigureSerMid = findMethodByDescriptor(
        connCls, "(Lio/netty/channel/ChannelPipeline;Lnet/minecraft/network/protocol/PacketFlow;)V", true);
    server.refs.connectionSendMid =
        findMethodByDescriptor(connCls, "(Lnet/minecraft/network/protocol/Packet;)V", false);
    server.refs.connectionAttrProtocolFid = findFieldByDescriptor(connCls, "Lio/netty/util/AttributeKey;", true);

    server.refs.connectionChannelFid = findFieldByDescriptor(connCls, "Lio/netty/channel/Channel;", false);
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
        server.refs.mcGetInstanceMid = findMethodByDescriptor(mcCls, "()Lnet/minecraft/client/Minecraft;", true);
        server.refs.mcGetProfilePropsMid =
            findMethodByDescriptor(mcCls, "()Lcom/mojang/authlib/properties/PropertyMap;", false);
        server.refs.mcGetUserMid = findMethodByDescriptor(mcCls, "()Lnet/minecraft/client/User;", false);
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
        server.refs.userGetProfileIdMid = findMethodByDescriptor(userCls, "()Ljava/util/UUID;", false);

        server.refs.userGetGameProfileMid =
            findMethodByDescriptor(userCls, "()Lcom/mojang/authlib/GameProfile;", false);
        env->DeleteLocalRef(userCls);
    }
}

static void cacheBufferBindings(JNIEnv* env, jobject mcLoader) {
    jclass fbbCls =
        loadOrFind(env, mcLoader, "net.minecraft.network.FriendlyByteBuf", "Lnet/minecraft/network/FriendlyByteBuf;");
    if (fbbCls) {
        server.refs.friendlyBufCls = static_cast<jclass>(env->NewGlobalRef(fbbCls));
        server.refs.friendlyBufCtor = env->GetMethodID(fbbCls, "<init>", "(Lio/netty/buffer/ByteBuf;)V");

        server.refs.fbbWriteByteMid = env->GetMethodID(fbbCls, "writeByte", "(I)Lio/netty/buffer/ByteBuf;");
        server.refs.fbbWriteBooleanMid = env->GetMethodID(fbbCls, "writeBoolean", "(Z)Lio/netty/buffer/ByteBuf;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        server.refs.fbbWriteVarIntMid =
            findMethodByDescriptor(fbbCls, "(I)Lnet/minecraft/network/FriendlyByteBuf;", false);
        server.refs.fbbWriteUUIDMid =
            findMethodByDescriptor(fbbCls, "(Ljava/util/UUID;)Lnet/minecraft/network/FriendlyByteBuf;", false);
        server.refs.fbbWriteUtfMid =
            findMethodByDescriptor(fbbCls, "(Ljava/lang/String;I)Lnet/minecraft/network/FriendlyByteBuf;", false);
        server.refs.fbbWriteGpPropsMid =
            findMethodByDescriptor(fbbCls, "(Lcom/mojang/authlib/properties/PropertyMap;)V", false);
        env->DeleteLocalRef(fbbCls);
    }
    jclass unpCls = loadOrFind(env, mcLoader, "io.netty.buffer.Unpooled", "Lio/netty/buffer/Unpooled;");
    if (unpCls) {
        server.refs.unpooledCls = static_cast<jclass>(env->NewGlobalRef(unpCls));
        server.refs.unpooledBufferMid = env->GetStaticMethodID(unpCls, "buffer", "()Lio/netty/buffer/ByteBuf;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(unpCls);
    }
}

static void cachePlayerPacketBindings(JNIEnv* env, jobject mcLoader) {
    jclass piuCls = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket",
                               "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket;");
    if (piuCls) {
        server.refs.playerInfoUpdatePacketCls = static_cast<jclass>(env->NewGlobalRef(piuCls));
        server.refs.playerInfoUpdatePacketBufCtor =
            env->GetMethodID(piuCls, "<init>", "(Lnet/minecraft/network/FriendlyByteBuf;)V");

        static const char* const kPacketWriteExcl[] = {"<init>"};
        server.refs.playerInfoUpdatePacketWriteMid = findMethodByDescriptorExcept(
            piuCls, "(Lnet/minecraft/network/FriendlyByteBuf;)V", false, kPacketWriteExcl, 1);

        jmethodID listMids[2] = {nullptr, nullptr};
        int nList = findMethodsByDescriptor(piuCls, "()Ljava/util/List;", false, listMids, 2);
        server.refs.piuEntriesMidA = listMids[0];
        server.refs.piuEntriesMidB = listMids[1];
        LogTo("  piu: %d ()List accessor(s) cached", nList);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(piuCls);
    }

    jclass piEntryCls =
        loadOrFind(env, mcLoader, "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Entry",
                   "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket$Entry;");
    if (piEntryCls) {
        server.refs.piEntryCls = static_cast<jclass>(env->NewGlobalRef(piEntryCls));
        server.refs.piEntryProfileIdMid = findMethodByDescriptor(piEntryCls, "()Ljava/util/UUID;", false);
        server.refs.piEntryGameModeMid =
            findMethodByDescriptor(piEntryCls, "()Lnet/minecraft/world/level/GameType;", false);
        static const char* const kHashExcl[] = {"hashCode"};
        server.refs.piEntryLatencyMid = findMethodByDescriptorExcept(piEntryCls, "()I", false, kHashExcl, 1);
        server.refs.piEntryDisplayNameMid =
            findMethodByDescriptor(piEntryCls, "()Lnet/minecraft/network/chat/Component;", false);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(piEntryCls);
    }

    jclass gameTypeCls =
        loadOrFind(env, mcLoader, "net.minecraft.world.level.GameType", "Lnet/minecraft/world/level/GameType;");
    if (gameTypeCls) {
        static const char* const kIdExcl[] = {"ordinal", "hashCode"};
        server.refs.gameTypeGetIdMid = findMethodByDescriptorExcept(gameTypeCls, "()I", false, kIdExcl, 2);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(gameTypeCls);
    }

    if (server.refs.friendlyBufCls) {
        server.refs.fbbWriteComponentMid = findMethodByDescriptor(
            server.refs.friendlyBufCls,
            "(Lnet/minecraft/network/chat/Component;)Lnet/minecraft/network/FriendlyByteBuf;", false);
    }

    jclass listCls = env->FindClass("java/util/List");
    if (listCls) {
        server.refs.listSizeMid = env->GetMethodID(listCls, "size", "()I");
        server.refs.listGetMid = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(listCls);
    }

    {
        jclass bbCls = loadOrFind(env, mcLoader, "io.netty.buffer.ByteBuf", "Lio/netty/buffer/ByteBuf;");
        if (bbCls) {
            server.refs.byteBufGetByteMid = env->GetMethodID(bbCls, "getByte", "(I)B");
            if (env->ExceptionCheck())
                env->ExceptionClear();
            env->DeleteLocalRef(bbCls);
        }
    }

    jclass cpp = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.game.ClientboundCustomPayloadPacket",
                            "Lnet/minecraft/network/protocol/game/ClientboundCustomPayloadPacket;");
    if (cpp) {
        server.refs.customPayloadPacketCls = static_cast<jclass>(env->NewGlobalRef(cpp));
        env->DeleteLocalRef(cpp);
    }

    jclass sptCls = loadOrFind(env, mcLoader, "net.minecraft.network.protocol.game.ClientboundSetPlayerTeamPacket",
                               "Lnet/minecraft/network/protocol/game/ClientboundSetPlayerTeamPacket;");
    if (sptCls) {
        server.refs.setPlayerTeamPacketCls = static_cast<jclass>(env->NewGlobalRef(sptCls));
        server.refs.setPlayerTeamPacketBufCtor =
            env->GetMethodID(sptCls, "<init>", "(Lnet/minecraft/network/FriendlyByteBuf;)V");
        server.refs.setPlayerTeamMethodFid = findFieldByDescriptor(sptCls, "I", false);
        server.refs.setPlayerTeamNameFid = findFieldByDescriptor(sptCls, "Ljava/lang/String;", false);
        server.refs.setPlayerTeamPlayersFid = findFieldByDescriptor(sptCls, "Ljava/util/Collection;", false);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(sptCls);
    }
    jclass collCls = env->FindClass("java/util/Collection");
    if (collCls) {
        server.refs.collectionContainsMid = env->GetMethodID(collCls, "contains", "(Ljava/lang/Object;)Z");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(collCls);
    }

    jclass uuidClsL = env->FindClass("java/util/UUID");
    if (uuidClsL) {
        server.refs.uuidGetMsbMid = env->GetMethodID(uuidClsL, "getMostSignificantBits", "()J");
        server.refs.uuidGetLsbMid = env->GetMethodID(uuidClsL, "getLeastSignificantBits", "()J");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(uuidClsL);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
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
    cachePlayerBindings(env, mcLoader);
    cacheBufferBindings(env, mcLoader);
    cachePlayerPacketBindings(env, mcLoader);
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

    cacheStatusResponse(env, mcLoader);
    return server.refs.connectionConfigureSerMid && server.refs.connectionSendMid && server.refs.flowServerbound &&
           server.refs.channelPipelineMid && server.refs.pipelineAddLastMid && server.refs.channelWriteAndFlushMid &&
           server.refs.channelAttrMid && server.refs.attributeSetMid;
}

} // namespace proxy_server
