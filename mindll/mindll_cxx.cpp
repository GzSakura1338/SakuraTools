

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/include/ReflectiveDLLInjection.h"
extern "C" {
#include "../native/ReflectiveLoader.c"
}

#include <string>
#include <vector>
static std::string* g_dummy_string = nullptr;
static std::vector<int>* g_dummy_vec = nullptr;

static void writeLine(const char* line) {
    HANDLE h = CreateFileA("C:\\MinecraftProxy.log",
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w, n = 0;
    while (line[n]) ++n;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, line, n, &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        writeLine("mindll_cxx: DllMain reached");
    }
    return TRUE;
}
