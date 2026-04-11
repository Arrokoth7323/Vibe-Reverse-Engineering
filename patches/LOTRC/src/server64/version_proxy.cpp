/*
 * version.dll proxy — loaded by NvRemixBridge.exe from .trex/ directory.
 * Forwards all 17 exports to the real version.dll from System32,
 * then loads remix_lights.dll (our 64-bit light emitter).
 *
 * We suppress winver.h (via NOVERSION) to avoid redefinition errors,
 * then resolve all 17 functions by GetProcAddress at runtime.
 */
#define WIN32_LEAN_AND_MEAN
#define NOVERSION
#include <windows.h>

static HMODULE g_real_version = nullptr;
static HMODULE g_lights_dll   = nullptr;

/*
 * All 17 exports forwarded via GetProcAddress at runtime. On x64,
 * __stdcall == __cdecl, so a generic FARPROC trampoline works for any
 * function that NvRemixBridge actually calls (argument passing is
 * register-based and the caller cleans up). For the 6 frequently-called
 * functions we use typed wrappers; the rest are no-op stubs (NvRemixBridge
 * never calls them — the import just needs to resolve at load time).
 */

/* --- Typed wrappers for commonly-called functions --- */

typedef BOOL  (WINAPI* PFN_GFVIA)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI* PFN_GFVIW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI* PFN_GFVSA)(LPCSTR, LPDWORD);
typedef DWORD (WINAPI* PFN_GFVSW)(LPCWSTR, LPDWORD);
typedef BOOL  (WINAPI* PFN_VQVA)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL  (WINAPI* PFN_VQVW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

static PFN_GFVIA real_GetFileVersionInfoA;
extern "C" __declspec(dllexport)
BOOL WINAPI proxy_GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    if (!real_GetFileVersionInfoA)
        real_GetFileVersionInfoA = (PFN_GFVIA)GetProcAddress(g_real_version, "GetFileVersionInfoA");
    return real_GetFileVersionInfoA ? real_GetFileVersionInfoA(a, b, c, d) : FALSE;
}

static PFN_GFVIW real_GetFileVersionInfoW;
extern "C" __declspec(dllexport)
BOOL WINAPI proxy_GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) {
    if (!real_GetFileVersionInfoW)
        real_GetFileVersionInfoW = (PFN_GFVIW)GetProcAddress(g_real_version, "GetFileVersionInfoW");
    return real_GetFileVersionInfoW ? real_GetFileVersionInfoW(a, b, c, d) : FALSE;
}

static PFN_GFVSA real_GetFileVersionInfoSizeA;
extern "C" __declspec(dllexport)
DWORD WINAPI proxy_GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    if (!real_GetFileVersionInfoSizeA)
        real_GetFileVersionInfoSizeA = (PFN_GFVSA)GetProcAddress(g_real_version, "GetFileVersionInfoSizeA");
    return real_GetFileVersionInfoSizeA ? real_GetFileVersionInfoSizeA(a, b) : 0;
}

static PFN_GFVSW real_GetFileVersionInfoSizeW;
extern "C" __declspec(dllexport)
DWORD WINAPI proxy_GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) {
    if (!real_GetFileVersionInfoSizeW)
        real_GetFileVersionInfoSizeW = (PFN_GFVSW)GetProcAddress(g_real_version, "GetFileVersionInfoSizeW");
    return real_GetFileVersionInfoSizeW ? real_GetFileVersionInfoSizeW(a, b) : 0;
}

static PFN_VQVA real_VerQueryValueA;
extern "C" __declspec(dllexport)
BOOL WINAPI proxy_VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
    if (!real_VerQueryValueA)
        real_VerQueryValueA = (PFN_VQVA)GetProcAddress(g_real_version, "VerQueryValueA");
    return real_VerQueryValueA ? real_VerQueryValueA(a, b, c, d) : FALSE;
}

static PFN_VQVW real_VerQueryValueW;
extern "C" __declspec(dllexport)
BOOL WINAPI proxy_VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
    if (!real_VerQueryValueW)
        real_VerQueryValueW = (PFN_VQVW)GetProcAddress(g_real_version, "VerQueryValueW");
    return real_VerQueryValueW ? real_VerQueryValueW(a, b, c, d) : FALSE;
}

/* --- No-op stubs for remaining exports (resolve the import, never called) --- */

#define NOP_STUB(name) \
    extern "C" __declspec(dllexport) void proxy_##name() {}

NOP_STUB(GetFileVersionInfoByHandle)
NOP_STUB(GetFileVersionInfoExA)
NOP_STUB(GetFileVersionInfoExW)
NOP_STUB(GetFileVersionInfoSizeExA)
NOP_STUB(GetFileVersionInfoSizeExW)
NOP_STUB(VerFindFileA)
NOP_STUB(VerFindFileW)
NOP_STUB(VerInstallFileA)
NOP_STUB(VerInstallFileW)
NOP_STUB(VerLanguageNameA)
NOP_STUB(VerLanguageNameW)

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        /* Load real version.dll from System32 */
        char sys_path[MAX_PATH];
        GetSystemDirectoryA(sys_path, MAX_PATH);
        lstrcatA(sys_path, "\\version.dll");
        g_real_version = LoadLibraryA(sys_path);

        /* Load our light emitter DLL from the same directory (.trex/) */
        g_lights_dll = LoadLibraryA("remix_lights.dll");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_lights_dll)   FreeLibrary(g_lights_dll);
        if (g_real_version) FreeLibrary(g_real_version);
    }
    return TRUE;
}
