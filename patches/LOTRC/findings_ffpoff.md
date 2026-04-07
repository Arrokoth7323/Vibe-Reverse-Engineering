# Destructible Props Pipeline Analysis — FFP OFF (Working Case)

**Trace**: `dxtrace_20260407_022240.jsonl` (137,698 calls, 10 frames)
**Capture Conditions**: FFP conversion OFF, destructible scenery objects rendering correctly

---

## Summary

Destructible props (benches, tables, barrels, fence posts) in the working case use a **shader-based pipeline with morph target deformation**. The vertex shader applies 5 morph target matrices from c182-c191 before world/viewproj transformation. With FFP OFF, the original VS handles everything correctly. With FFP ON, the proxy replaces the VS, losing the morph deformation path entirely. Additionally, the proxy must correctly extract and decompose the world matrix (c178-c181) and ViewProj (c239-c242) into D3D fixed-function transform states.

There are **NO SetTransform calls** in the main render pass. All positioning is 100% VS-constant-driven.

---

## Key Addresses / Pointers

| Item | Value | Description |
|------|-------|-------------|
| VS (rigid+morph) | `0x2A161558` | Main pass rigid mesh with morph targets, stride 40 |
| VS (shadow morph) | `0x2A161C60` | Shadow/depth pass morph variant, stride 24 |
| PS (3-tex) | `0x3BB9ED50` | Albedo + normal + detail/specular |
| PS (1-tex) | `0x3BB9FC50` | Single-texture shadow/simple |
| VDecl (stride 40) | `0x0142C3E8` | Pos(12)+Normal(12)+UV(8)+Tangent(8) |
| VDecl (stride 24) | `0x2A1FC3C0` | Pos(12)+Normal_packed(4)+UV(8) (shadow) |
| VB (NV=76 mesh) | `0x2A1A0948` | Static VB, shared by all NV=76 instances |
| VB (NV=197 mesh) | `0x2A1A1A80` | Static VB, shared by all NV=197 instances |

---

## VS Constant Register Layout (Active During Prop Draws)

### Per-Object Constants (change per DIP)

| Register | Content | Notes |
|----------|---------|-------|
| c178-c181 | **World Matrix** (4x4 row-major) | Rotation + translation, unique per instance |
| c182-c191 | **Morph Target Matrices** (5 x float2x4) | Identity when undamaged; Z-shear when damaged |

### Per-Material Constants (change per material batch)

| Register | Content | Notes |
|----------|---------|-------|
| c197 | **Material Color/Tint** | `[1,1,1,1]` normal, `[0.8,1,0.992,1]` damaged |
| PS c5 | **Diffuse Color** | `[1,1,1,1]` or `[0.736,0.736,0.736,1]` |
| PS c6 | **Specular Color** | `[1,1,1,1]` typically |
| PS c8 | **Emission/Glow** | `[0,0,0,0]` typically |

### Per-Frame Constants (set once at frame start)

| Register | Content | Notes |
|----------|---------|-------|
| c196 | **Time** `[totalMs, totalSec, frame, dt]` | `[90248, 90.248, 41, 0.042]` |
| c198-c206 | **Atmosphere/Fog** (9 vec4s) | Sky color, fog density, scattering params |
| c239-c242 | **ViewProj Matrix** (4x4 row-major) | Camera VP |
| c243-c244 | **Camera Right/Up Vectors** | For billboard/facing calculations |
| c245 | **Camera Position** | `[-264.29, 4.03, -324.15, 1]` |
| c246 | **Near/Far/FOV** | `[0.1, 1500, 135.41, 0]` |
| c247-c248 | **Screen/Shadow Params** | `[0,0,3412,1389, 0.000586,-0.00144,-1,1]` |
| c249-c251 | **Ambient/Sun/Dir Light** | Per-frame lighting |

### NOT Used by Prop Draws

| Register | Content | Why Irrelevant |
|----------|---------|----------------|
| c0-c8 | Wind matrices (3x float3x4) | Foliage/tree wind system only |
| c9 | Wind blend weights | Foliage only |
| c23-c31 | Foliage instance data | Foliage only |
| c194-c195 | Wind/fog offsets | Updated per-pass, tiny values |

---

## Morph Target System (c182-c191)

### Register Layout

The 10 vec4s at c182-c191 encode **5 morph target matrices**, each stored as a float2x4 (2 rows):

```
Morph Target 0: c182 (row0), c183 (row1)
Morph Target 1: c184 (row0), c185 (row1)
Morph Target 2: c186 (row0), c187 (row1)
Morph Target 3: c188 (row0), c189 (row1)
Morph Target 4: c190 (row0), c191 (row1)
```

### Undamaged State (Identity)

All morph targets are identity — no deformation:
```
c182: [1, 0, 0, 0]  c183: [0, 1, 0, 0]
c184: [1, 0, 0, 0]  c185: [0, 1, 0, 0]
...repeated for all 5 targets
```

### Damaged State (Non-Identity, from shadow pass NV=168 objects)

Example from seq 9833 (heavily damaged):
```
Morph 0: c182=[1, 0,  0.625, 0]  c183=[0, 1, 0, 0]   <- +Z shear on X
Morph 1: c184=[1, 0, -0.723, 0]  c185=[0, 1, 0, 0]   <- -Z shear on X (opposing)
Morph 2-4: identity
```

Example from seq 9815 (lightly damaged):
```
Morph 0: c182=[1, 0, 0.121, 0]   c183=[0, 1, 0, 0]   <- slight Z shear
Morph 1-4: identity
```

The deformation is a **Z-axis shear** applied to the model, creating a "lean/tilt" effect. The VS applies these before the world transform. When an object is hit, the game populates successive morph targets with increasing/alternating shear angles.

### Morph Blend Register (c197)

c197 appears to serve as a material tint AND morph blend factor:
- `[1, 1, 1, 1]` = full white, no morph
- `[0.8, 1, 0.992, 1]` = partial damage tint (used alongside non-identity c182)

---

## Draw Call Patterns

### Main Render Pass — Undamaged Props

**First NV=76 draw** (seq 3732, line 3733):
```
SetStreamSource(0, VB=0x2A1A0948, stride=40)        <- static mesh VB
SetIndices(IB=0x2A1F4348)
SetVertexDeclaration(0x0142C3E8)                      <- Pos+Normal+UV+Tangent
SetVertexShader(0x2A161558)                            <- rigid+morph VS
SetPixelShader(0x3BB9ED50)                             <- 3-texture PS
SetTexture(Stage 0, albedo)
SetTexture(Stage 2, normal map)
SetTexture(Stage 4, detail/spec)
SetTexture(Stage 6, environment/lightmap 0x3538EA50)   <- shared across scene
SetVSConstantF(c178, world_matrix)                     <- per-instance position
SetVSConstantF(c182, identity x 10 vec4)               <- no deformation
SetPSConstantF(c5, diffuse+spec+emission)
DrawIndexedPrimitive(TriList, NV=76, PC=36)
```

### Rapid Instance Batching (Fence Posts)

For a row of identical NV=76 objects (seq 3955-3970):
```
[First draw gets full state setup as above]
For each subsequent instance:
  SetStreamSource(0, SAME VB, stride=40)              <- redundant but harmless
  SetVSConstantF(c178, new_world_matrix)               <- ONLY this changes
  DrawIndexedPrimitive(NV=76, PC=36)
```

World positions trace a path along a slight hill:
```
Instance 0: (-296.45,  9.81, -404.39)
Instance 1: (-296.21,  9.74, -401.40)
Instance 2: (-296.04,  9.66, -398.41)
Instance 3: (-295.92,  9.57, -395.41)
Instance 4: (-295.82,  9.47, -392.41)
Instance 5: (-295.73,  9.38, -389.42)
Instance 6: (-295.62,  9.30, -386.42)
```

Y heights: 9.81 down to 9.30 — consistent with terrain slope. Between instances, **nothing changes except c178**.

### NV=197 Props (Benches/Tables)

Same pattern as NV=76, different static VB (0x2A1A1A80). Also rapid-instance batched:
```
Instance 0: (-302.07,  7.97, -421.08) [seq 3774]
Instance 1: (-300.75,  8.42, -418.44) [seq 3777]
Instance 2: (-299.36,  9.09, -415.87) [seq 3780]
```

---

## SetTransform Usage (Frame 0)

| Line | State | Matrix | Context |
|------|-------|--------|---------|
| 156 | 256 (WORLD) | Identity | Early init |
| 13743 | 256 (WORLD) | Identity | Minimap pass |
| 13744 | 2 (VIEW) | Identity | Minimap pass |
| 13745 | 3 (PROJ) | Ortho projection | Minimap pass |
| 13754 | 256 (WORLD) | Identity | Camera view pass |
| 13755 | 2 (VIEW) | Camera view | Camera view pass |
| 13756 | 3 (PROJ) | Perspective | Camera view pass |

**SetTransform is only used for post-processing / UI passes** (seq 13743+), NOT for 3D scene rendering. All 3D geometry uses VS constants exclusively.

---

## Vertex Declaration Format (0x0142C3E8)

Stride 40 bytes. Inferred element layout:

| Offset | Size | Type | Semantic |
|--------|------|------|----------|
| 0 | 12 | FLOAT3 | POSITION |
| 12 | 12 | FLOAT3 | NORMAL |
| 24 | 8 | FLOAT2 | TEXCOORD0 |
| 32 | 8 | FLOAT2 | TANGENT (or TEXCOORD1) |

**No BLENDWEIGHT, no BLENDINDICES** — these are rigid meshes. Skinning detection must NOT trigger on these draws.

Single stream only (Stream 0). No per-instance Stream 1.

---

## VS Transformation Pipeline (Inferred)

The VS (0x2A161558) likely performs:

```hlsl
// 1. Apply morph deformation (c182-c191)
float4 pos = input.pos;
// Each morph target is a 2x4 shear/deform matrix
// Applied before world transform, blended by weights (c197?)

// 2. World transform (c178-c181)
float4 world_pos = mul(pos, g__worldMatrix);    // c178-c181

// 3. ViewProj transform (c239-c242)
float4 clip_pos = mul(world_pos, g__viewProj);  // c239-c242

// 4. Lighting/atmosphere using camera vectors (c243-c245)
// 5. Fog from atmosphere params (c198-c206)
```

---

## What FFP Conversion Must Preserve

For destructible props to render correctly under FFP:

1. **World Matrix (c178-c181)**: Must be correctly extracted as D3DTS_WORLD. The FFP proxy already does this for other rigid geometry.

2. **ViewProj Decomposition (c239-c242)**: Must be correctly split into D3DTS_VIEW and D3DTS_PROJECTION. The `apply_camera_transforms()` function handles this.

3. **Morph Deformation (c182-c191)**: This is **LOST** when the VS is replaced with FFP. FFP has no morph target support. For undamaged objects this is harmless (identity matrices). For damaged objects, the deformation disappears.

4. **Vertex Declaration Routing**: The stride 40, single-stream, no-blend-weight format should route through the rigid FFP path (not skinning). The VDecl has NO BLENDWEIGHT/BLENDINDICES.

5. **No Terrain Misidentification**: These props HAVE texcoords (UV at offset 24), so `cur_decl_has_texcoord()` returns true. They should NOT be excluded by terrain detection.

6. **No SpeedTree Misidentification**: No BINORMAL element. `cur_decl_has_binormal()` returns false. They should NOT be excluded by leaf/billboard detection.

---

## Potential FFP Height Bug Causes

If these props render at the **wrong height** with FFP ON:

1. **VP Decomposition Error**: If `apply_camera_transforms()` decomposes c239-c242 incorrectly, the VIEW or PROJ matrix could shift Y coordinates. Check if the same VP decomposition works for other rigid objects (e.g., buildings).

2. **Stale Transform State**: If terrain's `disengage()` leaves D3DTS_VIEW/PROJ in an incorrect state, subsequent FFP draws inherit wrong transforms. The terrain module sets identity VIEW/PROJ with decomposed camera — if those don't get properly restored for the next FFP draw, positions shift.

3. **c178 World Matrix Interpretation**: The world matrix for these props has non-trivial rotation (not axis-aligned). If the proxy assumes simpler matrices or truncates precision, small Y errors accumulate.

4. **Stream 1 Confusion**: Some objects use Stream 1 (stride 4, per-instance data). If the proxy incorrectly reads Stream 1 data for these single-stream props, it could corrupt position.

5. **Morph Matrix Side Effects**: Even though c182-c191 are identity for undamaged objects, the proxy might read them for a different purpose (e.g., confusing them with bone matrices) and apply incorrect transforms.

---

## Comparison: Working vs Potentially Broken Geometry Types

| Feature | NV=76/197 Props | Terrain | Foliage | Skinned |
|---------|----------------|---------|---------|---------|
| VS | 0x2A161558 | Terrain VS | Foliage VS | Skinned VS |
| Stride | 40 | 16 | 24 | 20/28 |
| Stream 1 | No | No | Yes (instance) | No |
| c178 World | Yes (per-inst) | No (world-space) | No (per-inst c0) | Yes |
| c182 Morph | Yes (identity) | No | No | No |
| TEXCOORD | Yes | No | Yes | Optional |
| BLENDWEIGHT | No | No | Yes | Yes |
| BINORMAL | No | No | Yes (leaves) | No |
| FFP routing | Rigid path | Excluded | Excluded | Skinning path |

Props should take the **rigid FFP path** — same as buildings and large static objects.

---

## Suggested Live Verification

1. **Breakpoint on c178 write** when NV=76 DIP fires: verify the proxy reads the correct world matrix
2. **Log D3DTS_WORLD** set by the proxy for a NV=76 draw: compare to the c178 values above
3. **Log D3DTS_VIEW/PROJ** during FFP prop draws: verify they match the decomposed c239-c242
4. **Check if terrain disengage() corrupts transforms**: disable terrain module and see if props fix
5. **Trace FFP engage/disengage** around a prop draw: ensure the proxy's state machine correctly identifies these as rigid FFP candidates
