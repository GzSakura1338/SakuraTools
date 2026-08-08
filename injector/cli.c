

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

BOOL DoInject(DWORD dwProcessId, const char* cpDllFile,
              char* outMessage, int maxLen);

static int usage(const char* argv0) {
    fprintf(stderr, "usage: %s <dll-path>\n", argv0);
    return 2;
}

typedef struct ProcessChoice {
    DWORD pid;
    ULONGLONG created;
} ProcessChoice;

static BOOL is_java_process(DWORD pid) {
    BOOL found = FALSE;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return FALSE;

    PROCESSENTRY32 entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                found = _stricmp(entry.szExeFile, "java.exe") == 0 ||
                        _stricmp(entry.szExeFile, "javaw.exe") == 0;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static ULONGLONG process_creation_time(DWORD pid) {
    ULONGLONG value = 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return 0;

    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        ULARGE_INTEGER time;
        time.LowPart = created.dwLowDateTime;
        time.HighPart = created.dwHighDateTime;
        value = time.QuadPart;
    }
    CloseHandle(process);
    return value;
}

static BOOL title_is_buji_island(const wchar_t* title) {
    static const wchar_t buji_island[] = {0x5e03, 0x5409, 0x5c9b, 0};
    return wcsstr(title, buji_island) != NULL;
}

static BOOL CALLBACK find_buji_island_window(HWND window, LPARAM param) {
    ProcessChoice* choice = (ProcessChoice*)param;
    if (!IsWindowVisible(window)) return TRUE;

    wchar_t title[512];
    if (GetWindowTextW(window, title, (int)(sizeof(title) / sizeof(title[0]))) <= 0)
        return TRUE;
    if (!title_is_buji_island(title)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid || !is_java_process(pid)) return TRUE;

    ULONGLONG created = process_creation_time(pid);
    if (!choice->pid || created > choice->created) {
        choice->pid = pid;
        choice->created = created;
    }
    return TRUE;
}

static DWORD find_buji_island_process(void) {
    ProcessChoice choice;
    ZeroMemory(&choice, sizeof(choice));
    EnumWindows(find_buji_island_window, (LPARAM)&choice);
    return choice.pid;
}

static DWORD wait_for_buji_island_process(void) {
    fprintf(stdout,
            "waiting for a visible Java window containing "
            "U+5E03 U+5409 U+5C9B...\n");
    fflush(stdout);
    for (;;) {
        DWORD pid = find_buji_island_process();
        if (pid) return pid;
        Sleep(50);
    }
}

static unsigned short network_port_to_host(DWORD value) {
    unsigned short port = (unsigned short)value;
    return (unsigned short)((port >> 8) | (port << 8));
}

static BOOL proxy_listener_ready(DWORD pid) {
    DWORD size = 0;
    DWORD rc = GetExtendedTcpTable(NULL, &size, FALSE, AF_INET,
                                   TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0) return FALSE;

    PMIB_TCPTABLE_OWNER_PID table =
        (PMIB_TCPTABLE_OWNER_PID)HeapAlloc(GetProcessHeap(), 0, size);
    if (!table) return FALSE;
    rc = GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                             TCP_TABLE_OWNER_PID_LISTENER, 0);
    BOOL ready = FALSE;
    if (rc == NO_ERROR) {
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            MIB_TCPROW_OWNER_PID* row = &table->table[i];
            if (row->dwOwningPid == pid &&
                network_port_to_host(row->dwLocalPort) == 25565) {
                ready = TRUE;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, table);
    return ready;
}

static BOOL wait_for_proxy_listener(DWORD pid) {
    fprintf(stdout, "waiting for PID %lu to listen on 127.0.0.1:25565...\n", pid);
    fflush(stdout);
    for (int elapsed = 0; elapsed < 30000; elapsed += 50) {
        if (proxy_listener_ready(pid)) return TRUE;
        Sleep(50);
    }
    return FALSE;
}

int main(int argc, char** argv) {
    if (argc != 2) return usage(argv[0]);

    unsigned long pid = 0;
    const char* dll = argv[1];

    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "dll not found: %s\n", dll);
        return 2;
    }

    pid = wait_for_buji_island_process();
    fprintf(stdout, "matched Buji Island Java window, PID %lu\n", pid);

    char msg[1024];
    BOOL ok = DoInject((DWORD)pid, dll, msg, (int)sizeof(msg));
    fprintf(stdout, "%s\n", msg);
    if (!ok) return 1;
    if (!wait_for_proxy_listener((DWORD)pid)) {
        fprintf(stderr, "injection completed, but the proxy listener was not ready after 30 seconds\n");
        return 4;
    }
    fprintf(stdout, "proxy listener ready\n");
    return 0;
}
