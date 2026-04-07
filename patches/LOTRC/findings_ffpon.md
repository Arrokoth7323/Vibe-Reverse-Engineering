## Stride-40 Destructible Scenery Sinking -- dx9tracer JSONL Analysis

### Summary

The stride=40 destructible scenery draws (benches, tables, barrels) are routed through the **FFP rigid path** by the proxy. Code analysis of `renderer.cpp` confirms all draw routing conditions match: not skinned, not morphable, has NORMAL, has TEXCOORD, no BINORMAL, not water. The FFP conversion strips the original vertex shader, which the game uses to apply per-vertex position adjustments. Some of these draws have c178 world matrices with negative Y translations (e.g. Y=-0.657, placing the origin below ground); the original VS compensates using per-vertex data from secondary streams or TEXCOORD aliases, but FFP mode loses that compensation, causing objects to sink.

### Key Addresses

| Address / ID | Description |
|-------------|-------------|
| VS 0x2A161558 | Destructible scenery VS (Type A: single stream, 1 draw/frame) |
| VS 0x2A1624D0 | Destructible scenery VS (Type B: two streams, stride=40 + stride=4) |
| VS 0x2A160928 | Skinned stride=40 VS (Type C: bone palette c0-c113, NOT the target) |
| Decl 0x2A1FD4D0 | Type A vertex declaration (runtime addr, = user's 0x0142c3e8) |
| Decl 0x174885E8 | Type B vertex declaration (secondary per-vertex stream on stream 1) |
| Decl 0x2A1FC580 | Type C vertex declaration (skinned, bone palette present) |
| c178-c181 | World matrix (g__worldMatrix, row-major 4x4) |
| c239-c242 | ViewProj (decomposed by proxy into View + Projection) |
| c182-c191 | Secondary transforms (always identity for stride=40 draws) |

### dx9tracer Capture Limitations

**The dx9tracer does NOT capture D3D9 calls made by the proxy from within DIP/DP hook handlers.** This was confirmed by observing that:
- Terrain draws (which the proxy routes through `terrain.cpp` with `SetTransform(D3DTS_WORLD, identity)` and `apply_camera_transforms()`) show NO SetTransform calls in the trace
- SetTransform(D3DTS_WORLD) only appears at seq 155 (game init, identity) and seq 11072+ (proxy end-of-frame section)
- Proxy calls that happen OUTSIDE DIP hooks (e.g. the standalone FFP engage at seq 10747) ARE captured

This means the proxy's engage/disengage calls within `on_draw_indexed_prim` are invisible in the trace. The trace showing the original VS active for stride=40 draws does NOT mean the proxy bypasses FFP -- it means the game's SetVertexShader call is traced but the proxy's override inside the DIP hook is not.

### Frame Structure (Frame 0)

| Seq Range | Phase | Draws |
|-----------|-------|-------|
| 0-569 | Init + bulk c178/c239 writes | No draws |
| 570-2133 | Per-object c178 writes + more setup | No draws |
| 2134-2323 | Terrain draws (stride=16, no UV) | ~19 DIPs |
| 2324-2866 | Building structures (stride=28/32) | ~30 DIPs |
| **2867-2881** | **Stride=40 Type A (VS 0x2A161558)** | **1 DIP** |
| 2882-3003 | Building structures continued | ~5 DIPs |
| **3004-3019** | **Stride=40 Type B (VS 0x2A1624D0, 2 streams)** | **1 DIP** |
| 3020-4393 | Mixed structures + more Type B | ~20 DIPs |
| 4394-4680 | Skinned draws (Type C, bone palette) | ~15 DIPs |
| 4681-10729 | Remaining 3D draws, particles, effects | Many DIPs |
| 10730-10746 | Stencil setup + render state transition | No draws |
| 10747-10903 | **Proxy FFP engage #1** (VS=null, standalone) | No draws (overridden by game) |
| 10904-11030 | UI overlay pass (game's own shaders) | 2 DrawPrimitive |
| 11031 | EndScene | |
| 11034-11082 | **Proxy FFP engage #2** (terrain/minimap batch) | 4 DIPs (FVF=322, stride=24) |
| 11083-11088 | Disengage + Present | |

### Draw Routing Analysis

The proxy's `on_draw_indexed_prim` in `renderer.cpp` evaluates these paths in order:

1. **Instanced foliage** -- NO (not instanced)
2. **CPU skinning** -- NO (not skinned, no BLENDWEIGHT/BLENDINDICES)
3. **Skinned/morphable passthrough** -- NO (no POSITION1, not skinned)
4. **FFP rigid** -- **YES** (all conditions match: `!skinned && !pos_t && has_normal && !binormal && !water && has_texcoord`)
5. Terrain -- skipped (already matched #4)
6. Passthrough -- skipped (already matched #4)

The FFP rigid path (lines 181-193) calls:
```
ffp.engage(dev)          -> SetVS(null), SetPS(null), apply_transforms(), setup_texture_stages()
ffp.setup_albedo_texture -> SetTexture(0, albedo), NULL stages 1-7
dev->DrawIndexedPrimitive
ffp.restore_textures     -> restore all 8 stages
```

### c178 World Matrices for Stride=40 Draws

| Draw (seq) | VS | c178 Y Translation | Comment |
|-----------|-----|-------------------|---------|
| 2881 (Type A) | 0x2A161558 | **+10.36** | Building-level origin, reasonable |
| 3019 (Type B) | 0x2A1624D0 | **-0.657** | BELOW GROUND -- VS must compensate |
| 4424 (Type C) | 0x2A160928 | +0.998 | Near ground, skinned (has bone palette) |

The critical observation: **Type B draws have c178 with Y=-0.657 (below ground level)**. The game's VS (0x2A1624D0) reads the c178 world matrix AND per-vertex data from the secondary stream (stream 1, stride=4) to compute the final position. In FFP mode, stream 1 data and TEXCOORD alias data are not used for vertex position -- only POSITION[0] * D3DTS_WORLD * D3DTS_VIEW * D3DTS_PROJ. The per-vertex height compensation from the VS is lost.

### Type B Secondary Stream Detail

Type B stride=40 draws have two stream sources:
- Stream 0: stride=40 (position, normal, color, texcoords, tangent)
- Stream 1: stride=4, offset varies (e.g. offset 90880, 93236), VB 0x331C9240

The stride=4 secondary stream likely carries a per-vertex scalar (FLOAT1 or D3DCOLOR) used by the VS for:
- Height offset / vertical displacement
- Destruction state interpolation
- LOD blend factor

When FFP strips the VS, this per-vertex data is ignored and objects collapse to c178's raw position.

### Root Cause

The FFP rigid conversion applies: `final_pos = vertex_position * D3DTS_WORLD * D3DTS_VIEW * D3DTS_PROJ`

But the game's original VS computes: `final_pos = f(vertex_position, stream1_data, texcoord_aliases, c178, c239, c182...)`

For simple objects (Type A), `f()` may equal the FFP formula and sinking is minimal or absent. For Type B objects with secondary streams and per-vertex data, `f()` includes additional terms that FFP cannot replicate, causing sinking when the raw c178 origin is underground.

### Recommended Fix

Route stride=40 draws through the **shader passthrough with transform hints** path (same pattern as skinned/morphable draws and terrain), instead of FFP rigid. This preserves the original VS while giving Remix the D3D transform info it needs:

```cpp
// In on_draw_indexed_prim, add a new condition BEFORE the FFP rigid check:
//
// Destructible scenery: multi-element decl with TANGENT (numElems >= 10)
// or secondary stream present -- keep VS for position adjustments,
// provide Remix with transform hints.
else if (ffp.is_enabled() && ffp.view_proj_valid() &&
    !ffp.cur_decl_has_pos_t() &&
    !ffp.cur_decl_is_skinned() &&
    ffp.stream_stride(0) == 40)   // stride=40 destructible scenery
{
    ffp.disengage(dev);
    dev->SetTransform(D3DTS_WORLD,
        reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
    ffp.apply_camera_transforms(dev);
    hr = dev->DrawIndexedPrimitive(...);
    ffp.mark_transforms_dirty();
}
```

A better detection than `stride == 40` would be checking for TANGENT in the vertex declaration (since the target decl has D3DDECLUSAGE_TANGENT which the proxy currently ignores) or checking for the presence of a secondary stream (`stream_vb(1) != nullptr`).

### Suggested Live Verification

1. **Confirm sinking objects are stride=40**: Use `livetools trace` on the proxy's DIP hook, filter for stride=40, log c178.Y and stream1 presence
2. **Test shader passthrough**: Modify the FFP routing to exclude stride=40 draws and verify objects stop sinking
3. **Identify the per-vertex stream 1 data**: Read stream 1 VB content for a Type B draw to understand what the VS does with it
4. **Check if Type A draws (VS 0x2A161558) also sink**: They have reasonable c178.Y but might still lose VS-specific adjustments
