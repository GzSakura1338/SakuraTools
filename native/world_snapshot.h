#pragma once

#include "proxy.h"
#include <vector>

using SnapshotBoundary = bool (*)(JNIEnv*);

bool InstallWorldSnapshot(JNIEnv* env, jobject loader);
// Returned packets are global references, owned by the caller.
bool CaptureWorldSnapshot(JNIEnv* env, jobject minecraft, jobject connection,
                          SnapshotBoundary boundary, std::vector<jobject>& packets);
bool WriteSnapshotPacket(JNIEnv* env, jobject channel, jobject packet);
