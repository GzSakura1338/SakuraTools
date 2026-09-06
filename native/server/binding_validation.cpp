#include "internal.h"

namespace proxy_server {
bool validateRequiredBindings() {
    bool ok = true;
    const auto& r = server.refs;
#define REQUIRE(group, member)                                                                                         \
    if (!r.member) {                                                                                                   \
        LogTo("bindings: missing required " #group "." #member);                                                       \
        ok = false;                                                                                                    \
    }
    REQUIRE(protocol, connectionCls);
    REQUIRE(protocol, connectionConfigureSerMid);
    REQUIRE(protocol, connectionSendMid);
    REQUIRE(protocol, connectionAttrProtocolFid);
    REQUIRE(protocol, connectionChannelFid);
    REQUIRE(protocol, protoHandshaking);
    REQUIRE(protocol, protoLogin);
    REQUIRE(protocol, protoPlay);
    REQUIRE(protocol, protoStatus);
    REQUIRE(protocol, flowServerbound);
    REQUIRE(channel, contextChannelMid);
    REQUIRE(channel, channelPipelineMid);
    REQUIRE(channel, channelWriteAndFlushMid);
    REQUIRE(channel, channelAttrMid);
    REQUIRE(channel, channelCloseMid);
    REQUIRE(channel, pipelineAddLastMid);
    REQUIRE(channel, pipelineRemoveNameMid);
    REQUIRE(channel, pipelineGetHandlerMid);
    REQUIRE(channel, attributeSetMid);
    REQUIRE(channel, writer.closeOnFailure);
    REQUIRE(login, uuidCls);
    REQUIRE(login, playLoginPacketCls);
    REQUIRE(login, uuidNameUuidFromBytesMid);
    REQUIRE(login, uuidGetMsbMid);
    REQUIRE(login, uuidGetLsbMid);
    REQUIRE(login, gameProfileCls);
    REQUIRE(login, gameProfileCtor);
    REQUIRE(login, loginFinishedPacketCls);
    REQUIRE(login, loginFinishedPacketCtor);
    REQUIRE(login, helloPacketCls);
    REQUIRE(login, helloPacketNameFid);
    REQUIRE(login, intentPacketCls);
    REQUIRE(login, intentionPacketIntentFid);
    REQUIRE(identity, minecraftCls);
    REQUIRE(identity, mcGetInstanceMid);
    REQUIRE(identity, mcGetUserMid);
    REQUIRE(identity, userGetProfileIdMid);
    REQUIRE(identity, userGetGameProfileMid);
    REQUIRE(identity, gameProfileGetNameMid);
    REQUIRE(identity, mcGetProfilePropsMid);
    REQUIRE(buffer, friendlyBufCls);
    REQUIRE(buffer, friendlyBufCtor);
    REQUIRE(buffer, fbbWriteByteMid);
    REQUIRE(buffer, fbbWriteBooleanMid);
    REQUIRE(buffer, fbbWriteVarIntMid);
    REQUIRE(buffer, fbbWriteUUIDMid);
    REQUIRE(buffer, fbbWriteUtfMid);
    REQUIRE(buffer, fbbWriteGpPropsMid);
    REQUIRE(buffer, unpooledCls);
    REQUIRE(buffer, unpooledBufferMid);
    REQUIRE(buffer, byteBufReleaseMid);
    REQUIRE(players, playerInfoUpdatePacketCls);
    REQUIRE(players, playerInfoUpdatePacketBufCtor);
    REQUIRE(players, playerInfoUpdatePacketWriteMid);
    REQUIRE(players, piuEntriesField);
    REQUIRE(players, piEntryProfileIdMid);
    REQUIRE(players, listSizeMid);
    REQUIRE(players, listGetMid);
    REQUIRE(players, byteBufGetByteMid);
    REQUIRE(teams, setPlayerTeamPacketCls);
    REQUIRE(teams, setPlayerTeamCtor);
    REQUIRE(teams, setPlayerTeamParametersFid);
    REQUIRE(teams, setPlayerTeamMethodFid);
    REQUIRE(teams, setPlayerTeamNameFid);
    REQUIRE(teams, setPlayerTeamPlayersFid);
    REQUIRE(bundle, bundlePacketCls);
    REQUIRE(bundle, bundleSubPacketsMid);
    REQUIRE(bundle, iterableIteratorMid);
    REQUIRE(bundle, iteratorHasNextMid);
    REQUIRE(bundle, iteratorNextMid);
#undef REQUIRE
    return ok;
}
} // namespace proxy_server
