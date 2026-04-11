/*
 * remix_lights.dll — 64-bit light emitter for NvRemixBridge.exe.
 *
 * Reads light data from shared memory (written by the 32-bit FFP proxy),
 * calls the Remix SDK directly inside the bridge server process.
 * Bypasses the broken bridge IPC CreateLight path.
 *
 * Hook chain: IAT(GetProcAddress) → wrapped_Direct3DCreate9 →
 * vtable hooks on CreateDevice/CreateDeviceEx (capture device pointer) →
 * vtable hook on EndScene (emit lights on the render thread).
 *
 * CreateLight/DrawLightInstance must run on the render thread — calling
 * from a background thread crashes DXVK-Remix.
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unordered_map>

#include "remix/remix_c.h"
#include "shared_light_data.h"

/* ── Logging ────────────────────────────────────────────────────────── */

static FILE* g_log = nullptr;

static void log_msg(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
    fflush(g_log);
}

/* ── Forward declarations for D3D9 COM interfaces ──────────────────── */

struct IDirect3D9Ex;
struct IDirect3DDevice9Ex;

/* IDirect3D9 vtable indices */
static constexpr int VTABLE_IDX_CREATEDEVICE   = 16;
static constexpr int VTABLE_IDX_CREATEDEVICEEX = 20;

/* IDirect3DDevice9 vtable indices */
static constexpr int VTABLE_IDX_PRESENT   = 17;
static constexpr int VTABLE_IDX_ENDSCENE  = 42;
static constexpr int VTABLE_IDX_PRESENTEX = 121;

/* CreateDevice signature (non-Ex) */
typedef HRESULT (WINAPI* PFN_CreateDevice)(
    IDirect3D9Ex* pThis,
    UINT Adapter, UINT DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags,
    void* pPresentationParameters,
    void** ppReturnedDeviceInterface); /* IDirect3DDevice9** */

typedef HRESULT (WINAPI* PFN_CreateDeviceEx)(
    IDirect3D9Ex* pThis,
    UINT Adapter, UINT DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags,
    void* pPresentationParameters,
    void* pFullscreenDisplayMode,
    IDirect3DDevice9Ex** ppReturnedDeviceInterface);

/* IDirect3DDevice9::Present (slot 17) */
typedef HRESULT (WINAPI* PFN_Present)(
    IDirect3DDevice9Ex* pThis,
    const RECT* pSourceRect, const RECT* pDestRect,
    HWND hDestWindowOverride, const void* pDirtyRegion);

/* IDirect3DDevice9Ex::PresentEx (slot 121) */
typedef HRESULT (WINAPI* PFN_PresentEx)(
    IDirect3DDevice9Ex* pThis,
    const RECT* pSourceRect, const RECT* pDestRect,
    HWND hDestWindowOverride, const void* pDirtyRegion,
    DWORD dwFlags);

/* IDirect3DDevice9::EndScene (slot 42) */
typedef HRESULT (WINAPI* PFN_EndScene)(IDirect3DDevice9Ex* pThis);

typedef HRESULT (WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppD3D);

/* The bridge calls Direct3DCreate9 (non-Ex) which DXVK returns as IDirect3D9Ex */
struct IDirect3D9;
typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);

typedef FARPROC (WINAPI* PFN_GetProcAddress)(HMODULE hModule, LPCSTR lpProcName);

/* ── Tuning constants ──────────────────────────────────────────────── */

static constexpr float SPHERE_RADIUS  = 0.3f;   // physical emitter size (small, unobtrusive)
static constexpr float BRIGHTNESS_K   = 1.0f;   // global brightness multiplier

/* ── State ──────────────────────────────────────────────────────────── */

static remixapi_Interface     g_remix = {};
static bool                   g_api_ready = false;
static volatile bool          g_device_registered = false;
static HANDLE                 g_shmem_handle = nullptr;
static shared_light_data*     g_shm = nullptr;

/* Hash-keyed light tracking — stable across array reorders */
struct remix_light_state {
    remixapi_LightHandle handle;
    float pos[3];
    float col[3];
    float radius;
    float linear_att;
    float quad_att;
};
static std::unordered_map<uint64_t, remix_light_state> g_light_map;

/* Hook state */
static PFN_CreateDevice            g_orig_CreateDevice = nullptr;
static PFN_CreateDeviceEx          g_orig_CreateDeviceEx = nullptr;
static PFN_Present                 g_orig_Present = nullptr;
static PFN_PresentEx               g_orig_PresentEx = nullptr;
static PFN_EndScene                g_orig_EndScene = nullptr;
static PFN_Direct3DCreate9         g_real_Direct3DCreate9 = nullptr;
static volatile IDirect3DDevice9Ex* g_captured_device = nullptr;
static PFN_GetProcAddress          g_orig_GetProcAddress = nullptr;

/* ── Forward declarations ──────────────────────────────────────────── */

static void render_thread_update();
static void install_present_hooks(IDirect3DDevice9Ex* device);

/* ── CreateDevice vtable hook (slot 16) ────────────────────────────── */

static HRESULT WINAPI hooked_CreateDevice(
    IDirect3D9Ex* pThis,
    UINT Adapter, UINT DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags,
    void* pPresentationParameters,
    void** ppReturnedDeviceInterface)
{
    log_msg("CreateDevice intercepted (Adapter=%u)", Adapter);

    HRESULT hr = g_orig_CreateDevice(
        pThis, Adapter, DeviceType, hFocusWindow,
        BehaviorFlags, pPresentationParameters,
        ppReturnedDeviceInterface);

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface)
    {
        g_captured_device = (IDirect3DDevice9Ex*)*ppReturnedDeviceInterface;
        log_msg("CreateDevice captured device: %p", (void*)g_captured_device);
        install_present_hooks((IDirect3DDevice9Ex*)g_captured_device);
    }
    else
    {
        log_msg("CreateDevice FAILED hr=0x%08X", (unsigned)hr);
    }

    return hr;
}

/* ── CreateDeviceEx vtable hook (slot 20) ──────────────────────────── */

static HRESULT WINAPI hooked_CreateDeviceEx(
    IDirect3D9Ex* pThis,
    UINT Adapter, UINT DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags,
    void* pPresentationParameters,
    void* pFullscreenDisplayMode,
    IDirect3DDevice9Ex** ppReturnedDeviceInterface)
{
    log_msg("CreateDeviceEx intercepted (Adapter=%u)", Adapter);

    HRESULT hr = g_orig_CreateDeviceEx(
        pThis, Adapter, DeviceType, hFocusWindow,
        BehaviorFlags, pPresentationParameters,
        pFullscreenDisplayMode, ppReturnedDeviceInterface);

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface)
    {
        g_captured_device = *ppReturnedDeviceInterface;
        log_msg("Device captured: %p", (void*)g_captured_device);
        install_present_hooks(*ppReturnedDeviceInterface);
    }
    else
    {
        log_msg("CreateDeviceEx FAILED hr=0x%08X", (unsigned)hr);
    }

    return hr;
}

/* ── Direct3DCreate9 wrapper (non-Ex — this is what the bridge calls) ─ */

static IDirect3D9* WINAPI wrapped_Direct3DCreate9(UINT SDKVersion)
{
    log_msg("Direct3DCreate9 intercepted (SDK=%u)", SDKVersion);

    IDirect3D9* result = g_real_Direct3DCreate9(SDKVersion);
    log_msg("Direct3DCreate9 returned pD3D=%p", result);

    if (result)
    {
        void** vtable = *(void***)result;

        /* Hook CreateDevice (slot 16) */
        g_orig_CreateDevice = (PFN_CreateDevice)vtable[VTABLE_IDX_CREATEDEVICE];
        log_msg("Hooking CreateDevice at vtable[%d]=%p",
                VTABLE_IDX_CREATEDEVICE, g_orig_CreateDevice);

        DWORD old_protect = 0;
        if (VirtualProtect(&vtable[VTABLE_IDX_CREATEDEVICE], sizeof(void*),
                           PAGE_READWRITE, &old_protect))
        {
            vtable[VTABLE_IDX_CREATEDEVICE] = (void*)hooked_CreateDevice;
            VirtualProtect(&vtable[VTABLE_IDX_CREATEDEVICE], sizeof(void*),
                           old_protect, &old_protect);
            log_msg("CreateDevice vtable hook installed (slot 16)");
        }

        /* Hook CreateDeviceEx (slot 20) */
        g_orig_CreateDeviceEx = (PFN_CreateDeviceEx)vtable[VTABLE_IDX_CREATEDEVICEEX];
        log_msg("Hooking CreateDeviceEx at vtable[%d]=%p",
                VTABLE_IDX_CREATEDEVICEEX, g_orig_CreateDeviceEx);

        if (VirtualProtect(&vtable[VTABLE_IDX_CREATEDEVICEEX], sizeof(void*),
                           PAGE_READWRITE, &old_protect))
        {
            vtable[VTABLE_IDX_CREATEDEVICEEX] = (void*)hooked_CreateDeviceEx;
            VirtualProtect(&vtable[VTABLE_IDX_CREATEDEVICEEX], sizeof(void*),
                           old_protect, &old_protect);
            log_msg("CreateDeviceEx vtable hook installed (slot 20)");
        }
    }

    return result;
}

/* ── GetProcAddress IAT hook ───────────────────────────────────────── */

static FARPROC WINAPI hooked_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    /* Avoid crash on ordinal imports (lpProcName < 0x10000 = ordinal) */
    if ((uintptr_t)lpProcName > 0xFFFF)
    {
        if (strcmp(lpProcName, "Direct3DCreate9") == 0)
        {
            FARPROC real = g_orig_GetProcAddress(hModule, lpProcName);
            if (real)
            {
                g_real_Direct3DCreate9 = (PFN_Direct3DCreate9)real;
                log_msg("GetProcAddress(\"Direct3DCreate9\") intercepted -> %p, returning wrapper",
                        real);
                return (FARPROC)wrapped_Direct3DCreate9;
            }
        }
    }

    return g_orig_GetProcAddress(hModule, lpProcName);
}

/* ── IAT patching helper ───────────────────────────────────────────── */

static bool patch_iat_entry(HMODULE module, const char* target_dll,
                            const char* func_name, void* new_func, void** orig_func)
{
    auto dos = (IMAGE_DOS_HEADER*)module;
    auto nt = (IMAGE_NT_HEADERS*)((uint8_t*)module + dos->e_lfanew);
    auto& import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!import_dir.VirtualAddress) return false;

    auto desc = (IMAGE_IMPORT_DESCRIPTOR*)((uint8_t*)module + import_dir.VirtualAddress);

    for (; desc->Name; desc++)
    {
        auto dll_name = (const char*)((uint8_t*)module + desc->Name);
        if (_stricmp(dll_name, target_dll) != 0) continue;

        auto thunk_orig = (IMAGE_THUNK_DATA*)((uint8_t*)module + desc->OriginalFirstThunk);
        auto thunk_iat  = (IMAGE_THUNK_DATA*)((uint8_t*)module + desc->FirstThunk);

        for (; thunk_orig->u1.AddressOfData; thunk_orig++, thunk_iat++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(thunk_orig->u1.Ordinal)) continue;

            auto import = (IMAGE_IMPORT_BY_NAME*)((uint8_t*)module + thunk_orig->u1.AddressOfData);
            if (strcmp(import->Name, func_name) != 0) continue;

            /* Found the IAT entry */
            if (orig_func)
                *orig_func = (void*)thunk_iat->u1.Function;

            DWORD old_protect = 0;
            VirtualProtect(&thunk_iat->u1.Function, sizeof(void*),
                           PAGE_READWRITE, &old_protect);
            thunk_iat->u1.Function = (uintptr_t)new_func;
            VirtualProtect(&thunk_iat->u1.Function, sizeof(void*),
                           old_protect, &old_protect);
            return true;
        }
    }
    return false;
}

static void install_getprocaddress_hook()
{
    HMODULE bridge_exe = GetModuleHandleA(nullptr); /* NvRemixBridge.exe */
    void* orig = nullptr;

    if (patch_iat_entry(bridge_exe, "kernel32.dll", "GetProcAddress",
                        (void*)hooked_GetProcAddress, &orig))
    {
        g_orig_GetProcAddress = (PFN_GetProcAddress)orig;
        log_msg("GetProcAddress IAT hook installed on bridge exe");
    }
    else
    {
        /* Try api-ms-win-core-libraryloader */
        if (patch_iat_entry(bridge_exe, "api-ms-win-core-libraryloader-l1-2-0.dll",
                            "GetProcAddress", (void*)hooked_GetProcAddress, &orig))
        {
            g_orig_GetProcAddress = (PFN_GetProcAddress)orig;
            log_msg("GetProcAddress IAT hook installed (api-ms-win-core-libraryloader)");
        }
        else
        {
            log_msg("WARN: Could not hook GetProcAddress in bridge IAT");
        }
    }
}

/* ── Seqlock reader ─────────────────────────────────────────────────── */

static bool seqlock_read(const volatile shared_light_data* src, shared_light_data* dst)
{
    for (int attempt = 0; attempt < 64; attempt++)
    {
        uint32_t seq1 = src->write_seq;
        MemoryBarrier();
        if (seq1 & 1) continue;

        memcpy(dst, (const void*)src, sizeof(*dst));
        MemoryBarrier();

        uint32_t seq2 = src->write_seq;
        if (seq1 == seq2) return true;
    }
    return false;
}

/* ── Create sphere (point) light via Remix SDK ─────────────────────── */

static remixapi_LightHandle create_point_light(uint64_t hash,
    const float pos[3], const float col[3],
    float radius, float linear_att, float quad_att)
{
    remixapi_LightInfoSphereEXT ext = {};
    ext.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
    ext.position = { pos[0], pos[1], pos[2] };
    ext.radius = SPHERE_RADIUS;
    ext.volumetricRadianceScale = 1.0f;

    remixapi_LightInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    info.pNext = &ext;
    info.hash = hash;

    // Physically-motivated radiance: match game irradiance at d=1
    // Game: E(d) = col / (1 + linear*d + quad*d²)
    // Remix sphere: E(d) = L * r² / d²
    // At d=1: L = col / ((1 + linear + quad) * r²)
    float atten_at_1 = 1.0f + linear_att + quad_att;
    if (atten_at_1 < 0.1f) atten_at_1 = 0.1f;  // safety floor
    float r2 = SPHERE_RADIUS * SPHERE_RADIUS;
    float scale = BRIGHTNESS_K / (atten_at_1 * r2);

    info.radiance = { col[0] * scale, col[1] * scale, col[2] * scale };

    // One-time dump of first batch
    static int s_detail_logged = 0;
    if (s_detail_logged < 10)
    {
        log_msg("  LIGHT hash=%016llX pos=(%.1f,%.1f,%.1f) col=(%.4f,%.4f,%.4f) "
                "outerR=%.1f lin=%.3f quad=%.3f radiance=(%.1f,%.1f,%.1f)",
            (unsigned long long)hash, pos[0], pos[1], pos[2],
            col[0], col[1], col[2], radius, linear_att, quad_att,
            info.radiance.x, info.radiance.y, info.radiance.z);
        s_detail_logged++;
    }

    remixapi_LightHandle handle = nullptr;
    remixapi_ErrorCode rc = g_remix.CreateLight(&info, &handle);
    if (rc != REMIXAPI_ERROR_CODE_SUCCESS)
    {
        static int s_err_count = 0;
        if (s_err_count < 25)
        {
            log_msg("CreateLight error %d hash=%016llX pos=(%.2f,%.2f,%.2f)",
                (int)rc, (unsigned long long)hash, pos[0], pos[1], pos[2]);
            s_err_count++;
        }
        return nullptr;
    }
    return handle;
}

/* ── Process one frame of light data ────────────────────────────────── */

static void process_lights(const shared_light_data* data)
{
    if (!g_device_registered) return;

    int count = (int)data->light_count;
    if (count > SHMEM_MAX_LIGHTS) count = SHMEM_MAX_LIGHTS;

    // Collect active hashes this frame
    // Use a simple bitset-like approach: mark which hashes we've seen
    static std::unordered_map<uint64_t, bool> active_set;
    active_set.clear();

    for (int i = 0; i < count; i++)
    {
        const auto& light = data->lights[i];
        uint64_t hash = light.stable_hash;
        if (hash == 0) continue;  // invalid

        active_set[hash] = true;

        auto it = g_light_map.find(hash);
        bool need_create = false;

        if (it == g_light_map.end())
        {
            need_create = true;
        }
        else
        {
            // Check if light data changed (position, color, attenuation)
            auto& prev = it->second;
            if (memcmp(prev.pos, light.pos, sizeof(float) * 3) != 0 ||
                memcmp(prev.col, light.col, sizeof(float) * 3) != 0 ||
                prev.radius != light.radius ||
                prev.linear_att != light.linear_att ||
                prev.quad_att != light.quad_att)
            {
                // Destroy old, recreate with new parameters
                if (prev.handle)
                    g_remix.DestroyLight(prev.handle);
                g_light_map.erase(it);
                need_create = true;
            }
        }

        if (need_create)
        {
            remixapi_LightHandle handle = create_point_light(
                hash, light.pos, light.col,
                light.radius, light.linear_att, light.quad_att);

            remix_light_state state = {};
            state.handle = handle;
            memcpy(state.pos, light.pos, sizeof(float) * 3);
            memcpy(state.col, light.col, sizeof(float) * 3);
            state.radius = light.radius;
            state.linear_att = light.linear_att;
            state.quad_att = light.quad_att;
            g_light_map[hash] = state;
        }

        // DrawLightInstance every frame — tells Remix this light is still alive
        auto handle_it = g_light_map.find(hash);
        if (handle_it != g_light_map.end() && handle_it->second.handle)
            g_remix.DrawLightInstance(handle_it->second.handle);
    }

    // Destroy lights no longer present
    for (auto it = g_light_map.begin(); it != g_light_map.end(); )
    {
        if (active_set.find(it->first) == active_set.end())
        {
            if (it->second.handle)
                g_remix.DestroyLight(it->second.handle);
            it = g_light_map.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

/* ── Per-frame light processing (called from EndScene hook = render thread) ─ */

static void render_thread_update()
{
    if (!g_shm || !g_device_registered) return;

    shared_light_data local = {};
    if (!seqlock_read(g_shm, &local)) return;

    process_lights(&local);

    static bool s_first_lights = false;
    if (!s_first_lights && local.light_count > 0)
    {
        s_first_lights = true;
        log_msg("First lights arrived: %u point lights", local.light_count);
    }

    static uint32_t s_log_frame = 0;
    if (local.frame_id - s_log_frame >= 300)
    {
        s_log_frame = local.frame_id;
        log_msg("frame=%u lights=%u tracked=%zu registered=%d",
            local.frame_id, local.light_count,
            g_light_map.size(), (int)g_device_registered);
    }
}

/* ── Present/EndScene vtable hooks (render thread) ─────────────────── */

static HRESULT WINAPI hooked_EndScene(IDirect3DDevice9Ex* pThis)
{
    render_thread_update();
    return g_orig_EndScene(pThis);
}

static HRESULT WINAPI hooked_Present(
    IDirect3DDevice9Ex* pThis,
    const RECT* pSrc, const RECT* pDst,
    HWND hWndOverride, const void* pDirtyRegion)
{
    return g_orig_Present(pThis, pSrc, pDst, hWndOverride, pDirtyRegion);
}

static HRESULT WINAPI hooked_PresentEx(
    IDirect3DDevice9Ex* pThis,
    const RECT* pSrc, const RECT* pDst,
    HWND hWndOverride, const void* pDirtyRegion,
    DWORD dwFlags)
{
    return g_orig_PresentEx(pThis, pSrc, pDst, hWndOverride, pDirtyRegion, dwFlags);
}

/* ── Install Present hooks on captured device ──────────────────────── */

static void install_present_hooks(IDirect3DDevice9Ex* device)
{
    void** vtable = *(void***)device;
    DWORD old_protect = 0;

    /* EndScene (slot 42) — most reliable hook point */
    g_orig_EndScene = (PFN_EndScene)vtable[VTABLE_IDX_ENDSCENE];
    if (VirtualProtect(&vtable[VTABLE_IDX_ENDSCENE], sizeof(void*),
                       PAGE_READWRITE, &old_protect))
    {
        vtable[VTABLE_IDX_ENDSCENE] = (void*)hooked_EndScene;
        VirtualProtect(&vtable[VTABLE_IDX_ENDSCENE], sizeof(void*),
                       old_protect, &old_protect);
        log_msg("EndScene hook installed (slot %d)", VTABLE_IDX_ENDSCENE);
    }

    /* Present (slot 17) */
    g_orig_Present = (PFN_Present)vtable[VTABLE_IDX_PRESENT];
    if (VirtualProtect(&vtable[VTABLE_IDX_PRESENT], sizeof(void*),
                       PAGE_READWRITE, &old_protect))
    {
        vtable[VTABLE_IDX_PRESENT] = (void*)hooked_Present;
        VirtualProtect(&vtable[VTABLE_IDX_PRESENT], sizeof(void*),
                       old_protect, &old_protect);
        log_msg("Present hook installed (slot %d)", VTABLE_IDX_PRESENT);
    }

    /* PresentEx (slot 121) */
    g_orig_PresentEx = (PFN_PresentEx)vtable[VTABLE_IDX_PRESENTEX];
    if (VirtualProtect(&vtable[VTABLE_IDX_PRESENTEX], sizeof(void*),
                       PAGE_READWRITE, &old_protect))
    {
        vtable[VTABLE_IDX_PRESENTEX] = (void*)hooked_PresentEx;
        VirtualProtect(&vtable[VTABLE_IDX_PRESENTEX], sizeof(void*),
                       old_protect, &old_protect);
        log_msg("PresentEx hook installed (slot %d)", VTABLE_IDX_PRESENTEX);
    }
}

/* ── Light thread (init only — light processing runs on render thread) ── */

static DWORD WINAPI light_thread(LPVOID)
{
    log_msg("Light thread started (TID %u)", GetCurrentThreadId());

    /* Wait for DXVK-Remix d3d9.dll to load */
    HMODULE dxvk = nullptr;
    for (int i = 0; i < 6000 && !dxvk; i++)
    {
        dxvk = GetModuleHandleA("d3d9.dll");
        if (!dxvk) Sleep(10);
    }
    if (!dxvk) { log_msg("d3d9.dll never loaded"); return 1; }
    log_msg("DXVK-Remix d3d9.dll at %p", dxvk);

    /* Initialize Remix API */
    auto pfn_init = (PFN_remixapi_InitializeLibrary)
        GetProcAddress(dxvk, "remixapi_InitializeLibrary");
    if (!pfn_init) { log_msg("InitializeLibrary not exported"); return 1; }

    for (int attempt = 0; attempt < 6000; attempt++)
    {
        remixapi_InitializeLibraryInfo init_info = {};
        init_info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
        init_info.version = REMIXAPI_VERSION_MAKE(
            REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);

        remixapi_Interface iface = {};
        remixapi_ErrorCode rc = pfn_init(&init_info, &iface);
        if (rc == REMIXAPI_ERROR_CODE_SUCCESS)
        {
            g_remix = iface;
            g_api_ready = true;
            log_msg("Remix API initialized (attempt %d)", attempt + 1);
            break;
        }
        Sleep(10);
    }
    if (!g_api_ready) { log_msg("Remix API never initialized"); return 1; }

    log_msg("CreateLight=%p RegisterDevice=%p",
        g_remix.CreateLight, g_remix.dxvk_RegisterD3D9Device);

    /* Wait for device to be captured by CreateDevice/CreateDeviceEx hook */
    for (int i = 0; i < 6000 && !g_captured_device; i++)
        Sleep(10);

    if (g_captured_device && g_remix.dxvk_RegisterD3D9Device)
    {
        remixapi_ErrorCode rc = g_remix.dxvk_RegisterD3D9Device(
            (IDirect3DDevice9Ex*)g_captured_device);
        log_msg("dxvk_RegisterD3D9Device(%p) returned %d",
                (void*)g_captured_device, (int)rc);
        if (rc == REMIXAPI_ERROR_CODE_SUCCESS)
        {
            g_device_registered = true;
            log_msg("Device registered — lights will emit from EndScene hook");
        }
    }
    else
    {
        log_msg("WARN: device never captured — lights will not work");
    }

    /* Open shared memory */
    for (int i = 0; i < 6000 && !g_shm; i++)
    {
        g_shmem_handle = OpenFileMappingA(
            FILE_MAP_READ | FILE_MAP_WRITE, FALSE, SHMEM_LIGHT_NAME);
        if (g_shmem_handle)
        {
            g_shm = (shared_light_data*)MapViewOfFile(
                g_shmem_handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                sizeof(shared_light_data));
            if (!g_shm) { CloseHandle(g_shmem_handle); g_shmem_handle = nullptr; }
        }
        if (!g_shm) Sleep(10);
    }
    if (!g_shm) { log_msg("Shared memory never appeared"); return 1; }
    log_msg("Shared memory at %p", g_shm);

    InterlockedExchange((volatile LONG*)&g_shm->server_active, 1);
    log_msg("server_active set to 1");

    log_msg("Init thread done — render thread will process lights");
    return 0;
}

/* ── DllMain ────────────────────────────────────────────────────────── */

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        g_log = fopen("remix_lights.log", "w");
        log_msg("remix_lights.dll loaded into PID %u", GetCurrentProcessId());

        /* IAT-hook GetProcAddress SYNCHRONOUSLY — before bridge init continues.
         * This catches the bridge's GetProcAddress("Direct3DCreate9") call. */
        install_getprocaddress_hook();

        HANDLE t = CreateThread(nullptr, 0, light_thread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_api_ready)
        {
            for (auto& [hash, state] : g_light_map)
                if (state.handle && g_remix.DestroyLight)
                    g_remix.DestroyLight(state.handle);
            g_light_map.clear();
        }
        if (g_shm) UnmapViewOfFile((void*)g_shm);
        if (g_shmem_handle) CloseHandle(g_shmem_handle);
        if (g_log) { log_msg("remix_lights.dll unloading"); fclose(g_log); }
    }
    return TRUE;
}
