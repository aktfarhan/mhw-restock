#include <windows.h>
#include <iostream>
#include <fstream>

// Runs on its own thread as long as the game is open.
DWORD WINAPI RestockThread(_In_ LPVOID LpParameter) {
    // Append: add lines, don't overwrite
    std::ofstream outFile("HelloWorld.txt", std::ios::app);

    if (!outFile.is_open()) {
        std::cerr << "Error opening file." << std::endl;
        return 1;
    }

    // Remembers the key state from the previous loop
    bool wasDown = false;

    while (true) {
        bool downNow = (GetAsyncKeyState('P') & 0x8000) != 0;
        
        // Only run when the key is pressed
        if (downNow && !wasDown) {
            outFile << "pressed" << std::endl;
        }

        wasDown = downNow;
        Sleep(10);

    }

    return 0;
}

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpvReserved ) // reserved
{
    // Perform actions based on the reason for calling.
    if (fdwReason == DLL_PROCESS_ATTACH) {
        HANDLE hThread = CreateThread(nullptr, 0, RestockThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}