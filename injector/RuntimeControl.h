#pragma once
#include <windows.h>
#include <stdio.h>

#define SAKURA_RESUME_PREFIX "Local\\SakuraTools.Resume.v1."

/* 1: resident signalled, 0: no resident, -1: access/signalling failure. */
static int TryResumeProxy(DWORD pid) {
    char name[96];
    snprintf(name, sizeof(name), SAKURA_RESUME_PREFIX "%lu", pid);
    HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (!event) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    BOOL ok = SetEvent(event);
    CloseHandle(event);
    return ok ? 1 : -1;
}
