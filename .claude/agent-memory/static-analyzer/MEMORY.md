# Static Analyzer Memory

## LOTRC — VS Constant Register Map (Confirmed via dx9tracer)

- c0-c8: Wind matrices (3x float3x4) — foliage/tree only
- c9: Wind blend weights — foliage only
- c178-c181: World matrix (4x4 row-major) — per-object
- c182-c191: Morph target matrices (5x float2x4) — destructible deformation
- c194-c195: Wind/fog offsets (per-pass)
- c196: Time [totalMs, totalSec, frame, dt]
- c197: Material color/tint + morph blend factor
- c198-c206: Atmosphere/fog (9 vec4s, per-frame)
- c239-c242: ViewProj matrix (4x4 row-major)
- c243-c244: Camera right/up vectors
- c245: Camera position
- c246: Near/Far/FOV
- c247-c248: Screen/shadow params
- c249-c251: Ambient/sun/dir light

## LOTRC — Morph Target Deformation System

Destructible objects use c182-c191 for vertex morphing:
- 5 morph targets, each stored as float2x4 (2 rows per target)
- Identity when undamaged; Z-axis shear when damaged (lean/tilt)
- Example damaged: c182=[1,0,0.625,0] = X-axis shear in Z direction
- Applied by VS BEFORE world transform (c178)
- c197 may carry morph blend weights alongside material tint
- FFP conversion loses morph deformation entirely (no FFP equivalent)

## LOTRC — Key VS Pointers (from trace)

- 0x2A161558: Rigid mesh + morph (stride 40, main pass)
- 0x2A161C60: Shadow/depth morph variant (stride 24)
- 0x2A1624D0: Dual-texture objects (stride 32 + stream1)
- 0x2A161990: Single-texture rigid (stride 28)

## LOTRC — SetTransform Usage

SetTransform is ONLY used for post-processing/UI passes (minimap, HUD).
All 3D scene rendering uses VS constants exclusively — no D3DTS_WORLD/VIEW/PROJ.

## RTX Remix Bridge — remixapi_InitializeLibrary Internals (d3d9_remix.dll)

- Gate: `byte [base+0xC5E01]` = exposeRemixApi flag (offset +0x71 of config object at base+0xC5D90)
- Return 0xB (11) when flag==0, return 3 on invalid args, return 0 on success
- Config init: func 0x10043290 reads bridge.conf via readConfigBool (0x1001CA20), default=false
- Config sentinel: `byte [base+0xC7A36]` must be non-zero or accessor returns defaults
- On success: fills output struct with 21 API function pointers, sets byte [base+0xC65E0]=1
- remixapi_RegisterCallbacks (0x10059560): no gate check, stores 3 callbacks at base+0xC65E4/E8/EC
