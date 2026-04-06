/* LOTR Conquest (Pandemic Studios "Magellan" Engine) — Knowledge Base
 * Binary: Conquest.exe (32-bit, x86, MSVC, no ASLR)
 * Engine: Magellan (d:\Projects\magellan\final_pc\)
 * Renderer: D3D9 (vs_3_0 / ps_3_0), all transforms via shader constants
 */

/* ============================================================
 *  Renderer Class Hierarchy (RTTI)
 * ============================================================ */

// MgRenderer — abstract renderer interface
// vtable @ 0x9FC71C   RTTI: .?AVMgRenderer@@
struct MgRenderer;

// MgRendererWin32 — concrete D3D9 renderer, inherits MgRenderer
// vtable @ 0x9FC5F4   RTTI: .?AVMgRendererWin32@@
// Size: >= 0x15CB0 bytes
//
// MgRendererWin32 vtable method map:
//   [0]  +0x00  Destructor (0x0048E7FF)
//   [1]  +0x04  Release (0x0048EA7E)
//   [2]  +0x08  ResetState — sets renderState to -1 (0x0048EA76)
//   [3]  +0x0C  CreateVertexDeclaration (0x006A5CAC)
//   [4]  +0x10  CreateVertexShader (0x006A527C)
//   [7]  +0x1C  SetRenderTarget (0x0069E851)
//   [8]  +0x20  SetViewport (0x0069E82C)
//   [9]  +0x24  SetScissorRect (0x0069E809)
//   [10] +0x28  SetMultiRenderTarget (0x0069E7E1)
//   [11] +0x2C  EndScene / sync (0x0069E72E)
//   [13] +0x34  TestCooperativeLevel — calls IDirect3DDevice9::TestCooperativeLevel
//   [14] +0x38  BeginFrame — calls D3D9 Present + BeginScene (front-loaded)
//   [15] +0x3C  RenderPasses — main render loop, iterates RTs, sets state, issues draws
//   [16] +0x40  EndFrame — calls D3D9 EndScene, clears per-frame state
//   [18] +0x48  EndScene (skip/lost device path)
//   [19] +0x4C  Present/Flush
//   [20] +0x50  GetField_15C88 / post-render query
//   [21] +0x54  SetEnabled — writes this->isEnabled
//   [22] +0x58  IsDeviceReady (0x0069E69B)
//   [23] +0x5C  ValidateDevice (0x006A598D)
//   [24] +0x60  HandleDeviceLost (0x006A4BE6)
//   [25] +0x64  CreateDevice (0x0048E224)
//   [26] +0x68  SetupRenderTargets (0x006A4007)
//   [27] +0x6C  SetField_D80 (0x0048E7B8)
struct MgRendererWin32 {
    /* +0x0000  */ void* vtable;
    /* ...       padding ...  */
    /* +0x0AA8  */ int dirtyFlags;                   // bitmask of state groups needing update:
                                                     //   0x20      = raster state
                                                     //   0x40      = z-buffer state
                                                     //   0x80      = alpha blend state
                                                     //   0x100     = lighting state
                                                     //   0x800     = fog state
                                                     //   0x1000    = point sprite state
                                                     //   0x8000    = texture stage state
                                                     //   0x10000   = sampler state
                                                     //   0x20000   = RT/viewport/scissor
                                                     //   0x40000   = clip plane
                                                     //   0x80000   = (force-skip item flag)
                                                     //   0x200000  = stencil state
                                                     //   0x400000  = multisample state
                                                     //   0x800000  = scissor rect
                                                     //   0x1000000 = (reserved)
                                                     //   0x2000000 = depth bias
                                                     //   0x4000000 = color write
                                                     //   0x8000000 = stencil ref/op
                                                     //   0x40000000 = post-process state
                                                     //   0x80000000 = force-all render target mode
    /* +0x0AAC  */ int dirtyFlags2;                  // secondary dirty flags
    /* +0x0C20  */ int drawCount;                    // draw call counter per frame
    /* +0x0C24  */ int primCount;                    // total primitive count per frame
    /* +0x0C28  */ void* currentVS;                  // cached current vertex shader
    /* +0x0C2C  */ void* currentPS;                  // cached current pixel shader
    /* +0x0C34  */ int cachedVDeclIdx;               // cached vertex declaration index (-1 = invalid)
    /* +0x0C38  */ int cachedTexSetIdx;              // cached texture/sampler set index
    /* +0x0C40  */ int cachedEffectIdx;              // cached material/effect index
    /* +0x0C44  */ int cachedVSIdx;                  // cached vertex shader index
    /* +0x0C48  */ int cachedVBIdx;                  // cached vertex buffer index
    /* +0x0C4C  */ int cachedVSProgIdx;              // cached VS program (bits 0-10 of item+0x30)
    /* +0x0C50  */ int cachedPSProgIdx;              // cached PS program (bits 11-21 of item+0x30)
    /* +0x0C54  */ int cachedVSConstIdx;             // cached VS constant block (-1 = invalid)
    /* +0x0C58  */ int cachedPSConstIdx;             // cached PS constant block
    /* +0x0C60  */ int cachedMtlTechIdx;             // cached material technique
    /* +0x0C64  */ int cachedSamplerIdx;             // cached sampler state
    /* +0x0C68  */ int cachedBlendMode;              // cached blend mode
    /* +0x0C74  */ int currentPass;                  // current render pass index
    /* +0x0C78  */ int currentTechnique;             // current technique slot
    /* +0x0C88  */ void* cachedWorldMatrix;          // cached World matrix pointer
    /* +0x0C8C  */ void* cachedIndexBuffer;          // cached index buffer
    /* +0x0D64  */ void* currentRenderItem;          // current render item pointer
    /* +0x0D80  */ int field_D80;                    // set via vtable[27]
    /* +0x115D4 */ void* constantUploadIface;        // VS/PS constant upload interface
                                                     //   [iface+0xC](regIdx, data, count) = SetConstantF
    /* +0x15630 */ int renderState;                  // -1 on init (vtable[2])
    /* +0x15684 */ void* pPSStateManager;            // released in EndFrame
    /* +0x15688 */ void* pD3DDevice;                 // IDirect3DDevice9*
    /* +0x1567E */ char isEnabled;                   // bool, toggled via vtable[21]
    /* +0x15C54 */ char field_15C54;                 // checked in vtable[22]
    /* +0x15C58 */ void* rtSurfaceA;                 // render target surface A
    /* +0x15C60 */ int field_15C60;                  // pushed in vtable[26]
    /* +0x15C80 */ float resolutionScale;            // resolution/quality scale factor
    /* +0x15C88 */ int field_15C88;                  // read in vtable[20]
    /* +0x15C8C */ void* rtSurfaceB;                 // render target surface B
    /* +0x15C90 */ int callbackQueueProcessed;       // cleared after flush
    /* +0x15C94 */ void** callbackQueue;             // deferred callback array
    /* +0x15C98 */ int callbackQueueCount;
    /* +0x15CAC */ void** endFrameCallbacks;         // end-of-frame callback array
    /* +0x15CB0 */ int endFrameCallbackCount;
    /* +0x15CFC */ void* pDeviceQuery;               // D3D query (device lost detection, checks 0x88760868)
};

// Render Item — 0x4C (76) bytes, stored in table at *0xA363EC
struct MgRenderItem {
    /* +0x00  */ int field_00;
    /* +0x30  */ int shaderIndices;    // bits[0:10] = VS program index, bits[11:21] = PS program index
    /* +0x34  */ void* boundingVolume; // bounding sphere/box for frustum culling
    /* +0x40  */ int flags;            // 0x2000 = needs draw, 0x80000 = force-skip
    /* +0x48  */ int field_48;
};

// Render Batch Entry — 0x250 (592) bytes, in array at g_taskTable
struct MgRenderBatchEntry {
    /* +0x00  */ int deltaTimeLo;      // 64-bit time delta (low)
    /* +0x04  */ int deltaTimeHi;      // 64-bit time delta (high)
    /* +0x08  */ int counterA;         // swapped from pending each frame
    /* +0x0C  */ int counterB;
    /* +0x10  */ int prevTimestampLo;  // previous frame timestamp
    /* +0x14  */ int prevTimestampHi;
    /* +0x18  */ int pendingCounterA;  // zeroed after swap
    /* +0x1C  */ int pendingCounterB;
    /* +0x2C  */ int activeFlag;       // 0 = process, nonzero = skip
};

/* ============================================================
 *  State Block Classes
 * ============================================================ */

// HAL = abstract, Win32 = D3D9 concrete
struct MgPrimitiveStateBlockHAL;    // RTTI: .?AVMgPrimitiveStateBlockHAL@@
struct MgPrimitiveStateBlockWin32;  // RTTI: .?AVMgPrimitiveStateBlockWin32@@
struct MgSamplerStateBlockHAL;      // RTTI: .?AVMgSamplerStateBlockHAL@@
struct MgSamplerStateBlockWin32;    // RTTI: .?AVMgSamplerStateBlockWin32@@
struct MgRasterStateBlockHAL;       // RTTI: .?AVMgRasterStateBlockHAL@@
struct MgRasterStateBlockWin32;     // RTTI: .?AVMgRasterStateBlockWin32@@
struct MgZStencilStateBlockHAL;     // RTTI: .?AVMgZStencilStateBlockHAL@@
struct MgZStencilStateBlockWin32;   // RTTI: .?AVMgZStencilStateBlockWin32@@
struct MgAlphaStateBlockHAL;        // RTTI: .?AVMgAlphaStateBlockHAL@@
struct MgAlphaStateBlockWin32;      // RTTI: .?AVMgAlphaStateBlockWin32@@

/* ============================================================
 *  Material System
 * ============================================================ */

struct MgMaterial;                          // RTTI: .?AVMgMaterial@@
struct MgMaterialWin32;                     // RTTI: .?AVMgMaterialWin32@@
struct MgMaterialNormalWin32;               // RTTI: .?AVMgMaterialNormalWin32@@
struct MgMaterialTerrainWin32;              // RTTI: .?AVMgMaterialTerrainWin32@@
struct MgMaterialCharacterVariationWin32;   // RTTI: .?AVMgMaterialCharacterVariationWin32@@
struct MgMaterialVariationWin32;            // RTTI: .?AVMgMaterialVariationWin32@@

/* ============================================================
 *  Shader System
 * ============================================================ */

struct MgShader;                    // RTTI: .?AVMgShader@@
struct MgVertexShaderWin32;         // RTTI: .?AVMgVertexShaderWin32@@
struct MgFragmentShaderWin32;       // RTTI: .?AVMgFragmentShaderWin32@@
struct MgNULLVertexShaderWin32;     // RTTI: .?AVMgNULLVertexShaderWin32@@
struct MgNULLFragmentShaderWin32;   // RTTI: .?AVMgNULLFragmentShaderWin32@@
struct MgConstantStoreHAL;          // RTTI: .?AVMgConstantStoreHAL@@
struct MgConstantStoreWin32;        // RTTI: .?AVMgConstantStoreWin32@@

// MgConstantStoreWin32 batches constant uploads:
//   +0x04 = VS batch count
//   +0x1008 = PS batch count  (0x402 * sizeof(int))
//   Each batch entry: { startReg, count, data_ptr_or_null }
//   If data_ptr is null, inline data at self + startReg*4 + 0x200C (VS) or +0x300C (PS)
//   Flush calls IDirect3DDevice9::SetVertexShaderConstantF (vtable+0x10)
//   and IDirect3DDevice9::SetPixelShaderConstantF (vtable+0x14) on wrapper

/* ============================================================
 *  Other Rendering Classes
 * ============================================================ */

struct MgModel;                     // RTTI: .?AVMgModel@@
struct MgSurface;                   // RTTI: .?AVMgSurface@@
struct MgSurfaceWin32;              // RTTI: .?AVMgSurfaceWin32@@
struct MgFilter;                    // RTTI: .?AVMgFilter@@
struct MgToneMapFilterWin32;        // RTTI: .?AVMgToneMapFilterWin32@@
struct MgCommandBufferHAL;          // RTTI: .?AVMgCommandBufferHAL@@
struct MgCommandBufferWin32;        // RTTI: .?AVMgCommandBufferWin32@@

// FakeD3D wrappers — HAL abstraction over D3D9 objects
struct FakeD3D;                     // RTTI: .?AVFakeD3D@@
struct FakeD3DDevice;               // RTTI: .?AVFakeD3DDevice@@
struct FakeD3DQuery;                // RTTI: .?AVFakeD3DQuery@@
struct FakeD3DPixelShader;          // RTTI: .?AVFakeD3DPixelShader@@
struct FakeD3DVertexShader;         // RTTI: .?AVFakeD3DVertexShader@@
struct FakeD3DVertexDeclaration;    // RTTI: .?AVFakeD3DVertexDeclaration@@
struct FakeD3DIndexBuffer;          // RTTI: .?AVFakeD3DIndexBuffer@@
struct FakeD3DVertexBuffer;         // RTTI: .?AVFakeD3DVertexBuffer@@
struct FakeD3DSurface;              // RTTI: .?AVFakeD3DSurface@@
struct FakeD3DCubeTexture;          // RTTI: .?AVFakeD3DCubeTexture@@
struct FakeD3DVolumeTexture;        // RTTI: .?AVFakeD3DVolumeTexture@@
struct FakeD3DTexture;              // RTTI: .?AVFakeD3DTexture@@

/* ============================================================
 *  Global Variables
 * ============================================================ */

$ 0xD02060 MgRenderer* g_pRenderer     // Current renderer backend (MgRendererWin32 at runtime)
$ 0xD176E8 IDirect3DDevice9* g_pD3DDevice  // Raw D3D9 device pointer
$ 0xCD80C4 int g_surfaceParam          // Referenced by MgRendererWin32::vtable[14]
$ 0xCD80A0 int g_rendererParam         // Referenced by MgRendererWin32::vtable[20]
$ 0xCD80A4 int g_instancingMultiplier  // Used in instancing offset calculation
$ 0xCD8098 void* g_engineState         // Global engine state (material/render state tables)
$ 0xCD5A3C int g_renderPassCount       // Number of render passes
$ 0xCD7FC0 int g_frameCounter          // Frame index (mod 3, triple-buffered)
$ 0xE56AD0 char g_renderSkip           // If nonzero, skip rendering
$ 0xA41538 void** g_deferredReleaseQueue   // Array of textures to release at end of frame
$ 0xA4153C int g_deferredReleaseCount      // Count of deferred releases
$ 0xA4A6F8 void* g_mainRenderThread    // "Main thread (rendering)" task descriptor
$ 0xA4A718 void* g_renderTask          // "Render task" descriptor
$ 0xCD88F8 int64 g_perfCounter         // Current performance counter value
$ 0xCD8918 int64 g_ticksA              // Raw tick value A
$ 0xCD8920 int64 g_ticksB              // Raw tick value B
$ 0xCD8928 int g_msTimestampA          // Millisecond timestamp A (ticks * 1000)
$ 0xCD892C int g_msTimestampB          // Millisecond timestamp B
$ 0xCD8930 float g_deltaTimeA          // Float delta-time A (seconds)
$ 0xCD8934 float g_deltaTimeB          // Float delta-time B
$ 0xD17678 float g_renderDtA           // Per-frame copy of g_deltaTimeA
$ 0xD17680 float g_renderDtB           // Per-frame copy of g_deltaTimeB
$ 0xD1767C int g_renderMsA             // Per-frame copy of g_msTimestampA
$ 0xD17684 int g_renderMsB             // Per-frame copy of g_msTimestampB
$ 0xD17688 int g_renderTimingExtra     // Additional timing value
$ 0xD1768C int g_renderTimingExtra2
$ 0xD17690 int g_renderThreadSlot      // Render thread task slot index
$ 0xCD7FD4 void* g_subsystemB          // Subsystem B, executed before render (vtable+0x10)
$ 0xCD7FD8 void* g_subsystemA          // Subsystem A, executed before render (vtable+0x10)
$ 0xCD7E30 void* g_inputSubsystem      // Input subsystem object
$ 0xCD7E39 char g_renderFeatureFlag    // Toggled by command 0x2C
$ 0xCD8188 int g_keyboardLayout        // Current keyboard layout (GetKeyboardLayout)
$ 0xCD8166 char g_numLockState         // NumLock key state
$ 0xE56AA8 void* g_appObject           // Main application object (0x218 bytes)
$ 0xE56AC8 int g_pauseFadeIn           // Pause fade-in frame counter
$ 0xE56ACC int g_pauseFadeOut          // Pause fade-out frame counter
$ 0xCD5CA8 void* g_taskTable           // Thread task table (stride 0x250 per entry)
$ 0xA363EC void* g_renderItemTable     // Base of render item table (items are 0x4C bytes)
$ 0xCD8094 void* g_cameraFrustum       // Camera/frustum object for visibility culling
$ 0xCD8098 void* g_shaderResourceMgr   // Shader/resource manager base (shader lookup)
$ 0xCD8168 int g_renderThreadSync      // InterlockedExchange sync flag (multi-threaded submit)

/* ============================================================
 *  Key Functions — Main Loop / Frame
 *
 *  Full call chain (top to bottom):
 *    008A043B  Win32 message pump (PeekMessage/DispatchMessage)
 *      008A053A  Per-frame tick
 *        0089E466  Update frame delta-time (perf counters)
 *        0089E7C9  Render command processor (dispatch queue)
 *          -> Process queued commands (load, pause, scene transitions)
 *          -> When queue empty:
 *               MgRendererWin32::TestCooperativeLevel()  [vtable+0x34]
 *               MgRendererWin32::BeginFrame()            [vtable+0x38]
 *                 (calls D3D9 Present + BeginScene — front-loaded present)
 *               MgRendererWin32::RenderPasses()          [vtable+0x3C]
 *                 (loops render targets, sets state, issues all draw calls)
 *               MgRendererWin32::EndFrame()              [vtable+0x40]
 *                 (calls D3D9 EndScene, clears per-frame state)
 *          0089EA51  Frame finalize: triple-buffer rotate, swap command lists
 *            MgRendererWin32::Present()                  [vtable+0x4C]
 *            00746BD8  Swap draw batch buffer
 *            00749D47  Swap secondary buffer
 * ============================================================ */

@ 0x008A0549 void __cdecl MgGame_AppInit();
    // Allocates core objects, inits subsystems, parses input.xml, registers thread tasks
@ 0x008A043B void __cdecl MgGame_MessagePump();
    // Win32 message loop: PeekMessage/TranslateMessage/DispatchMessage, tracks keyboard
@ 0x008A053A void __fastcall MgGame_FrameTick(uint param_1);
    // Per-frame entry: calls MgEngine_UpdateTiming + MgRenderThread_CommandProcessor
@ 0x0089E466 void __cdecl MgEngine_UpdateTiming();
    // Perf counter delta → ms at 0xCD8928, float dt at 0xCD8930
@ 0x006780BB void __cdecl MgEngine_CalcDeltaTime(int64 ticks_a, int64 ticks_b);
    // Converts raw perf counter ticks to ms and float seconds
    // Stores: 0xCD8918-0xCD8934 (timing), copied to 0xD17678-0xD1768C per frame
@ 0x0089E7C9 void __cdecl MgRenderThread_CommandProcessor();
    // Command queue dispatcher. Opcodes:
    //   0x01/0x02: pause fade in/out counters
    //   0x24: hot-reload resources
    //   0x25: scene reset (level unload)
    //   0x26: level transition
    //   0x27: resource init + level load
    //   0x28: mode switch (menu<->game)
    //   0x2C: toggle render feature flag
    //   0x2D: toggle renderer setting via vtable[21]
    //   0x34: synchronization signal
@ 0x0089EA51 void __cdecl MgRenderThread_FrameFinalize();
    // Calls MgRendererWin32::Present [vtable+0x4C], rotates g_frameCounter mod 3,
    // swaps command buffers (00746BD8, 00749D47)
@ 0x0089E515 void __cdecl MgRenderThread_Inner();
    // Core render task: TestCoopLevel → BeginFrame → copy timing → RenderPasses → EndFrame
@ 0x0089E508 void __cdecl MgRenderer_RenderBackend();
    // 8KB jump-table state machine, dispatches via [g_pRenderer + 0x4C]
    // 64 return points, 164 callees — the core render orchestrator

/* ============================================================
 *  Key Functions — Draw Submission
 * ============================================================ */

@ 0x0067B120 void __cdecl MgRenderer_ProcessRenderList();
    // Iterates render batch entries (stride 0x250), computes time deltas, dispatches
@ 0x0067AFE0 void __cdecl MgRenderer_DispatchCommandBuffer();
    // 16356 hits — iterates 0x20-byte command entries, calls via function pointers
    // InterlockedExchange on 0xCD8168 for thread sync when entry+0x14 == 1
@ 0x006A4E6A void __cdecl MgRendererWin32_MultiPassDraw();
    // Multi-pass draw wrapper: sets render targets, applies per-pass stencil state,
    // loops passes, checks D3DERR_DEVICELOST (0x88760868) via D3D query
@ 0x006A413E void __cdecl MgRendererWin32_DrawExecute();
    // Primary draw execution: tests dirty flags in +0xAA8, applies state per flag,
    // computes WVP matrices, manages RT/depth/viewport, calls per-item iteration
    // Dirty flag → D3D state mapping documented in MgRendererWin32 struct
@ 0x0048EB18 bool __stdcall MgVisibilityTest(void* frustum, void* boundingVolume);
    // PVS/visibility table lookup: returns true = culled, false = visible
    // Thunk to 0x444A7D. PATCHED: always returns false (culling disabled).
@ 0x444A7D bool __stdcall MgVisibilityTest_Body(void* frustum, void* boundingVolume);
    // Real visibility function. Uses camera zone index from frustum+0xA8114,
    // resolves bounding volume to object index via 0x408E83,
    // checks state table entry[+8]==4 (4 = culled)
@ 0x0068E064 void MgVisibilityState_Update();
    // Transitions visibility state: 2→3 (visible) or 2→4 (culled)
    // via vtable[+0x1C] call (occlusion query or frustum test)
@ 0x006A3280 void __cdecl MgRendererWin32_IterateRenderItems_Culled();
    // Iterates render items (0x4C each from g_renderItemTable), performs PVS cull
    // via MgVisibilityTest against g_cameraFrustum, dispatches visible items to 0x006A0F6A
@ 0x006A333F void __cdecl MgRendererWin32_IterateRenderItems_NoCull();
    // Same iteration but skips frustum test, dispatches to 0x006A0C69
    // Only processes items with flag 0x2000 at +0x40
@ 0x006A0F1F void __cdecl MgRendererWin32_SelectPixelShader();
    // Extracts 11-bit PS index from item+0x30 bits[11:21], looks up in g_shaderResourceMgr,
    // binds via shader object vtable if different from cached value at +0xC50
@ 0x006A0C69 void __cdecl MgRendererWin32_PerItemSetup();
    // Per-item: VS constant upload, world matrix, pixel shader selection
@ 0x006A0F6A void __cdecl MgRendererWin32_PerItemSetupFull();
    // Full per-item: VDecl, textures, extra constants, then same as 0x006A0C69
@ 0x0069F6F6 void __cdecl MgRendererWin32_PerPassState();
    // Per-pass: tests dirty flags, uploads VS constants c38-c42, c247 (viewport),
    // applies render states via vtable+0x8C/+0x94, calls actual draw dispatch
@ 0x0069F35E void __cdecl MgRendererWin32_IssueDraw();
    // Calls DrawPrimitive (+0x144) or DrawIndexedPrimitive (+0x148) via device vtable
@ 0x0069EDBE void __cdecl MgRendererWin32_PreDrawSetup_Full();
    // Full pre-draw: multi-stream VBs, instancing, IB, vdecl, shader bind, material, draw
    // Instance data on streams 1+2, instance blend weights via SetVSConstantF
    // Caches current VS at [this+0xC28], PS at [this+0xC2C]
    // Tracks draw count at [this+0xC20], prim count at [this+0xC24]
@ 0x0069EF75 void __cdecl MgRendererWin32_PreDrawSetup_Simple();
    // Simplified path: skips multi-stream instancing, direct to IB/vdecl/shader/draw
@ 0x0069E874 void __cdecl MgRendererWin32_FlushCallbackQueue();
    // Iterates array at [this+0x15C94], count at [this+0x15C98], calls destructors
@ 0x0069E8A8 void __cdecl MgRendererWin32_EndFrame();
    // End-of-frame: execute callbacks from [this+0x15CAC], release shaders/state,
    // flush deferred texture release queue (g_deferredReleaseQueue)
@ 0x0069E871 void __cdecl MgRendererWin32_DrawOrchestrator();
    // Coordinates the per-object draw pipeline
@ 0x00444AAD void __cdecl MgGame_RenderObject();
    // Game-level render callback, invoked per visible object
@ 0x0041A008 void __cdecl MgGame_RenderObjectCallback();

/* ============================================================
 *  Key Functions — D3D9 Wrappers
 * ============================================================ */

@ 0x0068DE3E void __thiscall MgRendererWin32_SetStreamSource(MgRendererWin32* this);
@ 0x0068DB0E void __thiscall MgRendererWin32_SetIndices(MgRendererWin32* this);
@ 0x0068E01E void __thiscall MgRendererWin32_CreateVertexBuffer(MgRendererWin32* this);
@ 0x0068DCA9 void __thiscall MgRendererWin32_CreateIndexBuffer(MgRendererWin32* this);
@ 0x0068D7DA void __thiscall MgRendererWin32_CreateTexture(MgRendererWin32* this);
@ 0x0068D5DA void __thiscall MgRendererWin32_CreateTextureInner(MgRendererWin32* this);
@ 0x0068892D void __cdecl MgConstantStore_Flush();
    // Batched constant upload: VS via vtable+0x10, PS via vtable+0x14
@ 0x00688A67 void __cdecl MgConstantStore_SetVSConstants();
@ 0x0068FC1D void __cdecl MgRendererWin32_AllocBuffer();
@ 0x0068FC40 void __cdecl MgRendererWin32_AllocIndexBuffer();

/* ============================================================
 *  Key Functions — Resource Management
 * ============================================================ */

@ 0x0089E875 void __cdecl MgRenderer_CreateResources();
    // Resource creation path during frame setup
@ 0x00876A63 void __cdecl MgResourceManager_ProcessQueue();
@ 0x007E9F44 void __cdecl MgResourceManager_CreateBuffers();
@ 0x008761AC void __cdecl MgResourceManager_CreateTextures();

/* ============================================================
 *  Key Functions — Post-Processing / HDR Pipeline
 *  Shader names loaded at 0x0068A1xx-0x0068A2xx, 0x00688Fxx-0x006890xx
 * ============================================================ */

// HDR luminance + tone mapping:
@ 0x0068A1A9 void __cdecl MgFilter_LoadShader_HDR_DownsampleLumPass_hwfilter();
@ 0x0068A1CB void __cdecl MgFilter_LoadShader_HDR_DownsampleLumPass_swfilter();
@ 0x0068A1F4 void __cdecl MgFilter_LoadShader_HDR_Adapted_Lum();
@ 0x0068A21A void __cdecl MgFilter_LoadShader_HDR_Bright_Pass_hwfilter();
@ 0x0068A240 void __cdecl MgFilter_LoadShader_HDR_Bright_Pass_swfilter();
@ 0x0068A266 void __cdecl MgFilter_LoadShader_HDR_Blur();
@ 0x0068A28C void __cdecl MgFilter_LoadShader_HDR_Combine_Bloom();
@ 0x0068A2B2 void __cdecl MgFilter_LoadShader_ToneMap();
// Utility samplers:
@ 0x00688F68 void __cdecl MgFilter_LoadShader_SimpleSampler();
@ 0x00688F98 void __cdecl MgFilter_LoadShader_HDR_Filtered_Sampler();
@ 0x00688FC5 void __cdecl MgFilter_LoadShader_Downsample_4x4();
// Depth downsample (6 GPU-specific variants):
@ 0x00688FF2 void __cdecl MgFilter_LoadShader_DepthDS_NODEPTH();    // generic fallback
@ 0x0068901F void __cdecl MgFilter_LoadShader_DepthDS_DF24_FETCH1(); // ATI DF24 single fetch
@ 0x0068904C void __cdecl MgFilter_LoadShader_DepthDS_DF24_FETCH4(); // ATI DF24 quad fetch
@ 0x00689079 void __cdecl MgFilter_LoadShader_DepthDS_RAWZ();        // NVIDIA RAWZ
@ 0x006890A6 void __cdecl MgFilter_LoadShader_DepthDS_INTZ();        // NVIDIA INTZ
@ 0x006890D3 void __cdecl MgFilter_LoadShader_DepthDS_R32F();        // R32F float depth
@ 0x006A6337 void __cdecl MgRendererWin32_LoadShaderBin_nvidia();
    // Loads "Shaders_PC_nvidia.bin"
@ 0x006A634E void __cdecl MgRendererWin32_LoadShaderBin_ati();
    // Loads "Shaders_PC_ati.bin"
@ 0x006A6361 void __cdecl MgRendererWin32_LoadShaderBin_generic();
    // Loads "Shaders_PC_generic.bin"
@ 0x0068D7A1 void __cdecl MgRendererWin32_ProcessDeferredReleases();
    // Processes g_deferredReleaseQueue at end of frame

/* ============================================================
 *  Engine Thread / Task System
 *  Initialized at 0x0089EE40
 * ============================================================ */

@ 0x0089EE2E void __cdecl MgEngine_RegisterThreadTasks();
    // Registers all 5 thread tasks in g_taskTable (stride 0x250):
    //   Thread 0: "Main thread (rendering)"
    //     Init=0x0089EACC  Start=0x0089E5C5  Inner=0x0089E515
    //   Thread 1: "Physics manager thread"
    //     Init=0x0085B32D  Start=0x00859ABA  Inner=0x0085B0E4
    //   Thread 2: "Loading thread"
    //     Init=0x008A0736  Start=0x008A0731  Inner=0x008A1056
    //   Thread 3: "Input thread"
    //     Init=0x007DA274  Start=0x007D9C5B  Inner=0x007D9E2A
    //   Thread 4: "Game thread"
    //     Init=0x0089E75B  Start=0x0089E703  Inner=0x0089EB83

/* ============================================================
 *  IDirect3DDevice9 Vtable Offset Map
 *  (confirmed from decompiled wrapper functions)
 * ============================================================ */

// +0x2C    IDirect3DVertexBuffer9::Lock (on VB objects)
// +0x30    IDirect3DVertexBuffer9::Unlock
// +0x68    IDirect3DDevice9::CreateVertexBuffer
// +0x6C    IDirect3DDevice9::CreateIndexBuffer
// +0x144   IDirect3DDevice9::DrawPrimitive
// +0x148   IDirect3DDevice9::DrawIndexedPrimitive
// +0x15C   IDirect3DDevice9::SetVertexDeclaration
// +0x190   IDirect3DDevice9::SetStreamSource
// +0x198   IDirect3DDevice9::SetVertexShaderConstantF
// +0x1A0   IDirect3DDevice9::SetIndices
// +0xC2    (likely SetVertexShaderConstantF for instance constants — different call site)

/* ============================================================
 *  Vertex Shader Constant Register Map
 *  (from HLSL CTAB in captured shaders)
 * ============================================================ */

// c0-c177:     Bone matrices (3 regs per bone, max ~59 bones, 3x4 row-major)
//              Also used as per-instance transforms for instanced draws
// c178-c181:   g__worldMatrix      (float4x4, row-major)
// c182-c183:   g__texXfm0          (float2x4, row-major) — tex coord transform 0
// c184-c185:   g__texXfm1          (float2x4, row-major) — tex coord transform 1
// c186-c187:   g__texXfm2          (float2x4, row-major)
// c188-c189:   g__texXfm3          (float2x4, row-major) — tex coord transform 3
// c194-c195:   g__morphing          (float2x4, row-major) — morph target blending
// c196:        g__globalParms       [46194, 46.19, 8, 0.0081]
// c197:        g__mtlColor          (float4) — material color multiplier
// c198-c201:   g__zWorldMatrix      (float4x4) — combined WVP for z-prepass
// c202-c206:   (shadow/lighting auxiliary)
// c239-c242:   g__viewProjMatrix    (float4x4, row-major)
// c243:        g__cameraRight       — camera right vector (used by crowd billboard shader)
// c244:        g__cameraUp          — camera up vector (crowd billboards)
// c245:        g__cameraPos (VS)    — camera world position [x, y, z, 1]
// c246:        g__cameraClip        [near, far, fov(?), 0] — [0.1, 1500, 98, 0]
// c247:        g__viewPort          [x, y, width, height]  — [0, 0, 1024, 1024]
// c248:        g__invViewport       [2/w, -2/h, -1, 1]
// c249-c251:   (per-frame lighting/environment)
//
// --- Crowd billboard VS (special layout) ---
// c20:         g__crowdControl1     — crowd animation/LOD params
// c21:         g__crowdControl2
// c22:         g__crowdControl3
//
// --- Scaleform UI VS (completely different layout) ---
// c0-c1:       projection matrix    (dp4 pos.x/y)
// c2:          cxmul                — color transform multiply (GFx)
// c3:          cxadd                — color transform add (GFx)
// c4-c5:       texgen               — texture generation matrix (GFx)

/* ============================================================
 *  Pixel Shader Constant Register Map
 * ============================================================ */

// c0:          [1,1,1,1] — ones constant
// c1:          g__ambient            — ambient light color
// c2:          g__sunCol             — sun/directional light color
// c3:          g__sunDir             — sun direction (world space)
// c4:          [fogDist, 1, -1/fogDist, fogDensity]
// c5:          g__diffColor          — material diffuse color
// c6:          g__specColor          — specular (often [1,1,1,1])
// c7:          g__emissiveColor      — emissive color
// c8:          g__emissiveParms      — emissive parameters
// c10:         g__cameraPos          — camera world position
// c11:         g__cameraFront        — camera forward direction
// c20-c23:     g__shadowMatrices     (float4x4) — cascaded shadow map matrix
// c24-c27:     g__shadowScaleOffset  (4x float4) — CSM atlas scale/offset
// c28:         g__shadowDistances    — CSM cascade split distances
// c29:         [0.125, 0.375, 0.625, 0.875] — PCF dithering
// c30:         g__shadowMisc         — shadow quality params
// c31-c33:     (shadow filter/PCF params)
// c34-c37:     g__invViewProjMatrix  (float4x4) — c47 in some shaders
// c38:         g__viewPort (PS)      [0, 0, width, height]
// c39-c40:     screen-space transform helpers
// c41:         g__RT                 [width, height, 1/w, 1/h]
// c47-c50:     g__invViewProjMatrix  (alternate register in some shaders)
// c57:         g__waterFresnelParams
// c58:         g__waterRefractionParams
// c59-c60:     g__waterParams[2]
// c61:         g__deepWaterColor
// c62:         g__waterNormalScale
// c66:         g__straussParms       — Strauss specular params [smoothness, opacity, metalness, exponent]
// c67:         g__straussParms1      — Strauss secondary params (typically [0,0,0,0])
// c68:         g__hdrTexParms        — HDR encoding parameters
// c72:         g__ambientCubeMapColor — ambient from cubemap
// c81:         g__T0Extents          — post-process texture extents
// c82:         g__T1Extents          — secondary post-process texture extents
// c89:         [near, far, fov(?), 0]
// c180-c190:   (per-material params, varying per shader)
// c199:        g__globalParms (PS mirror)
// c200:        g__ambientOcclusionParms
// c201:        g__cloudCompUvOffset
// c203:        g__cloudCompWeight
// c204:        g__cloudCompParams
// g__dynamicLightingLights[8] (float3x4 * 8) — point/spot lights
//
// --- HDR/tone mapping PS constants (from outdoor2 + menu traces) ---
// g__HDR_Parms, g__HDR_AL_Parms       — HDR auto-exposure parameters
// g__HDR_Brightness, g__HDR_Contrast, g__HDR_Gamma — color grading
// g__HDR_BloomWeights, g__Bloom_Parms  — bloom mix weights
// g__TextureParms                      — general texture params
// g__bottomDomeColor                   — sky dome bottom hemisphere color
//
// --- Bool constants (SetPixelShaderConstantB) ---
// g__receiveShadows (bool)             — per-draw shadow receive toggle
// g__dynamicLightingBools[4] (bool)    — per-light enable toggle
// g__detailBools[2] (bool)             — detail texture enable toggle

/* ============================================================
 *  Texture Sampler Map
 * ============================================================ */

// s0:  g__albedoMap          — diffuse/albedo texture
//      (Scaleform: tex0)
// s1:  g__normalMap          — normal map
//      (Scaleform: tex1)
// s2:  g__specularMap        — specular map
// s3:  g__detailMap          — detail texture
// s4:  g__detailNormalMap    — detail normal map
// s5:  g__shadowMap          — cascaded shadow map atlas
// s6:  g__ambientMap         — ambient cubemap
// s7:  g__zMap / g__depthEncodeMap — depth buffer texture
// s8:  g__envMap             — environment cubemap (reflections)
// s9:  g__waterNormalMap1    — water normal animation layer 1
// s10: g__waterNormalMap2    — water normal layer 2
//      g__waterFoamMap       — water foam texture
// s11: g__backBufferMap / g__refractionMap — back buffer / refraction
//      g__waterNormalMap3    — water normal layer 3
// s12: g__terrain3Base       — terrain base layer texture
// s13: g__colorCurve         — color grading LUT
// s14: (sky/cloud/noise)
//
// HDR post-process samplers:
// g__hdr, g__sourceTex, g__t0 — source textures
// g__bloom0, g__bloom1, g__bloom2 — 3 bloom mip levels
//
// Debug: g__dbgVolumeTex (sampler3D), g__dbgCubeMap (samplerCUBE)

/* ============================================================
 *  Render Pipeline (per frame, from 3 trace captures)
 *
 *  Gameplay scene:    ~710-810 draws, 17-27 passes per frame
 *  Outdoor (busy):    ~710 draws, 27 passes per frame
 *  Menu:              ~32 draws, 9 passes per frame (no 3D scene)
 * ============================================================ */

// Pass 0:  Cloud composition        — 1 draw, fullscreen quad, cloud UV offset
// Pass 1:  Shadow map               — 213-217 draws, Z+STENCIL clear, depth-only
//          RT=backbuffer-sized, COLORWRITEENABLE=0
//          Uses g__zWorldMatrix (c198) for shadow-space WVP
// Pass 2:  Z-prepass                — 244-251 draws, Z+STENCIL clear, position-only verts
//          Bone matrices in c0-c177, COLORWRITEENABLE=0
// Pass 3:  Main scene (opaque)      — 210-311 draws, Z+STENCIL clear, full shading
//          18-36 unique VS, 16+ unique PS
//          Full lighting: sun + 8 dynamic lights + ambient cube + CSM shadows
//          Materials: Normal, Terrain, CharacterVariation, Variation
// Pass 4:  Volumetric resolve       — 1 draw, fullscreen quad
// Pass 5:  (auxiliary copy)         — 1 draw
// Pass 6:  Transparent/overlay      — 16 draws, alpha-blended, scene RT reused
// Pass 7:  Bloom threshold          — 1 draw, COLOR clear
// Passes 8-16: Post-processing chain (9 RTs, 1 draw each):
//          Bloom blur (H+V), tone mapping, fog composition,
//          depth-of-field, final color grading
//          Uses MgToneMapFilterWin32 for HDR→LDR
// Pass 17: Final composite          — 1 draw to backbuffer, 3 VS/3 PS
//          (possibly stereo/multi-view or debug overlays)

/* ============================================================
 *  Shader Source Files (from HLSL debug info)
 *  Source: d:\Projects\magellan\final_pc\code\libraries\mggraphics\win32\shaders\
 * ============================================================ */

// Mg_VP_Z_A_TexTransform.final            — Z-prepass with tex coord transform
// Mg_VP_VolumetricFinal_AD.final          — Volumetric lighting resolve
// Mg_VP_2D_T0_T1_Vd.final                — 2D/UI vertex program
// Mg_VP_VolumetricUVs_Morphable.final     — Volumetric with morph targets
// MgVP_Scaleform_StripVShader*.final      — 9 Scaleform UI vertex shader variants
// MgVP_Scaleform_GlyphVShader*.final      — Scaleform glyph rendering
// Mg_VP_Foliage_*.final                   — 3 foliage vertex shaders (instanced)
// Mg_FP_Foliage_*.final                   — 2 foliage pixel shaders
//
// --- Strauss specular PS variants (mesh + terrain) ---
// Strauss ADNS (full):  s0/s2/s3/s4/s5/s6/s8  — albedo+normal+spec+detailNorm+shadow+ambient+env
// Strauss ANS:          s0/s2/s3/s5/s6         — albedo+normal+spec+shadow+ambient (no detail normal, no env)
//                       442 DWORDs, most common opaque mesh PS in outdoor2 (38 bindings/frame)
//                       CTAB: g__straussParms(c66), g__straussParms1(c67), g__ambientOcclusionParms(c200)
// Both use the same lighting model; ANS drops the detail normal perturbation and env cubemap reflection.
//
// --- Post-process shaders ---
// MgFP_Downsample_4x4.final               — 16-tap box filter downsample (4×4 grid)
//                       598 DWORDs, CTAB: g__T0Extents(c81), g__SourceTex(s0)
//
// --- Terrain shaders ---
// Mg_VP_Lit_A_Vd_VNorm_WPos_VtxAtm.final             — Terrain base VS (albedo only)
// Mg_VP_Lit_AD_Vd_VtxAtm_WPos_VNorm.final            — Terrain detail VS (2 UV sets)
// Mg_VP_Lit_SL_ADN_Vd_Ao_VtxAtm_WPos_VNorm.final     — Terrain normal-mapped VS (3 UV sets, TBN)
// Mg_VP_strauss_SL_ADNS_Dn_Vd_Ao_VtxAtm_WPos_VNorm.final — Terrain full VS (5 UV sets, Strauss)
// Mg_VP_Lit_ADN_Ao_VtxAtm_WPos_VNorm.final           — Terrain variant: no COLOR0, AO-only
// Mg_FP_Lit_Constant_A_Vd_WPos_VtxAtm.final          — Terrain base PS (dome lighting)
// Mg_FP_Fx_A_Vd_WPos_Shdw_VNorm_VtxAtm.final        — Terrain detail PS (CSM shadows)
// Mg_FP_Lit_AN_Vd_Ao_WPos_Shdw_VNorm_VtxAtm.final   — Terrain normal-mapped PS (normal+AO+shadows)
// (Terrain full PS: Strauss specular ADNS — s0/s2/s3/s4/s5/s6/s8)
//
// (many more Mg_VP_* and Mg_FP_* in shader bins)

/* ============================================================
 *  Shader Bin Files
 * ============================================================ */

// Shaders_PC_nvidia.bin     — NVIDIA-specific shader pack
// Shaders_PC_ati.bin        — ATI/AMD-specific shader pack
// Shaders_PC_generic.bin    — Generic fallback shader pack
// Selected at runtime based on GPU vendor detection

/* ============================================================
 *  Draw Call Statistics (from 3 captures: gameplay, outdoor2, menu)
 * ============================================================ */

// --- Gameplay scene (original) ---
// Total draws/frame:     ~810     DIP: 795, DP: 15
// Z-prepass: 485, Opaque: 272, No-zwrite: 232, Alpha-tested: 26, Alpha-blended: 21
//
// --- Outdoor (busy scene) ---
// Total draws/frame:     ~710     DIP: 676, DP: 34
// Z-prepass: 475, Opaque: 178, No-zwrite: 238, Alpha-tested: 26
// Alpha-blended: 17 (SRCALPHA,INVSRCALPHA) + 4 (ONE,INVSRCALPHA) + 1 (SRCALPHA,ONE)
// Skinned draws: 52 (vs 28 in gameplay — more characters visible)
// Crowd billboard draws: 6 (g__crowdControl shader, camera-oriented sprites)
//
// --- Menu ---
// Total draws/frame:     ~32      DIP: 1, DP: 19
// No 3D scene: only post-process (7 RT blits) + Scaleform UI (11 draws with stencil)
// All draws: no-ztest + no-zwrite (pure 2D)
// Scaleform uses cxmul/cxadd color transforms, texgen matrix, stencil masking
//
// Blend modes observed across all captures:
//   SRCALPHA,INVSRCALPHA  — standard alpha blending
//   ONE,INVSRCALPHA       — premultiplied alpha (particles, Scaleform)
//   SRCALPHA,ONE          — additive alpha (effects, glow)
//
// 31% redundant state calls (samplers, streams, textures)

/* ============================================================
 *  Terrain Rendering System
 *
 *  Multi-layer alpha-blended terrain with 4 complexity tiers.
 *  Base terrain is opaque with z-write; detail layers blend on
 *  top with z-write OFF. All tiers share the z-prepass and
 *  shadow passes using position-only vertices.
 *
 *  Typical outdoor frame: ~12 terrain draws in main scene pass
 *  (2 base + 4 detail + 4 normal-mapped + 2 full specular).
 *
 *  Material class: MgMaterialTerrainWin32
 * ============================================================ */

// --- Terrain rendering order (within main scene pass 3) ---
//
// 1. Base terrain patches        — opaque, ZWRITE=ON, ALPHABLEND=OFF
//    Single albedo texture, hemisphere dome lighting (sky color blend)
//    VS: Mg_VP_Lit_A_Vd_VNorm_WPos_VtxAtm
//    PS: Mg_FP_Lit_Constant_A_Vd_WPos_VtxAtm
//
// 2. Detail terrain layers       — ZWRITE=OFF, ALPHABLEND=ON (SRCALPHA,INVSRCALPHA)
//    Albedo + vertex color blending, CSM shadows, ambient cube
//    VS: Mg_VP_Lit_AD_Vd_VtxAtm_WPos_VNorm
//    PS: Mg_FP_Fx_A_Vd_WPos_Shdw_VNorm_VtxAtm
//
// 3. Normal-mapped terrain       — ZWRITE=OFF, ALPHABLEND=ON
//    Albedo + normal map + AO, CSM shadows, ambient cube
//    VS: Mg_VP_Lit_SL_ADN_Vd_Ao_VtxAtm_WPos_VNorm
//    PS: Mg_FP_Lit_AN_Vd_Ao_WPos_Shdw_VNorm_VtxAtm
//
// 4. Full specular terrain       — ZWRITE=OFF, ALPHABLEND=ON
//    Albedo + normal + specular + detail normal + env cubemap
//    Strauss specular model
//    VS: Mg_VP_strauss_SL_ADNS_Dn_Vd_Ao_VtxAtm_WPos_VNorm
//    PS: Strauss ADNS pixel shader (s0/s2/s3/s4/s5/s6/s8)

// --- Terrain VS naming convention ---
// "Vd"     = vertex diffuse: COLOR0 modulated by g__mtlColor
// "Ao"     = ambient occlusion: COLOR1 passed to PS
// "VtxAtm" = vertex atmosphere/fog
// "WPos"   = world position output
// "VNorm"  = vertex normal output
// "SL"     = shadow + lighting (full shadow pipeline)
// "A/AD/ADN/ADNS" = texture complexity:
//    A    = albedo only
//    AD   = albedo + detail (2 UV sets)
//    ADN  = albedo + detail + normal (3 UV sets, tangent space)
//    ADNS = albedo + detail + normal + specular (5 UV sets, tangent space)

/* ============================================================
 *  Terrain Vertex Formats
 *
 *  Terrain identified by COLOR0 in vertex declaration.
 *  Standard meshes use COLOR1 (AO) without COLOR0.
 *  All terrain formats: CULLMODE=CW, except base tier (NONE).
 * ============================================================ */

// Tier 1 — Base terrain (opaque background)
// Stride: 28 bytes
struct MgTerrainVertex_Base {
    float    position[3];     // +0x00  FLOAT3   POSITION0
    uint32_t normal;          // +0x0C  D3DCOLOR NORMAL0   (packed: mad * 2 - 1)
    uint32_t color0;          // +0x10  D3DCOLOR COLOR0    (per-vertex tint/blend)
    float    texcoord0[2];    // +0x14  FLOAT2   TEXCOORD0 (albedo UV)
};
// VS: g__worldMatrix, g__mtlColor, g__viewProjMatrix
// PS: g__albedoMap(s0), g__topDomeColor(c189), g__bottomDomeColor(c190)
// Render state: ZWRITE=ON, ALPHABLEND=OFF, CULLMODE=CW

// Tier 2 — Detail layer (1 extra UV for detail texture)
// Stride: 28 bytes + Stream1(4B)
struct MgTerrainVertex_Detail {
    float    position[3];     // +0x00  FLOAT3   POSITION0
    uint32_t normal;          // +0x0C  D3DCOLOR NORMAL0
    uint32_t color0;          // +0x10  D3DCOLOR COLOR0    (layer blend weight)
    float    texcoord0[2];    // +0x14  FLOAT2   TEXCOORD0 (albedo UV)
    // Stream1: D3DCOLOR TEXCOORD5 (static lighting, stride 4)
    //          HDR = rgb * a * 31.875
};
// VS: g__worldMatrix, g__texXfm0, g__texXfm1, g__mtlColor, g__viewProjMatrix
// PS: g__albedoMap(s0), g__shadowMap(s5), g__ambientMap(s6)
//     + g__receiveShadows(b4), CSM shadow pipeline
// Render state: ZWRITE=OFF, ALPHABLEND=ON(SRCALPHA,INVSRCALPHA)
// g__mtlColor typically [0.53, 0.53, 0.53, 1] — darkened overlay

// Tier 3 — Normal-mapped (albedo + normal map + AO)
// Stride: 36 bytes + Stream1(4B)
struct MgTerrainVertex_NormalMapped {
    float    position[3];     // +0x00  FLOAT3   POSITION0
    uint32_t normal;          // +0x0C  D3DCOLOR NORMAL0
    uint32_t color0;          // +0x10  D3DCOLOR COLOR0    (layer blend weight)
    uint32_t color1;          // +0x14  D3DCOLOR COLOR1    (ambient occlusion)
    float    texcoord0[2];    // +0x18  FLOAT2   TEXCOORD0 (albedo UV)
    // TEXCOORD1 and TEXCOORD2 alias at same offset as TEXCOORD0 in decl,
    // but VS uses g__texXfm0/1/2 to compute distinct UVs
    uint32_t tangent;         // +0x20  D3DCOLOR TANGENT0  (packed tangent for TBN)
    // Stream1: D3DCOLOR TEXCOORD5 (static lighting, stride 4)
    //          HDR = rgb * a * 31.875
};
// VS: g__worldMatrix, g__texXfm0-2, g__mtlColor, g__viewProjMatrix
//     Builds TBN basis from normal + tangent, outputs 3 transformed UV sets
// PS: g__albedoMap(s0), g__normalMap(s2), g__shadowMap(s5), g__ambientMap(s6)
//     + g__ambientOcclusionParms(c200), CSM shadows
// Render state: ZWRITE=OFF, ALPHABLEND=ON

// Tier 4 — Full specular (Strauss model, 5 texture layers)
// Stride: 44 bytes + Stream1(4B)
struct MgTerrainVertex_Full {
    float    position[3];     // +0x00  FLOAT3   POSITION0
    uint32_t normal;          // +0x0C  D3DCOLOR NORMAL0
    uint32_t color0;          // +0x10  D3DCOLOR COLOR0    (layer blend weight)
    uint32_t color1;          // +0x14  D3DCOLOR COLOR1    (ambient occlusion)
    float    texcoord0[2];    // +0x18  FLOAT2   TEXCOORD0 (albedo UV)
    // TEXCOORD1-3 alias at same or adjacent offsets
    float    texcoord4[2];    // +0x20  FLOAT2   TEXCOORD4 (5th UV for detail normal)
    uint32_t tangent;         // +0x28  D3DCOLOR TANGENT0
    // Stream1: D3DCOLOR TEXCOORD5 (static lighting, stride 4)
    //          HDR = rgb * a * 31.875
};
// VS: g__worldMatrix, g__texXfm0-4, g__mtlColor, g__viewProjMatrix
//     5 UV transforms, TBN basis
// PS: g__albedoMap(s0), g__normalMap(s2), g__specularMap(s3),
//     g__detailNormalMap(s4), g__shadowMap(s5), g__ambientMap(s6),
//     g__envMap(s8)
//     + g__straussParms(c66), g__straussParms1(c67), g__hdrTexParms(c68)
//     + g__specColor(c6), g__ambientOcclusionParms(c200)
// Render state: ZWRITE=OFF, ALPHABLEND=ON

/* ============================================================
 *  Terrain Shader Register Map
 * ============================================================ */

// --- VS constants (terrain-specific) ---
// c178-c181:  g__worldMatrix      — identity for multi-layer terrain (world-space verts),
//                                   non-identity for some base terrain patches (local coords)
// c182-c191:  g__texXfm0-4        — 2x4 row-major UV transforms per layer
//             Tier 1: unused (direct UV passthrough)
//             Tier 2: texXfm0-1 (may have rotation+offset for albedo tiling)
//             Tier 3: texXfm0-2 (3 UV sets)
//             Tier 4: texXfm0-4 (5 UV sets, typically identity for world-space UVs)
// c197:       g__mtlColor         — material color multiplier, applied to COLOR0 in VS
//             Base: [1,1,1,1], Detail: [0.53,0.53,0.53,1] (darkened overlay)
//
// --- PS constants (terrain-specific beyond standard lighting) ---
// c189:       g__topDomeColor     — sky hemisphere top color (base terrain only)
//             .w = linear-vs-smooth dome blend factor (observed: 0.8)
//             HDR white [1,1,1] for sky
// c190:       g__bottomDomeColor  — sky hemisphere bottom color (base terrain only)
//             HDR ground bounce [3.84, 3.11, 2.29, 1] — warm, high-intensity
// c200:       g__ambientOcclusionParms — AO intensity/scale [1, 0.25, 1, 0.25]
//
// --- PS constants (base terrain dome lighting algorithm) ---
// c4:         g__sunFunc          — [sunPower, sunIntensity, sunThreshold, baseFog]
//             Observed: [500, 1, -0.005, 0.3]
//             Base terrain PS computes: sunFunc = intensity * pow(max(cosTheta - threshold, 0), power) + baseFog
//             Very sharp sun halo (power=500) for atmospheric scattering on distant patches

/* ============================================================
 *  Terrain Texture Binding
 * ============================================================ */

// Base terrain (Tier 1):
//   s0 = g__albedoMap      (single terrain texture)
//
// Detail terrain (Tier 2):
//   s0 = g__albedoMap      (terrain layer texture)
//   s5 = g__shadowMap      (CSM shadow atlas)
//   s6 = g__ambientMap     (ambient cubemap)
//
// Normal-mapped terrain (Tier 3):
//   s0 = g__albedoMap      (terrain layer albedo)
//   s2 = g__normalMap      (terrain normal map)
//   s5 = g__shadowMap
//   s6 = g__ambientMap
//
// Full terrain (Tier 4):
//   s0 = g__albedoMap      (terrain layer albedo)
//   s2 = g__normalMap      (terrain normal map)
//   s3 = g__specularMap    (specular intensity — often same texture as s0)
//   s4 = g__detailNormalMap (micro-detail normal)
//   s5 = g__shadowMap
//   s6 = g__ambientMap
//   s8 = g__envMap         (environment cubemap for specular reflection)
//
// --- Base terrain dome lighting (Mg_FP_Lit_Constant_A_Vd_WPos_VtxAtm) ---
// Atmospheric scattering for distant/LOD terrain patches:
//   viewVec = normalize(worldPos - cameraPos)
//   upTheta = asin(abs(viewVec.y))                    (polynomial approx)
//   domeLinear  = lerp(bottomDomeColor, topDomeColor, upTheta/PI)
//   domeSmooth  = sat(viewVec.y)*topDome + sat(-viewVec.y)*bottomDome
//   domeColor   = lerp(domeLinear, domeSmooth, topDomeColor.w)
//   sunHalo     = sunIntensity * pow(max(dot(viewVec,sunDir) - threshold, 0), sunPower) + baseFog
//   output      = domeColor * (diffuse * sunCol + sunHalo * sunCol)
// No shadows, no normal maps — purely view-dependent atmosphere tinting.

// Terrain sampler states (D3DSAMP enum key → value):
// s0-s4 (textures): trilinear, wrap          (MAG=LINEAR, MIN=LINEAR, MIP=LINEAR, ADDR=WRAP)
// s5 (shadow):      bilinear, border         (MAG=LINEAR, MIN=LINEAR, MIP=POINT, ADDR=BORDER)
// s6 (ambient):     trilinear, clamp         (MAG=LINEAR, MIN=LINEAR, MIP=LINEAR, ADDR=CLAMP)
// s8 (envMap):      point, clamp             (MAG=POINT, MIN=POINT, MIP=NONE, ADDR=CLAMP)

/* ============================================================
 *  Terrain Geometry & Pipeline Details
 * ============================================================ */

// Terrain patches are indexed triangle lists (DrawIndexedPrimitive).
// Typical patch sizes: 36-1266 vertices, 48-1899 triangles.
//
// Stream layout:
//   Stream0: per-vertex terrain data (stride varies by tier: 28/28/36/44)
//   Stream1: per-vertex static lighting (D3DCOLOR TEXCOORD5, stride 4)
//            Decoded in VS as: staticLight = rgb * a * 31.875
//            HDR-encoded: D3DCOLOR range [0,255] × alpha → up to ~31.875 intensity
//   Stream2: instancing data (stride 16)
//
// TEXCOORD aliasing: Tier 3/4 vertex declarations map multiple TEXCOORD
// semantics to the same vertex buffer offset (e.g. TEXCOORD0-2 all at +0x18).
// The VS applies g__texXfm0/1/2 to the shared base UVs, producing distinct
// UV sets per layer from a single stored UV pair.
//
// Normal encoding: D3DCOLOR packed, decoded in VS as: normal = vertex * 2 - 1
// Tangent encoding: D3DCOLOR packed, same decode + orthogonalization in VS
//   Tangent.w sign encodes bitangent handedness (cross product sign)
//   Bitangent = cross(normal, tangent) * tangent.w
//
// TBN interpolator packing (Tier 3/4):
//   o4 = [tangent.x,  binormal.x, normal.x, staticLight.r]
//   o5 = [tangent.y,  binormal.y, normal.y, staticLight.g]
//   o6 = [tangent.z,  binormal.z, normal.z, staticLight.b]
//   Each interpolator carries one TBN column + one lighting channel
//
// World matrix:
//   Multi-layer terrain: identity (vertices already in world space)
//   Base terrain patches: per-patch rotation + translation (local coords)
//
// Terrain draws are late in the main scene pass (after most opaque geometry),
// drawn in order: base opaque → blended detail → blended normal-mapped → blended full.
// All terrain layers share the same z-prepass geometry (POSITION-only VDecl).
