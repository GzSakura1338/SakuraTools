
#include <windows.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <jni.h>

extern "C" {
#include "../injector/LoadLibraryR.h"
}

static const wchar_t kBujiIsland[] = { 0x5e03, 0x5409, 0x5c9b, 0 };

static BOOL is_java_process(DWORD pid) {
    BOOL found = FALSE;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32 entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32First(snap, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                found = _stricmp(entry.szExeFile, "java.exe")  == 0
                     || _stricmp(entry.szExeFile, "javaw.exe") == 0;
                break;
            }
        } while (Process32Next(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

static ULONGLONG process_creation_time(DWORD pid) {
    ULONGLONG value = 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return 0;
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(p, &created, &exited, &kernel, &user)) {
        ULARGE_INTEGER t;
        t.LowPart  = created.dwLowDateTime;
        t.HighPart = created.dwHighDateTime;
        value = t.QuadPart;
    }
    CloseHandle(p);
    return value;
}

struct FindCtx { DWORD pid; ULONGLONG created; };

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lparam) {
    FindCtx* ctx = (FindCtx*)lparam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[512];
    if (GetWindowTextW(hwnd, title, (int)(sizeof(title) / sizeof(title[0]))) <= 0) return TRUE;
    if (wcsstr(title, kBujiIsland) == NULL) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !is_java_process(pid)) return TRUE;

    ULONGLONG t = process_creation_time(pid);
    if (!ctx->pid || t > ctx->created) {
        ctx->pid     = pid;
        ctx->created = t;
    }
    return TRUE;
}

static DWORD find_buji_island_pid(void) {
    FindCtx ctx = { 0, 0 };
    EnumWindows(enum_windows_cb, (LPARAM)&ctx);
    return ctx.pid;
}

static void enable_debug_privilege(void) {
    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
    TOKEN_PRIVILEGES p; ZeroMemory(&p, sizeof(p));
    p.PrivilegeCount = 1;
    p.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueA(NULL, "SeDebugPrivilege", &p.Privileges[0].Luid))
        AdjustTokenPrivileges(tok, FALSE, &p, 0, NULL, NULL);
    CloseHandle(tok);
}

static volatile DWORD sLastInjectedPid = 0;

static BOOL do_inject(DWORD pid, const char* dllPath, char* errOut, int errCap) {
    HANDLE hFile = CreateFileA(dllPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "CreateFile failed (err=%lu)", GetLastError());
        return FALSE;
    }
    DWORD sz = GetFileSize(hFile, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "GetFileSize failed (err=%lu)", GetLastError());
        CloseHandle(hFile);
        return FALSE;
    }
    LPVOID buf = HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "HeapAlloc %lu bytes failed", sz);
        CloseHandle(hFile);
        return FALSE;
    }
    DWORD read = 0;
    if (!ReadFile(hFile, buf, sz, &read, NULL) || read != sz) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "ReadFile failed (err=%lu)", GetLastError());
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(hFile);
        return FALSE;
    }
    CloseHandle(hFile);

    enable_debug_privilege();

    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION  | PROCESS_VM_WRITE           |
                               PROCESS_VM_READ,
                               FALSE, pid);
    if (!hProc) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "OpenProcess failed (err=%lu)", GetLastError());
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }

    HANDLE hRemoteThread = LoadRemoteLibraryR(hProc, buf, sz, NULL);
    HeapFree(GetProcessHeap(), 0, buf);
    if (!hRemoteThread) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "LoadRemoteLibraryR failed (err=%lu)", GetLastError());
        CloseHandle(hProc);
        return FALSE;
    }

    WaitForSingleObject(hRemoteThread, 15000);
    DWORD exit = 0;
    GetExitCodeThread(hRemoteThread, &exit);
    CloseHandle(hRemoteThread);
    CloseHandle(hProc);

    if (exit == 0) {
        _snprintf_s(errOut, errCap, _TRUNCATE, "ReflectiveLoader returned NULL");
        return FALSE;
    }
    return TRUE;
}

extern "C" {

JNIEXPORT jstring JNICALL
Java_today_starain_utils_injection_NativeInjector_injectProxyDll(JNIEnv* env,
                                                                  jclass klass,
                                                                  jstring dllPath,
                                                                  jint timeoutMs) {
    (void)klass;
    if (!dllPath) return env->NewStringUTF("dllPath is null");
    const char* utf = env->GetStringUTFChars(dllPath, NULL);
    if (!utf) return env->NewStringUTF("GetStringUTFChars returned NULL");

    DWORD pid = 0;
    int elapsed = 0;
    bool infinite = (timeoutMs <= 0);
    for (;;) {
        pid = find_buji_island_pid();
        if (pid) break;
        if (!infinite && elapsed >= timeoutMs) break;
        Sleep(100);
        elapsed += 100;
    }

    if (!pid) {
        env->ReleaseStringUTFChars(dllPath, utf);
        return env->NewStringUTF("NO_TARGET_WINDOW");
    }

    char err[512] = {0};
    BOOL ok = do_inject(pid, utf, err, (int)sizeof(err));
    env->ReleaseStringUTFChars(dllPath, utf);

    if (ok) {
        sLastInjectedPid = pid;
        return NULL;
    }
    return env->NewStringUTF(err[0] ? err : "unknown injection error");
}

JNIEXPORT jlong JNICALL
Java_today_starain_utils_injection_NativeInjector_getLastInjectedPid(JNIEnv* env, jclass klass) {
    (void)env; (void)klass;
    return (jlong) sLastInjectedPid;
}

}
