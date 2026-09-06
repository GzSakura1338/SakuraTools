#include "internal.h"
#include "mapped_method.h"

namespace proxy_server {
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
            FindMappedMethod(env, fbbCls, "writeVarInt", "m_130130_", "(I)Lnet/minecraft/network/FriendlyByteBuf;");
        server.refs.fbbWriteUUIDMid = FindMappedMethod(env, fbbCls, "writeUUID", "m_130077_",
                                                       "(Ljava/util/UUID;)Lnet/minecraft/network/FriendlyByteBuf;");
        server.refs.fbbWriteUtfMid = FindMappedMethod(env, fbbCls, "writeUtf", "m_130072_",
                                                      "(Ljava/lang/String;I)Lnet/minecraft/network/FriendlyByteBuf;");
        server.refs.fbbWriteGpPropsMid = FindMappedMethod(env, fbbCls, "writeGameProfileProperties", "m_246636_",
                                                          "(Lcom/mojang/authlib/properties/PropertyMap;)V");
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

        server.refs.playerInfoUpdatePacketWriteMid =
            FindMappedMethod(env, piuCls, "write", "m_5779_", "(Lnet/minecraft/network/FriendlyByteBuf;)V");

        server.refs.piuEntriesField = env->GetFieldID(piuCls, "entries", "Ljava/util/List;");
        if (!server.refs.piuEntriesField) {
            env->ExceptionClear();
            server.refs.piuEntriesField = env->GetFieldID(piuCls, "f_244436_", "Ljava/util/List;");
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(piuCls);
    }

    jclass piEntryCls =
        loadOrFind(env, mcLoader, "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Entry",
                   "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket$Entry;");
    if (piEntryCls) {
        server.refs.piEntryCls = static_cast<jclass>(env->NewGlobalRef(piEntryCls));
        server.refs.piEntryProfileIdMid =
            FindMappedMethod(env, piEntryCls, "profileId", "f_244142_", "()Ljava/util/UUID;");
        server.refs.piEntryGameModeMid =
            FindMappedMethod(env, piEntryCls, "gameMode", "f_244162_", "()Lnet/minecraft/world/level/GameType;");
        server.refs.piEntryLatencyMid = FindMappedMethod(env, piEntryCls, "latency", "f_244322_", "()I");
        server.refs.piEntryDisplayNameMid =
            FindMappedMethod(env, piEntryCls, "displayName", "f_244512_", "()Lnet/minecraft/network/chat/Component;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(piEntryCls);
    }

    jclass gameTypeCls =
        loadOrFind(env, mcLoader, "net.minecraft.world.level.GameType", "Lnet/minecraft/world/level/GameType;");
    if (gameTypeCls) {
        server.refs.gameTypeGetIdMid = FindMappedMethod(env, gameTypeCls, "getId", "m_46392_", "()I");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(gameTypeCls);
    }

    if (server.refs.friendlyBufCls) {
        server.refs.fbbWriteComponentMid =
            FindMappedMethod(env, server.refs.friendlyBufCls, "writeComponent", "m_130083_",
                             "(Lnet/minecraft/network/chat/Component;)Lnet/minecraft/network/FriendlyByteBuf;");
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
            server.refs.byteBufReleaseMid = env->GetMethodID(bbCls, "release", "()Z");
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
        server.refs.setPlayerTeamCtor =
            env->GetMethodID(sptCls, "<init>", "(Ljava/lang/String;ILjava/util/Optional;Ljava/util/Collection;)V");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        server.refs.setPlayerTeamParametersFid = findFieldByDescriptor(sptCls, "Ljava/util/Optional;", false);
        server.refs.setPlayerTeamMethodFid = findFieldByDescriptor(sptCls, "I", false);
        server.refs.setPlayerTeamNameFid = findFieldByDescriptor(sptCls, "Ljava/lang/String;", false);
        server.refs.setPlayerTeamPlayersFid = findFieldByDescriptor(sptCls, "Ljava/util/Collection;", false);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(sptCls);
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

void cachePacketBindings(JNIEnv* env, jobject loader) {
    jclass login = LoadClassInLoader(env, loader, "net.minecraft.network.protocol.game.ClientboundLoginPacket");
    if (login) {
        server.refs.playLoginPacketCls = static_cast<jclass>(env->NewGlobalRef(login));
        env->DeleteLocalRef(login);
    }
    LogAndClearException(env, "bindings/play login");
    cacheBufferBindings(env, loader);
    cachePlayerPacketBindings(env, loader);
}
} // namespace proxy_server
