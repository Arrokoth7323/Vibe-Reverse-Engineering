// Minimal d3d9 proxy that loads renderdoc.dll for DX9 capture.
//
// Key insight: renderdoc.dll must be loaded AFTER the real d3d9.dll
// and OUTSIDE DllMain (no loader lock). We load it lazily on the
// first Direct3DCreate9 call so RenderDoc can find and inline-hook
// the real d3d9.dll's exports.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ---------------------------------------------------------------------------
// Real d3d9.dll function pointers
// ---------------------------------------------------------------------------

static HMODULE g_real_d3d9;
static HMODULE g_renderdoc;
static bool    g_rdoc_inited;

typedef void* (WINAPI *PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT, void**);
typedef void* (WINAPI *PFN_Direct3DShaderValidatorCreate9)(void);
typedef int   (WINAPI *PFN_D3DPERF_BeginEvent)(DWORD, LPCWSTR);
typedef int   (WINAPI *PFN_D3DPERF_EndEvent)(void);
typedef void  (WINAPI *PFN_D3DPERF_SetMarker)(DWORD, LPCWSTR);
typedef void  (WINAPI *PFN_D3DPERF_SetRegion)(DWORD, LPCWSTR);
typedef BOOL  (WINAPI *PFN_D3DPERF_QueryRepeatFrame)(void);
typedef void  (WINAPI *PFN_D3DPERF_SetOptions)(DWORD);
typedef DWORD (WINAPI *PFN_D3DPERF_GetStatus)(void);
typedef void  (WINAPI *PFN_DebugSetLevel)(void);
typedef void  (WINAPI *PFN_DebugSetMute)(void);

static PFN_Direct3DCreate9               real_Direct3DCreate9;
static PFN_Direct3DCreate9Ex             real_Direct3DCreate9Ex;
static PFN_Direct3DShaderValidatorCreate9 real_Direct3DShaderValidatorCreate9;
static PFN_D3DPERF_BeginEvent            real_D3DPERF_BeginEvent;
static PFN_D3DPERF_EndEvent              real_D3DPERF_EndEvent;
static PFN_D3DPERF_SetMarker             real_D3DPERF_SetMarker;
static PFN_D3DPERF_SetRegion             real_D3DPERF_SetRegion;
static PFN_D3DPERF_QueryRepeatFrame      real_D3DPERF_QueryRepeatFrame;
static PFN_D3DPERF_SetOptions            real_D3DPERF_SetOptions;
static PFN_D3DPERF_GetStatus             real_D3DPERF_GetStatus;
static PFN_DebugSetLevel                 real_DebugSetLevel;
static PFN_DebugSetMute                  real_DebugSetMute;

// ---------------------------------------------------------------------------
// Load real d3d9 (called from DllMain — safe, no renderdoc yet)
// ---------------------------------------------------------------------------

static bool load_real_d3d9()
{
    char sys_path[MAX_PATH];
    GetSystemDirectoryA(sys_path, MAX_PATH);
    strcat_s(sys_path, "\\d3d9.dll");
    g_real_d3d9 = LoadLibraryA(sys_path);
    if (!g_real_d3d9)
        return false;

    real_Direct3DCreate9               = (PFN_Direct3DCreate9)              GetProcAddress(g_real_d3d9, "Direct3DCreate9");
    real_Direct3DCreate9Ex             = (PFN_Direct3DCreate9Ex)            GetProcAddress(g_real_d3d9, "Direct3DCreate9Ex");
    real_Direct3DShaderValidatorCreate9 = (PFN_Direct3DShaderValidatorCreate9)GetProcAddress(g_real_d3d9, "Direct3DShaderValidatorCreate9");
    real_D3DPERF_BeginEvent            = (PFN_D3DPERF_BeginEvent)           GetProcAddress(g_real_d3d9, "D3DPERF_BeginEvent");
    real_D3DPERF_EndEvent              = (PFN_D3DPERF_EndEvent)             GetProcAddress(g_real_d3d9, "D3DPERF_EndEvent");
    real_D3DPERF_SetMarker             = (PFN_D3DPERF_SetMarker)            GetProcAddress(g_real_d3d9, "D3DPERF_SetMarker");
    real_D3DPERF_SetRegion             = (PFN_D3DPERF_SetRegion)            GetProcAddress(g_real_d3d9, "D3DPERF_SetRegion");
    real_D3DPERF_QueryRepeatFrame      = (PFN_D3DPERF_QueryRepeatFrame)     GetProcAddress(g_real_d3d9, "D3DPERF_QueryRepeatFrame");
    real_D3DPERF_SetOptions            = (PFN_D3DPERF_SetOptions)           GetProcAddress(g_real_d3d9, "D3DPERF_SetOptions");
    real_D3DPERF_GetStatus             = (PFN_D3DPERF_GetStatus)            GetProcAddress(g_real_d3d9, "D3DPERF_GetStatus");
    real_DebugSetLevel                 = (PFN_DebugSetLevel)                GetProcAddress(g_real_d3d9, "DebugSetLevel");
    real_DebugSetMute                  = (PFN_DebugSetMute)                 GetProcAddress(g_real_d3d9, "DebugSetMute");

    return real_Direct3DCreate9 != nullptr;
}

// ---------------------------------------------------------------------------
// Load renderdoc lazily (called from Direct3DCreate9 — outside loader lock)
// Renderdoc will detect the already-loaded real d3d9.dll and hook it.
// After this, we re-resolve Direct3DCreate9/Ex from the real d3d9.dll
// so we call through renderdoc's inline hook.
// ---------------------------------------------------------------------------

static void ensure_renderdoc()
{
    if (g_rdoc_inited)
        return;
    g_rdoc_inited = true;

    g_renderdoc = LoadLibraryA("renderdoc.dll");
    if (!g_renderdoc)
        return;

    typedef int (__cdecl *PFN_RENDERDOC_GetAPI)(int version, void** out);
    auto getAPI = (PFN_RENDERDOC_GetAPI)GetProcAddress(g_renderdoc, "RENDERDOC_GetAPI");
    if (getAPI) {
        void* api = nullptr;
        getAPI(10000, &api); // eRENDERDOC_API_Version_1_0_0
    }

    // Re-resolve after renderdoc may have patched the real d3d9.dll
    real_Direct3DCreate9   = (PFN_Direct3DCreate9)  GetProcAddress(g_real_d3d9, "Direct3DCreate9");
    real_Direct3DCreate9Ex = (PFN_Direct3DCreate9Ex)GetProcAddress(g_real_d3d9, "Direct3DCreate9Ex");
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        if (!load_real_d3d9())
            return FALSE;
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_real_d3d9) { FreeLibrary(g_real_d3d9); g_real_d3d9 = nullptr; }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Forwarding exports
// ---------------------------------------------------------------------------

extern "C" {

void* WINAPI Direct3DCreate9(UINT SDKVersion)
{
    ensure_renderdoc();
    return real_Direct3DCreate9 ? real_Direct3DCreate9(SDKVersion) : nullptr;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, void** ppD3D)
{
    ensure_renderdoc();
    return real_Direct3DCreate9Ex ? real_Direct3DCreate9Ex(SDKVersion, ppD3D) : E_FAIL;
}

void* WINAPI Direct3DShaderValidatorCreate9()
{
    return real_Direct3DShaderValidatorCreate9 ? real_Direct3DShaderValidatorCreate9() : nullptr;
}

int WINAPI D3DPERF_BeginEvent(DWORD col, LPCWSTR wszName)
{
    return real_D3DPERF_BeginEvent ? real_D3DPERF_BeginEvent(col, wszName) : 0;
}

int WINAPI D3DPERF_EndEvent()
{
    return real_D3DPERF_EndEvent ? real_D3DPERF_EndEvent() : 0;
}

void WINAPI D3DPERF_SetMarker(DWORD col, LPCWSTR wszName)
{
    if (real_D3DPERF_SetMarker) real_D3DPERF_SetMarker(col, wszName);
}

void WINAPI D3DPERF_SetRegion(DWORD col, LPCWSTR wszName)
{
    if (real_D3DPERF_SetRegion) real_D3DPERF_SetRegion(col, wszName);
}

BOOL WINAPI D3DPERF_QueryRepeatFrame()
{
    return real_D3DPERF_QueryRepeatFrame ? real_D3DPERF_QueryRepeatFrame() : FALSE;
}

void WINAPI D3DPERF_SetOptions(DWORD dwOptions)
{
    if (real_D3DPERF_SetOptions) real_D3DPERF_SetOptions(dwOptions);
}

DWORD WINAPI D3DPERF_GetStatus()
{
    return real_D3DPERF_GetStatus ? real_D3DPERF_GetStatus() : 0;
}

void WINAPI DebugSetLevel()
{
    if (real_DebugSetLevel) real_DebugSetLevel();
}

void WINAPI DebugSetMute()
{
    if (real_DebugSetMute) real_DebugSetMute();
}

} // extern "C"
