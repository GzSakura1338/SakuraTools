#pragma once

#include "include/jni.h"
#include <string>

std::u16string FormatServerMotd(const std::u16string& serverName,
                               const std::u16string& serverAddress);

// Returns a local packet reference, or nullptr with a diagnostic and no pending exception.
jobject BuildStatusResponse(JNIEnv* env, jobject mcLoader, const std::u16string& motd);
