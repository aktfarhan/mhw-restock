#include <windows.h>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <cstdlib>
#include <cctype>
#include <string>

const uintptr_t JOB_RUNNER = 0x1AD9D20; // Runs every frame on the main thread
const uintptr_t APPLY_LOADOUT = 0x1D38110; // Call ApplyLoadout(obj, loadoutPos, flag)
const uintptr_t LOADOUT_OBJ_BASE = 0x051C4640; // Start of the ApplyLoadout object's pointer

// Settings from mhw_restock.ini
char iniPath[MAX_PATH] = {}; // Full path to mhw_restock.ini
char keyName[32] = "P"; // The key's name as written in the file
int restockKey = 'P'; // The key's Windows code
std::atomic<int> loadoutSlot{6}; // Which loadout to apply

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

// Returns the object ApplyLoadout needs, or 0 if the chain isn't ready
uintptr_t ReadLoadoutObj() {
    uintptr_t base = (uintptr_t) GetModuleHandle(nullptr);
    uintptr_t addr = *(uintptr_t*) (base + LOADOUT_OBJ_BASE);
    const uintptr_t offsets[] = { 0x150, 0x10, 0x140, 0x170, 0x2C0, 0x438 };

    // Follow each step of the path
    for (uintptr_t offset : offsets) {
        if (addr == 0) return 0;
        
        // Add the offset, read the next pointer
        addr = *(uintptr_t*) (addr + offset);
    }
    return addr;
}

// Astera, Astera Gathering Hub, Research Base, Seliana, Seliana Gathering Hub
bool IsBase(int zone) {
    return zone == 301 || zone == 302 || zone == 303 || zone == 305 || zone == 306;
}

// Returns true if tabbed into the game
bool IsGameFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// The shape of the two game functions
using ApplyLoadoutFn = void (*)(void* obj, int loadoutPos, int flag);
using JobRunnerFn = uintptr_t (*)(void* a, void* b, void* c, void* d);

JobRunnerFn originalJobRunner = nullptr;
std::atomic<bool> restockRequested{false};
std::atomic<int> restockResult{0};
std::atomic<int> restockZone{0};

// The replacement for the game's job runner, called on every frame
uintptr_t HookedJobRunner(void* a, void* b, void* c, void* d) {
    // Let the game do its normal jobs first
    uintptr_t result = originalJobRunner(a, b, c, d);

    // Check if P was pressed
    if (restockRequested.exchange(false)) {
        int zone = ReadZoneId();
        uintptr_t obj = ReadLoadoutObj();
        restockZone = zone;

        // Do the proper action
        if (!IsBase(zone)) {
            restockResult = -1;
        } else if (obj == 0) {
            restockResult = -2;
        } else {
            uintptr_t base = (uintptr_t) GetModuleHandle(nullptr);
            ApplyLoadoutFn applyLoadout = (ApplyLoadoutFn) (base + APPLY_LOADOUT);
            applyLoadout((void*) obj, loadoutSlot, 0);
            restockResult = 1;
        }
    }
    return result;
}

// Writes the 14-byte jump to 'dest' at 'at'
void WriteAbsJump(uint8_t* at, uintptr_t dest) {
    at[0] = 0xFF;
    at[1] = 0x25;
    *(uint32_t*) (at + 2) = 0;
    
    // The 8-byte address to jump to
    *(uintptr_t*) (at + 6) = dest;
}

// Hooks the job runner
bool InstallHook() {
    uint8_t* target = (uint8_t*) ((uintptr_t) GetModuleHandle(nullptr) + JOB_RUNNER);
    
    // The 16-byte swap needs a 16-byte aligned address
    if ((uintptr_t) target % 16 != 0) return false;

    // The 14 bytes that are replaced
    const uint8_t expected[14] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0x6C, 0x24, 0x50, 0x48, 0x8B, 0xD9 };
    if (memcmp(target, expected, sizeof(expected)) != 0) return false;

    // Save a copy of the game's first 14 bytes
    uint8_t* tramp = (uint8_t*) VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return false;
    memcpy(tramp, target, 14);
    WriteAbsJump(tramp + 14, (uintptr_t) (target + 14));
    originalJobRunner = (JobRunnerFn) tramp;

    // Build the new first 16 bytes
    alignas(16) uint8_t patch[16];
    WriteAbsJump(patch, (uintptr_t) &HookedJobRunner);
    patch[14] = target[14];
    patch[15] = target[15];

    // Change game code from read-only to write mode
    DWORD oldProtect;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;

    // Swap in the new bytes all at once
    unsigned __int128 oldBytes, newBytes;
    memcpy(&oldBytes, target, 16);
    memcpy(&newBytes, patch, 16);
    bool swapped = __sync_bool_compare_and_swap((unsigned __int128*) target, oldBytes, newBytes);

    // Make it read-only again, and tell the CPU the code changed
    VirtualProtect(target, 16, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    return swapped;
}

// Turns a key name into Windows key code, or 0 if unknown
int KeyFromName(const char* name) {
    size_t len = strlen(name);

    // A single letter or digit
    if (len == 1 && isalnum((unsigned char) name[0])) return toupper((unsigned char) name[0]);

    // F1 - F12
    if (len >= 2 && len <= 3 && toupper((unsigned char) name[0]) == 'F') {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 12) return VK_F1 + (n - 1);
    }

    // NUMPAD0 - NUMPAD9
    if (len == 7 && _strnicmp(name, "NUMPAD", 6) == 0 && isdigit((unsigned char) name[6])) {
        return VK_NUMPAD0 + (name[6] - '0');
    }

    return 0;
}

// Returns when the ini file was last saved, or 0 if it doesn't exist
ULONGLONG IniLastWrite() {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(iniPath, GetFileExInfoStandard, &info)) return 0;
    return ((ULONGLONG) info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
}

// Reads the ini file into the settings
void LoadSettings(std::ofstream& log) {
    // If the file doesn't exist, create it with the defaults
    if (IniLastWrite() == 0) {
        std::string loadoutText = std::to_string(loadoutSlot + 1);
        WritePrivateProfileStringA("Settings", "Key", keyName, iniPath);
        WritePrivateProfileStringA("Settings", "Loadout", loadoutText.c_str(), iniPath);
        log << "created" << iniPath << std::endl;
    }

    // Read both values
    char newKeyName[32];
    GetPrivateProfileStringA("Settings", "Key", keyName, newKeyName, sizeof(newKeyName), iniPath);
    int newLoadout = GetPrivateProfileIntA("Settings", "Loadout", loadoutSlot + 1, iniPath);

    // Only accept valid keys
    int newKey = KeyFromName(newKeyName);
    if (newKey == 0) {
        log << "settings: unknown key '" << newKeyName << "', keeping " << keyName << std::endl;
    } else if (newKey != restockKey) {
        restockKey = newKey;
        strcpy(keyName, newKeyName);
        log << "settings: Key = " << keyName << std::endl;
    }

    // Loadout: the game has 80 slots
    if (newLoadout < 1 || newLoadout > 80) {
        log << "settings: Loadout must be 1-80, keeping " << loadoutSlot + 1 << std::endl;
    } else if (newLoadout - 1 != loadoutSlot) {
        loadoutSlot = newLoadout - 1;
        log << "settings: Loadout = " << newLoadout << std::endl;
    }
}


// Runs on its own thread as long as the game is open.
DWORD WINAPI RestockThread(_In_ LPVOID LpParameter) {
    // Append: add lines, don't overwrite
    std::ofstream outFile("HelloWorld.txt", std::ios::app);

    if (!outFile.is_open()) {
        std::cerr << "Error opening file." << std::endl;
        return 1;
    }

    // Get the ini file
    GetModuleFileNameA((HMODULE) LpParameter, iniPath, MAX_PATH);
    char* dot = strrchr(iniPath, '.');
    if (dot) strcpy(dot, ".ini");

    // Load the settings once at startup
    LoadSettings(outFile);
    ULONGLONG lastWrite = IniLastWrite();
    outFile << "settings: Key = " << keyName << ", Loadout = " << loadoutSlot + 1 << std::endl;

    // Install the hook, and try for 60s if it fails
    bool hooked = false;
    for (int i = 0; i < 60 && !hooked; i++) {
        hooked = InstallHook();
        if (!hooked) Sleep(1000);
    }

    // Log if it worked, and stop if it didn't
    outFile << (hooked ? "hook installed" : "hook FAILED, restock disabled") << std::endl;
    if (!hooked) return 1;

    // Remembers the key state from the previous loop
    bool wasDown = false;

    // Loop to check the ini file once a second
    int loops = 0;

    while (true) {
        bool downNow = (GetAsyncKeyState(restockKey) & 0x8000) != 0;
        
        // Only run when the key is pressed
        if (downNow && !wasDown && IsGameFocused()) {
            restockRequested = true;
            outFile << keyName << " pressed" << std::endl;
        }

        wasDown = downNow;

        // Log what the hook did
        int result = restockResult.exchange(0);
        if (result == 1) outFile << "restocked (zone " << restockZone << ")" << std::endl;
        if (result == -1) outFile << "skipped: not in a base (zone " << restockZone << ")" << std::endl;
        if (result == -2) outFile << "skipped: loadout object not ready" << std::endl;

        // Reload the settings if the ini file was saved
        if (++loops >= 100) {
            loops = 0;
            ULONGLONG write = IniLastWrite();
            if (write != lastWrite) {
                lastWrite = write;
                LoadSettings(outFile);
                lastWrite = IniLastWrite();
            }
        }

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
        HANDLE hThread = CreateThread(nullptr, 0, RestockThread, hinstDLL, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}