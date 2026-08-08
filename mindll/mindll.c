

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../native/include/ReflectiveDLLInjection.h"

#include "../native/ReflectiveLoader.c"

static void writeLine(const char* line) {
    HANDLE h = CreateFileA("C:\\MinecraftProxy.log",
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {

        char tmp[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, tmp);
        if (n > 0 && n < MAX_PATH - 24) {
            lstrcatA(tmp, "MinecraftProxy.log");
            h = CreateFileA(tmp, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
    }
    if (h != INVALID_HANDLE_VALUE) {
        char buf[256];
        int len = wsprintfA(buf, "[%lu] mindll: %s\r\n",
                            GetTickCount(), line);
        DWORD w;
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, buf, (DWORD)len, &w, NULL);
        FlushFileBuffers(h);
        CloseHandle(h);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)hModule; (void)lpReserved;
    if (reason == DLL_PROCESS_ATTACH) {
        writeLine("DllMain PROCESS_ATTACH reached");
    }
    return TRUE;
}
