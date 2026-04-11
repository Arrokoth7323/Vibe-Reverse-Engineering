## remixapi_InitializeLibrary Internal Gate Analysis — d3d9_remix.dll RVA 0x593A0

### Summary

`remixapi_InitializeLibrary` (VA 0x100593A0, `__stdcall`, 2 args, returns `int` in eax) has three exit paths. Error code 11 (`REMIXAPI_ERROR_CODE_NOT_INITIALIZED` / 0x0B) is returned when the single-byte global at **VA 0x100c5e01** is zero. This byte is the parsed value of the `exposeRemixApi` bridge.conf option, stored at offset +0x71 of the global bridge config object (base ~0x100c5d90). The default value is **false (0)** — even though the on-disk PE has this byte as 1, the config initialization function `0x10043290` overwrites it with the parsed bridge.conf value, whose default argument is 0.

### Three Return Paths (from disassembly)

| Path | Condition | Return (eax) | Code Address |
|------|-----------|--------------|--------------|
| API disabled | `byte [0x100c5e01] == 0` | **0x0B (11)** | `0x10059457: mov eax, 0xb` |
| Invalid args | API enabled, but `param1==NULL` or `*param1!=1` or `param2==NULL` | **3** | `0x10059546: mov eax, 3` |
| Success | API enabled + valid args | **0** | `0x10059527: xor eax, eax` |

### Gate Check Detail

```asm
0x100593AE: cmp  byte ptr [0x100c5e01], 0   ; exposeRemixApi flag
0x100593B5: push esi
0x100593B6: mov  esi, [esp+0x7C]            ; param_2 (output interface ptr)
0x100593BA: jne  0x1005946e                 ; if flag != 0, proceed to param validation
; ... else: log error, return 0xB
0x10059457: mov  eax, 0xb                   ; REMIXAPI_ERROR_CODE_NOT_INITIALIZED
0x1005945C: pop  esi
; ... stack cookie check, ret 8
```

### How the Flag Gets Set

The config init function at **0x10043290** (`__fastcall`, ecx = config object ptr) populates all bridge options from `.trex/bridge.conf`. Near the end:

```c
// At 0x10043759 inside func 0x10043290:
bool exposeApi = readConfigBool("exposeRemixApi", /*default=*/ 0);  // func_0x1001ca20
*(configObj + 0x71) = exposeApi;   // → absolute addr 0x100c5e01
```

The config accessor `func_0x1001ca20` has its own gate:
- Checks `byte [0x100c7a36]` — the "config system initialized" sentinel
- If sentinel is 0: logs **"ClientOptions accessed before Config initialized"** and returns the DEFAULT value (which is 0/false for `exposeRemixApi`)
- If sentinel is non-zero: reads the actual value from the parsed config file

### Why It Perpetually Returns 11 (Root Cause Analysis)

The game's `.trex/bridge.conf` **does** contain `exposeRemixApi = true`. The on-disk PE byte at 0x100c5e01 is 1. So the flag *should* be true at runtime. Possible failure modes:

1. **Config init timing**: If the proxy calls `remixapi_InitializeLibrary` before the bridge's config system has run `func_0x10043290`, the byte is still at its BSS-zero value (the on-disk value of 1 is in `.data` but may be overwritten to 0 by early init code before config parsing completes).

2. **Config sentinel check failure**: `func_0x1001ca20` checks `byte [0x100c7a36]`. If the config system hasn't been initialized when this accessor is called during `func_0x10043290`, it would return the default (0) for `exposeRemixApi`. This is a bootstrap ordering issue within the bridge itself.

3. **Module identity confusion**: The user's memory notes "bridge's GetModuleHandleA finds us" — the bridge client may use `GetModuleHandleA("d3d9.dll")` during its own init to locate itself. With a proxy named `d3d9.dll` in the load path, this returns the proxy's HMODULE, not the bridge's. This could cause the bridge's init code to fail silently, leaving the config unpopulated.

4. **DLL load address / ASLR**: The byte at PE offset 0x100c5e01 has value 1 on disk, but at runtime the DLL may be relocated. All internal references are relocated too, so this shouldn't matter — unless an external caller is reading by hardcoded address.

### On-Success Path Detail

When the gate passes and params are valid, the function fills `param_2` with a vtable of 21 function pointers (the Remix API interface):

| Slot | VA | Likely Function |
|------|----|-----------------|
| 1 | 0x10057DD0 | API function #1 |
| 2 | 0x10058840 | API function #2 |
| 3 | 0x10058510 | API function #3 |
| 4 | 0x10058900 | API function #4 |
| 6 | 0x100589C0 | API function #6 |
| 7 | 0x10057490 | API function #7 |
| 8 | 0x10058780 | API function #8 |
| 9 | 0x10059040 | API function #9 |
| 10 | 0x10059100 | API function #10 |
| 11 | 0x100591C0 | API function #11 |
| 12 | 0x100592B0 | API function #12 |

Then sets `byte [0x100c65e0] = 1` — "library initialized" flag.

### Version Check

`*param_1 == 1` — the first dword of the input struct must be 1 (API version). If wrong, returns error 3.

### Related Export: remixapi_RegisterCallbacks (RVA 0x59560)

Trivial function — stores 3 callback pointers at globals 0x100c65e4/e8/ec, returns 0. No gate check (does NOT check `exposeRemixApi`).

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x100593A0 | `remixapi_InitializeLibrary` — main entry |
| 0x100c5e01 | `g_exposeRemixApi` — byte flag, offset +0x71 of config object |
| 0x100c5d90 | Config object base (0x100c5e01 - 0x71) |
| 0x100c7a36 | Config system initialized sentinel |
| 0x10043290 | `BridgeClientConfig::init()` — reads all bridge.conf options |
| 0x1001ca20 | `readConfigBool()` — reads a bool option with default |
| 0x100c65e0 | `g_remixApiInitialized` — set to 1 on success |
| 0x10059560 | `remixapi_RegisterCallbacks` — stores 3 callback pointers |
| 0x100a9d98 | Error string: "Remix API is not enabled..." |

### Suggested Live Verification

1. **Attach to running game** and read `byte [d3d9_remix_base + 0xC5E01]` — confirm whether the flag is 0 or 1 at runtime after CreateDevice
2. **Read `byte [d3d9_remix_base + 0xC7A36]`** — confirm config system is initialized (should be non-zero)
3. **Set breakpoint at `d3d9_remix_base + 0x593AE`** (the `cmp` instruction) — watch what value the byte has when the proxy calls InitializeLibrary
4. **If the byte is 0**: set breakpoint at `d3d9_remix_base + 0x43759`** (where exposeRemixApi is written) — confirm if `readConfigBool` returns 0 or 1
5. **Force-patch**: write 1 to `byte [d3d9_remix_base + 0xC5E01]` before calling InitializeLibrary — if this makes it succeed, the root cause is config timing

## NvRemixBridge.exe — Direct3DCreate9Ex Resolution Analysis

### Summary

**NvRemixBridge.exe does NOT resolve `Direct3DCreate9Ex` at all.** There is no static import of d3d9.dll, no "Direct3DCreate9Ex" string in the binary, and no `GetProcAddress` call for it. The bridge dynamically loads d3d9.dll (DXVK/Remix), resolves only `Direct3DCreate9` via `GetProcAddress`, and then directly calls `CreateDeviceEx` through vtable slot 0xA0 on the returned `IDirect3D9*` object — relying on DXVK/Remix's implementation returning an object whose vtable includes `IDirect3D9Ex` methods even from the non-Ex factory function.

### Import Table — No d3d9.dll

The PE import table contains only: `VERSION.dll`, `KERNEL32.dll`, `USER32.dll`, `SHELL32.dll`, `ole32.dll`, `dbghelp.dll`. **No d3d9.dll import.** All D3D9 interaction is via dynamic loading.

Relevant KERNEL32 imports for dynamic resolution:
- `LoadLibraryA` / `LoadLibraryW` / `LoadLibraryExW`
- `GetProcAddress`
- `FreeLibrary`

### String Evidence

| Address | String | Xref |
|---------|--------|------|
| 0x14007C108 | `d3d9vk_x64.dll` | 0x1400116CF — LoadLibrary target (non-RTX DXVK fallback) |
| 0x140077960 | `d3d9.dll` | 0x140011839 — LoadLibrary target (RTX Remix DXVK) |
| 0x14007C160 | `Direct3DCreate9` | 0x140011856 — GetProcAddress lookup name |
| — | `Direct3DCreate9Ex` | **NOT PRESENT in binary** |
| 0x14007B8B8 | `Server side D3D9 DeviceEx created successfully!` | 0x140012F49 — CreateDeviceEx success log |
| 0x14007B950 | `Server side D3D9 Device created successfully!` | 0x14001324F — CreateDevice success log |

### Initialization Flow (function at 0x1400115E0, 3952 bytes)

```
1. LoadLibraryA("d3d9vk_x64.dll")     ; [0x1400116CF-0x1400116D6]
   |
   +-- If loaded → log "Non-RTX standard d3d9vk_x64.dll loaded"
   |                → use this HMODULE
   +-- If failed → log "d3d9vk_x64.dll loading failed!"
                  → fall through to step 2

2. LoadLibraryA("d3d9.dll")            ; [0x140011839-0x140011840]
   → store HMODULE at [0x140098B80]

3. GetProcAddress(hModule, "Direct3DCreate9")  ; [0x140011856-0x140011860]

4. Direct3DCreate9(0x20)               ; [0x140011866-0x14001186B]
   → SDK_VERSION = 32 (D3D_SDK_VERSION)
   → store IDirect3D9* at [0x140098B80]
```

### DeviceEx Creation (separate command handler at ~0x140012E54)

The bridge is a client-server architecture: the 32-bit game client sends commands to the 64-bit NvRemixBridge.exe server. `CreateDeviceEx` and `CreateDevice` are separate command handlers in a large dispatch function (0x140012BA9).

**CreateDeviceEx path:**
```asm
0x140012E54: mov  rcx, [rip + 0x85d25]    ; load IDirect3D9* (same global as step 4)
0x140012E5B: mov  rax, [rcx]              ; vtable pointer
0x140012E5E: mov  r10, [rax + 0xA0]       ; vtable slot 0xA0 = IDirect3D9Ex::CreateDeviceEx
; ... set up 7 arguments ...
0x140012E96: call r10                      ; CreateDeviceEx(Adapter, DevType, hWnd, BehaviorFlags, pParams, pDisplayMode, ppDevice)
0x140012E9B: test eax, eax
0x140012E9D: jns  0x140012F49              ; if SUCCEEDED → log "DeviceEx created successfully!"
```

**CreateDevice fallback path:**
```asm
0x140013169: mov  rcx, [rip + 0x85a10]    ; load same IDirect3D9*
0x140013170: mov  rax, [rcx]              ; vtable pointer
0x140013173: mov  r10, [rax + 0x80]       ; vtable slot 0x80 = IDirect3D9::CreateDevice
; ... set up 6 arguments ...
0x14001319C: call r10                      ; CreateDevice(Adapter, DevType, hWnd, BehaviorFlags, pParams, ppDevice)
0x1400131A1: test eax, eax
0x1400131A3: jns  0x14001324F              ; if SUCCEEDED → log "Device created successfully!"
```

### IDirect3D9 Vtable Layout (x64, 8 bytes per slot)

| Offset | Method | Used by Bridge |
|--------|--------|----------------|
| 0x00 | QueryInterface | — |
| 0x08 | AddRef | — |
| 0x10 | Release | — |
| 0x18-0x78 | IDirect3D9 methods | — |
| **0x80** | **CreateDevice** | **Yes — fallback path** |
| 0x88 | GetAdapterModeCountEx | — |
| 0x90 | EnumAdapterModesEx | — |
| 0x98 | GetAdapterDisplayModeEx | — |
| **0xA0** | **CreateDeviceEx** | **Yes — primary path** |

### Key Finding

The bridge **assumes** that the d3d9.dll it loads (DXVK/RTX Remix) returns an `IDirect3D9Ex`-compatible object from `Direct3DCreate9`. This is a valid assumption because DXVK always allocates an `IDirect3D9Ex` implementation internally regardless of which factory function the caller used. The bridge skips `QueryInterface` entirely — it directly dereferences vtable+0xA0 on the object returned by the non-Ex factory.

If you were to point NvRemixBridge.exe at the real Microsoft d3d9.dll instead of DXVK, the `CreateDeviceEx` call would read past the end of the real `IDirect3D9` vtable (which ends at 0x88) and crash or call garbage.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x1400115E0 | D3D9 initialization function (loads DLL, creates IDirect3D9) |
| 0x140077960 | String: "d3d9.dll" |
| 0x14007C108 | String: "d3d9vk_x64.dll" |
| 0x14007C160 | String: "Direct3DCreate9" |
| 0x140098B80 | Global: stored IDirect3D9* (and HMODULE before that) |
| 0x140012E96 | `call r10` — IDirect3D9Ex::CreateDeviceEx via vtable+0xA0 |
| 0x14001319C | `call r10` — IDirect3D9::CreateDevice via vtable+0x80 |
