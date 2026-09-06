#pragma once

#include "proxy.h"
#include <mutex>

// Controller thread: startup, cancellation, and retryable cleanup.
bool InstallBServer(JNIEnv* env);
void BServer_RequestStop();
bool StopBServer(JNIEnv* env);
void BServer_CheckLoginTimeout(JNIEnv* env);

// Connection hook: select A's remote connection, or discover an existing one.
void BServer_SetTargetConnection(JNIEnv* env, jobject connection);
enum class LiveConnectionState { NotConnected, Captured, Failed };
LiveConnectionState BServer_TryCaptureLiveConnection(JNIEnv* env);

// Relay: serialize snapshot boundaries with downstream packet dispatch.
bool BServer_IsBActive();
std::recursive_mutex& BServer_DispatchMutex();
void BServer_ForwardToB(JNIEnv* env, jobject packet);

// Pre-connection injection: wait for B before allowing A to join the server.
bool BServer_IsLoginIntention(JNIEnv* env, jobject packet);
bool BServer_WaitForBConnected(int timeoutMs);
bool BServer_BlockAMainThreadUntilBConnected(JNIEnv* env);
