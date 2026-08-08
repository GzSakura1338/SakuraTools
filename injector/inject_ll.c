

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <pid> <absolute dll path>\n", argv[0]);
        return 2;
    }
    DWORD pid = (DWORD)strtoul(argv[1], NULL, 10);
    const char* dllPath = argv[2];

    if (strlen(dllPath) < 3 || dllPath[1] != ':') {
        fprintf(stderr, "path must be absolute (e.g. C:\\dir\\file.dll)\n");
        return 2;
    }
    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "dll not found on local fs: %s\n", dllPath);
        return 2;
    }

    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProc) {
        fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }

    SIZE_T len = strlen(dllPath) + 1;
    LPVOID remote = VirtualAllocEx(hProc, NULL, len, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);
    if (!remote) {
        fprintf(stderr, "VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(hProc); return 1;
    }
    SIZE_T wrote = 0;
    if (!WriteProcessMemory(hProc, remote, dllPath, len, &wrote) || wrote != len) {
        fprintf(stderr, "WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc); return 1;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    FARPROC llA = GetProcAddress(k32, "LoadLibraryA");
    if (!llA) {
        fprintf(stderr, "GetProcAddress(LoadLibraryA) failed\n");
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc); return 1;
    }

    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0,
                                        (LPTHREAD_START_ROUTINE)llA,
                                        remote, 0, NULL);
    if (!hThread) {
        fprintf(stderr, "CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc); return 1;
    }

    WaitForSingleObject(hThread, 15000);
    DWORD hModuleLo = 0;
    GetExitCodeThread(hThread, &hModuleLo);

    if (hModuleLo) {
        printf("[+] LoadLibraryA in target returned nonzero (low32=0x%lx)\n",
               (unsigned long)hModuleLo);
    } else {
        printf("[-] LoadLibraryA in target returned NULL — check target log for LDR errors\n");
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return hModuleLo ? 0 : 1;
}
