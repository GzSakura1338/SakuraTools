

#pragma once

#include "proxy.h"

bool InstallBServer(JNIEnv* env);

void BServer_SetTargetConnection(JNIEnv* env, jobject connection);

bool BServer_IsBActive();

bool BServer_IsLoginIntention(JNIEnv* env, jobject packet);

bool BServer_WaitForBConnected(int timeoutMs);

bool BServer_BlockAMainThreadUntilBConnected(JNIEnv* env);

void BServer_ForwardToB(JNIEnv* env, jobject packet);

bool BServer_TryCaptureLiveConnection(JNIEnv* env);
