#include <windows.h>
#include <iostream>
#include <fstream>

// Returns the zone ID (e.g. 306 = Seliana Gathering Hub)
int ReadZoneId() {
    // Where MHW starts in memory
    uintptr_t base = (uintptr_t) GetModuleHandle(nullptr);

    // Read the fixed pointer inside the exe
    uintptr_t addr = *(uintptr_t*) (base + 0x051C4368);

    // The middle steps of the path
    const uintptr_t offsets[] = { 0x80, 0x50, 0xD0, 0x8, 0x508 };

    // Part of the chain doesn't exist yet
    for (uintptr_t off : offsets) {
        if (addr == 0) return -1;

        // Add the offset, read the next pointer
        addr = *(uintptr_t*) (addr + off);
    }

    if (addr == 0) return -1;

    // Read the zone ID and return it
    return *(int*) (addr + 0xB88);
}


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
            outFile << "zone = " << ReadZoneId() << std::endl;
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