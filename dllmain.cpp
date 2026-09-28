#include <windows.h>

DWORD WINAPI RestockThread(_In_ LPVOID LpParameter) {
    return 0;
}

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpvReserved )  // reserved
{
    // Perform actions based on the reason for calling.
    if (fdwReason == DLL_PROCESS_ATTACH) {
        HANDLE hThread = CreateThread(nullptr, 0, RestockThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}