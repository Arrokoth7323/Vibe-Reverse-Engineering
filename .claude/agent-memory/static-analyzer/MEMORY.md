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
