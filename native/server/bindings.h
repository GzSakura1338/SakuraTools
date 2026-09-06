#pragma once

#include "proxy.h"

namespace proxy_server {

struct PacketWriterBindings {
    jmethodID write = nullptr, writeAndFlush = nullptr, close = nullptr;
    jmethodID addListener = nullptr, isDone = nullptr, isSuccess = nullptr, isOpen = nullptr;
    jobject closeOnFailure = nullptr;
};

// Global JNI references and IDs are retained for the resident DLL's lifetime.
// Session shutdown releases connections and players, but keeps these bindings.
struct JavaBindings {
    PacketWriterBindings writer;
    jclass initClass = nullptr;
    jmethodID initCtor = nullptr;
    jclass handlerClass = nullptr;
    jmethodID handlerCtor = nullptr;
    jclass mainGateClass = nullptr;
    jmethodID mainGateCtor = nullptr;
    jclass connectionCls = nullptr;
    jmethodID connectionConfigureSerMid = nullptr;
    jmethodID connectionSendMid = nullptr;
    jfieldID connectionAttrProtocolFid = nullptr;
    jobject protoHandshaking = nullptr;
    jobject protoLogin = nullptr;
    jobject protoPlay = nullptr;
    jobject protoStatus = nullptr;
    jobject statusResponse = nullptr;
    jobject statusClassLoader = nullptr;
    jmethodID mcGetCurrentServerMid = nullptr;
    jfieldID serverDataNameFid = nullptr;
    jfieldID serverDataAddressFid = nullptr;
    jmethodID contextChannelMid = nullptr;
    jclass pongResponsePacketCls = nullptr;
    jmethodID pongResponsePacketCtor = nullptr;
    jclass statusRequestPacketCls = nullptr;
    jclass pingRequestPacketCls = nullptr;
    jfieldID pingRequestPacketTimeFid = nullptr;
    jfieldID intentionPacketIntentFid = nullptr;
    jobject flowServerbound = nullptr;
    jobject flowClientbound = nullptr;
    jclass channelCls = nullptr;
    jmethodID channelPipelineMid = nullptr;
    jmethodID channelWriteAndFlushMid = nullptr;
    jmethodID channelAttrMid = nullptr;
    jmethodID channelConfigMid = nullptr;
    jmethodID channelCloseMid = nullptr;
    jfieldID connectionChannelFid = nullptr;
    jmethodID configSetOptionMid = nullptr;
    jobject tcpNoDelayOption = nullptr;
    jobject booleanTrue = nullptr;
    jclass pipelineCls = nullptr;
    jmethodID pipelineAddLastMid = nullptr;
    jmethodID pipelineRemoveNameMid = nullptr;
    jmethodID pipelineGetHandlerMid = nullptr;
    jclass attributeCls = nullptr;
    jmethodID attributeSetMid = nullptr;
    jclass bundlerInfoCls = nullptr;
    jfieldID bundlerProviderFid = nullptr;
    jclass gameProfileCls = nullptr;
    jmethodID gameProfileCtor = nullptr;
    jclass minecraftCls = nullptr;
    jmethodID mcGetInstanceMid = nullptr;
    jmethodID mcGetProfilePropsMid = nullptr;
    jclass friendlyBufCls = nullptr;
    jmethodID friendlyBufCtor = nullptr;
    jmethodID fbbWriteByteMid = nullptr;
    jmethodID fbbWriteBooleanMid = nullptr;
    jmethodID fbbWriteVarIntMid = nullptr;
    jmethodID fbbWriteUUIDMid = nullptr;
    jmethodID fbbWriteUtfMid = nullptr;
    jmethodID fbbWriteGpPropsMid = nullptr;
    jclass unpooledCls = nullptr;
    jmethodID unpooledBufferMid = nullptr;
    jclass playerInfoUpdatePacketCls = nullptr;
    jmethodID playerInfoUpdatePacketBufCtor = nullptr;
    jmethodID playerInfoUpdatePacketWriteMid = nullptr;
    jfieldID piuEntriesField = nullptr;
    jclass piEntryCls = nullptr;
    jmethodID piEntryProfileIdMid = nullptr;
    jmethodID piEntryGameModeMid = nullptr;
    jmethodID piEntryLatencyMid = nullptr;
    jmethodID piEntryDisplayNameMid = nullptr;
    jmethodID gameTypeGetIdMid = nullptr;
    jmethodID fbbWriteComponentMid = nullptr;
    jmethodID listSizeMid = nullptr;
    jmethodID listGetMid = nullptr;
    jmethodID byteBufGetByteMid = nullptr;
    jmethodID byteBufReleaseMid = nullptr;
    jclass customPayloadPacketCls = nullptr;
    jclass setPlayerTeamPacketCls = nullptr;
    jclass playLoginPacketCls = nullptr;
    jmethodID setPlayerTeamCtor = nullptr;
    jfieldID setPlayerTeamParametersFid = nullptr;
    jfieldID setPlayerTeamMethodFid = nullptr;
    jfieldID setPlayerTeamNameFid = nullptr;
    jfieldID setPlayerTeamPlayersFid = nullptr;
    jmethodID userGetGameProfileMid = nullptr;
    jmethodID gameProfileGetNameMid = nullptr;
    jclass userCls = nullptr;
    jmethodID userGetProfileIdMid = nullptr;
    jmethodID mcGetUserMid = nullptr;
    jmethodID uuidGetMsbMid = nullptr;
    jmethodID uuidGetLsbMid = nullptr;
    jclass loginFinishedPacketCls = nullptr;
    jmethodID loginFinishedPacketCtor = nullptr;
    jclass helloPacketCls = nullptr;
    jfieldID helloPacketNameFid = nullptr;
    jclass intentPacketCls = nullptr;
    jclass uuidCls = nullptr;
    jmethodID uuidNameUuidFromBytesMid = nullptr;
    jclass bundlePacketCls = nullptr;
    jmethodID bundleSubPacketsMid = nullptr;
    jmethodID iterableIteratorMid = nullptr;
    jmethodID iteratorHasNextMid = nullptr;
    jmethodID iteratorNextMid = nullptr;
    jmethodID mcGetConnectionMid = nullptr;
    jmethodID cplGetConnectionMid = nullptr;
    jmethodID teleportAckId = nullptr;
};

} // namespace proxy_server
