#pragma once

// Private server interfaces. Other modules include only b_server.h.
#include "b_server.h"
#include "jni_lookup.h"
#include "state.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace proxy_server {

void setProtocolState(JNIEnv* env, jobject channel, jobject protoValue);
bool defineInitClass(JNIEnv* env, jobject mcLoader);
bool defineHandlerClass(JNIEnv* env, jobject mcLoader);
bool defineMainGateClass(JNIEnv* env, jobject mcLoader);
bool startLanAnnouncement(JNIEnv* env, jobject mcLoader);
bool bindServer(JNIEnv* env, jobject mcLoader);
std::u16string currentServerMotd(JNIEnv* env);
bool refreshMidSessionRefs(JNIEnv* env, jobject mcLoader);
bool cacheJavaRefs(JNIEnv* env, jobject mcLoader);
void cachePacketBindings(JNIEnv* env, jobject loader);
bool validateRequiredBindings();
bool cachePacketWriter(JNIEnv* env, jobject loader);
bool writePacket(JNIEnv* env, jobject channel, jobject packet, bool flush = true);
void closeBChannel(JNIEnv* env, jobject channel);
bool sendGamePacket(JNIEnv* env, jobject channel, jobject packet, bool flush = true);
bool buildPlayerInfoMirror(JNIEnv* env, jobject packet, jobject& output);
void clearPendingPackets(JNIEnv* env);
void closeARemoteConnection(JNIEnv* env);
bool uuidToBytes(JNIEnv* env, jobject uuid, unsigned char out[16]);
bool initializeBIdentity(JNIEnv* env, jobject uuid, jstring name);
void completeLogin(JNIEnv* env, jobject hello);
bool prepareTeamPacket(JNIEnv* env, jobject packet, jobject& output);
void handleClientPacket(JNIEnv* env, jobject ctx, jobject msg);

} // namespace proxy_server
