1. Normal Unpacking (Unorm4x8 → FLOAT3)
The engine packs normals as 4-byte Unorm4x8 (same as D3DCOLOR layout). i managed to handle it this way:


static void UnpackNormal(uint32_t packed, float& nx, float& ny, float& nz)
{
    nx = ((packed        & 0xFF) / 127.5f) - 1.0f;
    ny = (((packed >>  8) & 0xFF) / 127.5f) - 1.0f;
    nz = (((packed >> 16) & 0xFF) / 127.5f) - 1.0f;
    // then normalize (rsqrtss + Newton-Raphson)
}
Key detail: byte order in memory is [R, G, B, A] = [nx, ny, nz, unused]. This is not BGRA-swizzled when read via raw memcpy but when D3D9 hardware reads a D3DDECLTYPE_D3DCOLOR element, it applies BGRA→RGBA swapping automatically. So:

If you're reading the vertex buffer bytes directly on CPU (like locking the VB), use the formula above — byte[0]=nx, byte[1]=ny, byte[2]=nz
If D3D9 FFP is reading it as D3DCOLOR type, it'll swizzle to BGRA order and you get wrong normals which is why your existing wrapper expands D3DCOLOR normals to FLOAT3
Foliage normals in this game are packed the same way. If Remix is reading raw FLOAT3 positions but seeing D3DCOLOR normals, the lighting will be wrong. You'd need the same NormExp expansion you already do for rigid geometry.

2. Vertex Format Bitfield Decoder (fmt1)
Every vertex buffer in the PAK files has a VBuffInfo header with a fmt1 field that encodes the complete vertex layout as bitflags. We decoded this from the game's Rust-based PAK parser and confirmed it in our C++ loader:


Process bits in this ORDER to compute byte offsets (order matters!):

Bit(s)           Attribute           Size     D3D Type
─────────────────────────────────────────────────────────────
fmt1 & 0x001     Position            12 bytes  FLOAT3
fmt1 & 0x400     BlendWeight          4 bytes  Unorm4x8
fmt1 & 0x800     BlendIndices         4 bytes  Unorm4x8
fmt1 & 0x002     Normal               4 bytes  Unorm4x8
fmt1 & 0x100     Color(0)             4 bytes  Unorm4x8
fmt1 & 0x200     Color(1)             4 bytes  Unorm4x8
(fmt1>>2)&0xF=N  TexCoord(0..N-1)     8 bytes each  FLOAT2
fmt1 & 0x040     Tangent              4 bytes  Unorm4x8
fmt1 & 0x080     PSize               12 bytes  FLOAT3
Common formats we've seen:

fmt1	Stride	Layout
0x00000247	32	Pos(12) + Normal(4) + Color1(4) + UV0(8) + Tangent(4) — most static meshes
0x00000347	36	Pos(12) + Normal(4) + Color0(4) + Color1(4) + UV0(8) + Tangent(4)
0x00000003	16	Pos(12) + Normal(4) — terrain
0x00000C47	40	Pos(12) + BlendWeight(4) + BlendIndices(4) + Normal(4) + Color1(4) + UV0(8) + Tangent(4) — skinned
Our C++ offset computation:


uint32_t normalByteOff = 12u;
if ((vbi.fmt1 & 0x400u) != 0u) normalByteOff += 4u;  // BlendWeight before normal
if ((vbi.fmt1 & 0x800u) != 0u) normalByteOff += 4u;  // BlendIndices before normal

uint32_t uvByteOff = normalByteOff;
if ((vbi.fmt1 & 0x002u) != 0u) uvByteOff += 4u;  // Normal
if ((vbi.fmt1 & 0x100u) != 0u) uvByteOff += 4u;  // Color(0)
if ((vbi.fmt1 & 0x200u) != 0u) uvByteOff += 4u;  // Color(1)
bool hasUV = (((vbi.fmt1 >> 2) & 0xFu) >= 1u) && (vStride >= uvByteOff + 8u);
Why this matters for foliage: If your foliage stream 0 or stream 1 vertex data comes from PAK files, fmt1 tells you exactly what's at each byte offset without guessing. If the foliage mesh has fmt1 & 0xC00 set, it has BlendWeight+BlendIndices (the wind weight and wind matrix index that the SpeedTree shaders read). That's the same data your fix is reading from TEXCOORD slots in stream 1 — but in the PAK file it might be laid out differently than what the game's vertex declaration presents at runtime.

3. TEXCOORD Abuse — Engine-Wide Pattern (Detailed)
This is probably the most important thing we can share. The Zero engine systematically abuses TEXCOORD semantics to pack arbitrary per-vertex data. The D3D semantic name is meaningless — you have to read the shader to know what each TEXCOORD actually contains.

We fully (not actually fully but partially) decoded the particle system's vertex layout, which uses 7 TEXCOORD slots where only one is actual UV:


struct PackedParticleVertex  // 76 bytes per vertex
{
    float px, py, pz;                    // POSITION — particle center
    float cr, cg, cb, ca;               // COLOR    — RGBA tint

    float rot, alphaMul, colorMul;       // TEXCOORD0 — rotation angle + modifiers (NOT UV)
    float u, v;                          // TEXCOORD1 — atlas UV (the ONLY real UV)
    float cornerIndex;                   // TEXCOORD2 — quad corner 0-3 (NOT UV)
    float halfW, halfH;                  // TEXCOORD3 — billboard half-dimensions (NOT UV)
    float pivotX, pivotY;                // TEXCOORD4 — pivot offset in world units (NOT UV)
    float sunScale;                      // TEXCOORD5 — directional light scale (NOT UV)
    float alphaRef;                      // TEXCOORD6 — alpha test threshold (NOT UV)
};
The D3D vertex declaration for this:


{ 0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // rot,alphaMul,colorMul
{ 0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },  // actual UV
{ 0, 48, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },  // cornerIndex
{ 0, 52, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },  // halfW,halfH
{ 0, 60, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },  // pivotX,pivotY
{ 0, 68, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },  // sunScale
{ 0, 72, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6 },  // alphaRef
This is the same pattern your foliage uses:

Your TEXCOORD1 = instance world position (not UV)
Your TEXCOORD2 = orientation angles (not UV)
Your TEXCOORD3.z = per-instance random rotation (not UV)
Confirmed from our SpeedTree shader disassembly too. The Branch/Frond/LeafCard/LeafMesh shaders declare inputs like:


dcl_position    v0   // local-space vertex position
dcl_normal      v1   // packed D3DCOLOR normal
dcl_blendindices v2  // .x = wind weight, .z = wind matrix index (NOT bone indices)
dcl_texcoord    v3   // UV set 0
dcl_texcoord1   v4   // UV set 1
dcl_tangent     v5   // packed tangent
And for LeafCard/LeafMesh:


dcl_color       v2   // .rgb = vertex color, .w = alpha ref LOD index
dcl_blendindices v3  // .x = wind weight, .z = wind matrix index
dcl_texcoord    v4   // actual UV
dcl_binormal    v5   // NOT a binormal — leaf card camera-space offset (x,y,z) OR leaf mesh pivot point
Even BINORMAL and BLENDINDICES are repurposed. The engine treats D3D semantic names purely as register binding slots.

Pros of knowing this pattern:
Predictability — every complex draw type in this game will abuse TEXCOORDs the same way. Once you identify a new geometry type (particles, effects, SpeedTree billboards), check the vertex declaration's TEXCOORD count. If it has 3+ TEXCOORDs, at least some are packing transform/control data, not UV.
FFP stripping becomes mechanical — for any geometry type you need to convert to FFP, you now know: find which TEXCOORD (if any) is the real UV, keep only that one in your stream-0 FFP declaration, and reconstruct everything else on CPU from the locked VB data.
Format cross-reference — our particle struct gives you a template for how the engine packs data. The types match what the HLSL shader expects: FLOAT3 for 3-component values, FLOAT2 for 2-component, FLOAT1 for scalars. No surprises.
Cons / Limitations from our side:
We decoded this offline, not at runtime. Our Scene3D viewer renders particles by building the PackedParticleVertex array on CPU and calling DrawPrimitiveUP — we never intercept the game's actual vertex declarations at runtime. So we know what the data should be, but we can't tell you what the game's D3D vertex declaration looks like for foliage at runtime — that's something only your wrapper can see.
We only decoded particles fully. For SpeedTree, we have the shader disassembly showing what each input register reads, but we haven't built a Scene3D renderer for SpeedTree — we skip animated vegetation. So the foliage-specific stream 1 instance layout (TEXCOORD1=position, TEXCOORD2=orientation, TEXCOORD3=rotation) is something your wrapper discovered at runtime that we haven't seen in our offline data.
The PAK vertex format (fmt1) and the runtime vertex declaration may differ. The game can reorganize vertex data into different streams or change attribute types between what's stored on disk and what's bound to D3D. Our fmt1 decoder tells you the on-disk layout; the runtime layout is what your wrapper intercepts.
Ground-cover foliage vs SpeedTree may be different systems. Our shader disassembly covers SpeedTree (Branch/Frond/LeafCard/LeafMesh/Billboard) which uses single-stream vertices + wind matrices in VS constants c0-c8. If your "foliage" is actually a separate ground-cover/grass system with hardware instancing on stream 1, that's a system we haven't encountered in the shaders we've disassembled. The TEXCOORD meanings in stream 1 would be specific to that system.
4. Rest-Pose Skin Baking (Breakable/Destructible Meshes)
We discovered that models with skin_binds_num > 0 need their rest-pose bone offset baked into vertex positions at load time. This was causing breakable buildings (and potentially breakable trees/foliage) to render at the wrong position.

The pipeline:


1. Read bone_transforms[bones_num] — local-space 4x4 per bone
2. Read bone_parents[bones_num] — parent index per bone (-1 = root)
3. Build bone hierarchy: boneWorld[i] = boneLocal[i] * boneWorld[parent[i]]
4. Read skin_binds[skin_binds_num] — inverse bind matrices
5. Read skin_order[skin_binds_num] — maps skin bind index → bone index
6. Compute: restSkinMat[i] = skinBind[i] * boneWorld[skinOrder[i]]
7. Per vertex: newPos = restSkinMat[blendIdx] * oldPos
The twist: many skinned meshes in this game don't have per-vertex BlendIndices in the vertex format (fmt1 flags 0x400/0x800 not set). Instead, each mesh part (BufferInfo) has a single bufSkinOff value at byte offset 272 of the BufferInfo struct. All vertices in that part use the same blend index = bufSkinOff.

Relevance to foliage: If destructible trees/props in the game use this system, they'd appear at the origin without this baking. Your FFP World matrix fix handles per-instance positioning, but if the mesh itself is pre-skinned, you'd get a double offset or the mesh geometry would be wrong. Check if your foliage models have skin_binds in their PAK data.

5. Level Rotation Detection
We found that collision geometry (and some building meshes) are stored in pre-rotation space in the PAK files. The level's global rotation isn't applied to the raw vertex data — it's expected to be applied at runtime.

Our fix: detect the rotation from the first _BL_ (building) instance that has zero translation:


for each instance:
    if (name contains "_BL_" && translation ≈ zero):
        m_levelRot[3x3] = instance.matrix.upper3x3
        break
Then apply m_levelRot to all collision vertices, shape translations, and capsule endpoints.

Relevance to foliage: If foliage instance positions in the PAK data are also in pre-rotation space, your CPU-built World matrices might place foliage at rotated-wrong positions. Helm's Deep has a 90° rotation, Isengard has -45°. If your foliage looks correct on some maps but rotated on others, this is likely why. The rotation matrix can be extracted from any building instance with zero translation — same approach would work in your wrapper if you can identify building instances.

Here's everything from our Scene3D codebase that can help them, with full source code and detailed explanations:

1. Normal Unpacking — How This Engine Packs Normals
Source: Scene3D/LevelScene.cpp:61-67


static void UnpackNormal(uint32_t packed, float& nx, float& ny, float& nz)
{
    nx = ((packed        & 0xFF) / 127.5f) - 1.0f;
    ny = (((packed >>  8) & 0xFF) / 127.5f) - 1.0f;
    nz = (((packed >> 16) & 0xFF) / 127.5f) - 1.0f;
    ZNormalize3f(nx, ny, nz);  // rsqrtss + Newton-Raphson renormalize
}
Source: Scene3D/ZeroMath.h:456-460


inline void ZNormalize3f(float& x, float& y, float& z)
{
    ZVec4 v(x, y, z, 0.0f);
    ZVec3Normalize(&v);
    x = v.x; y = v.y; z = v.z;
}
Detailed explanation:

The Pandemic Magellan engine stores vertex normals as a single 32-bit unsigned integer (Unorm4x8 / D3DCOLOR format) — 4 bytes packed into one uint32. Each byte maps to one axis:

Byte 0 (bits 0-7) = X component
Byte 1 (bits 8-15) = Y component
Byte 2 (bits 16-23) = Z component
Byte 3 (bits 24-31) = unused (usually 0xFF or 0x00)
The mapping from [0, 255] to [-1.0, +1.0] is: float = (byte / 127.5) - 1.0. This means byte 0 maps to -1.0, byte 127/128 maps to ~0.0, byte 255 maps to +1.0. After unpacking we renormalize because the quantization introduces small length errors.

Critical detail about byte order: When you lock a D3D9 vertex buffer and read raw bytes via memcpy, the bytes are in memory order: [R, G, B, A] = [nx, ny, nz, unused]. This is straightforward.

BUT — when D3D9 hardware reads a vertex element declared as D3DDECLTYPE_D3DCOLOR, it automatically applies BGRA-to-RGBA swizzling. So if the vertex declaration says the normal is type D3DCOLOR, the GPU sees the bytes shuffled: what was byte[0] (R/nx) ends up in the Blue channel, byte[2] (B/nz) ends up in Red. The game's vertex shaders are compiled with this swizzle in mind — they read the normal with a mad(v1.zxyw, 2, -1) or similar pattern to undo the hardware swizzle and scale simultaneously.

If you're replacing the game's vertex shader with FFP (which doesn't have this mad decode step), or if you're building a new vertex buffer on CPU for Remix to read, you need to either:

Expand D3DCOLOR normals to FLOAT3 in CPU code using the formula above (reading raw bytes, no BGRA issue)
Or change the vertex declaration type from D3DCOLOR to FLOAT3 and write decoded floats
The same packing is used for Tangent (fmt1 bit 0x040) — same formula, same byte order.

2. Vertex Format Bitfield Decoder (fmt1) — The Rosetta Stone
Source: Scene3D/LevelScene.cpp:978-1004


// ── Build LevelVertex array ───────────────────────────────────────────
// LOTRC static-mesh vertex layout decoded from VBuffInfo.fmt1:
//   byte  0-11: Position      (3 x float32)        fmt1 & 0x001
//   byte 12+  : BlendWeight   (Unorm4x8, 4 bytes)  fmt1 & 0x400  (skinned meshes)
//   byte   +  : BlendIndices  (Unorm4x8, 4 bytes)  fmt1 & 0x800  (skinned meshes)
//   byte   +  : Normal        (Unorm4x8, 4 bytes)  fmt1 & 0x002
//   byte   +  : Color(0)      (Unorm4x8, 4 bytes)  fmt1 & 0x100
//   byte   +  : Color(1)      (Unorm4x8, 4 bytes)  fmt1 & 0x200
//   byte   +  : UV0           (2 x float32)        (fmt1>>2)&0xF >= 1
//   byte   +  : Tangent       (Unorm4x8, 4 bytes)  fmt1 & 0x040  (after UV)
uint32_t normalByteOff = 12u;
if ((vbi.fmt1 & 0x400u) != 0u) normalByteOff += 4u;  // BlendWeight before normal
if ((vbi.fmt1 & 0x800u) != 0u) normalByteOff += 4u;  // BlendIndices before normal
// Color(0) offset: after normal (if present)
uint32_t colorByteOff = normalByteOff;
if ((vbi.fmt1 & 0x002u) != 0u) colorByteOff += 4u;   // Normal before color
bool hasColor = (vbi.fmt1 & 0x100u) != 0u && (vStride >= colorByteOff + 4u);
uint32_t uvByteOff = normalByteOff;
if ((vbi.fmt1 & 0x002u) != 0u) uvByteOff += 4u;  // Normal present
if ((vbi.fmt1 & 0x100u) != 0u) uvByteOff += 4u;  // Color(0) present
if ((vbi.fmt1 & 0x200u) != 0u) uvByteOff += 4u;  // Color(1) present
bool      hasUV  = (((vbi.fmt1 >> 2) & 0xFu) >= 1u) && (vStride >= uvByteOff + 8u);

uint32_t blendIdxByteOff = 12u;
if ((vbi.fmt1 & 0x400u) != 0u) blendIdxByteOff += 4u;
bool hasBlendIdx = (vbi.fmt1 & 0x800u) != 0u && hasSkinData
                && (blendIdxByteOff + 4u <= vStride);
Detailed explanation:

Every vertex buffer stored in the game's PAK/BIN files has a VBuffInfo header. Inside that header, there's a 32-bit field called fmt1 that encodes the complete vertex attribute layout as bitflags. This is the engine's own internal format — it's NOT a D3D FVF code, it's Pandemic's custom bitfield.

The bit table:

Bit Mask	Attribute	Data Type	Size	Notes
0x001	Position	3 x float32	12 bytes	Always first, always present
0x400	BlendWeight	Unorm4x8	4 bytes	Up to 4 bone weights packed
0x800	BlendIndices	Unorm4x8	4 bytes	Up to 4 bone indices packed
0x002	Normal	Unorm4x8	4 bytes	Packed, use UnpackNormal decode
0x100	Color(0)	Unorm4x8	4 bytes	Per-vertex AO / baked lighting
0x200	Color(1)	Unorm4x8	4 bytes	Secondary vertex color
(fmt1>>2)&0xF	UV count	2 x float32 each	8 bytes each	Number of UV channels (0-15)
0x040	Tangent	Unorm4x8	4 bytes	Packed same as normal
0x080	PSize	3 x float32	12 bytes	Particle size (rare)
The order is fixed — attributes appear in the vertex buffer in exactly the order listed above. You accumulate byte offsets top-to-bottom. Position always starts at byte 0. If BlendWeight is present (bit 0x400), it starts at byte 12. If both BlendWeight AND BlendIndices are present, Normal starts at byte 20. And so on.

Common fmt1 values we've encountered:

fmt1 = 0x00000247 (stride 32 bytes) — the most common, used for most static world meshes:


byte  0: Position  (3 x float32 = 12 bytes)      ← bit 0x001
byte 12: Normal    (Unorm4x8 = 4 bytes)           ← bit 0x002
byte 16: Color(1)  (Unorm4x8 = 4 bytes)           ← bit 0x200
byte 20: UV0       (2 x float32 = 8 bytes)        ← (0x247>>2)&0xF = 1 UV channel
byte 28: Tangent   (Unorm4x8 = 4 bytes)           ← bit 0x040
fmt1 = 0x00000347 (stride 36 bytes) — meshes with two vertex color channels:


byte  0: Position  (12)                            ← bit 0x001
byte 12: Normal    (4)                             ← bit 0x002
byte 16: Color(0)  (4)                             ← bit 0x100
byte 20: Color(1)  (4)                             ← bit 0x200
byte 24: UV0       (8)                             ← 1 UV channel
byte 32: Tangent   (4)                             ← bit 0x040
fmt1 = 0x00000003 (stride 16 bytes) — terrain, bare minimum:


byte  0: Position  (12)                            ← bit 0x001
byte 12: Normal    (4)                             ← bit 0x002
fmt1 = 0x00000C47 (stride 40 bytes) — skinned meshes (characters, breakable props):


byte  0: Position  (12)                            ← bit 0x001
byte 12: BlendWeight (4)                           ← bit 0x400
byte 16: BlendIndices (4)                          ← bit 0x800
byte 20: Normal    (4)                             ← bit 0x002
byte 24: Color(1)  (4)                             ← bit 0x200
byte 28: UV0       (8)                             ← 1 UV
byte 36: Tangent   (4)                             ← bit 0x040
Why this matters for foliage: If you lock a foliage vertex buffer and want to know where position, normal, or UV data lives, fmt1 tells you unambiguously. No guessing offsets. The BlendWeight/BlendIndices bits (0x400/0x800) are particularly important — in SpeedTree shaders, BLENDINDICES doesn't mean bone indices, it means wind weight (.x) and wind matrix index (.z). The same packed 4-byte field, completely different semantic meaning.

There's also a fmt1 & 0x40000 flag (bit 18) — if set, Position and Normal use 16-byte aligned Vector4 instead of Vector3/Unorm4x8. We haven't seen this on foliage meshes, but it exists in the format.

3. TEXCOORD Abuse Pattern — Engine-Wide Convention
Source: Scene3D/MgPackedParticleShaders.cpp:16-27


struct PackedParticleVertex
{
    float px, py, pz;       // POSITION.xyz (particle center)
    float cr, cg, cb, ca;   // COLOR.rgba
    float rot, alphaMul, colorMul; // TEXCOORD0.xyz — rotation angle + blend modifiers
    float u, v;             // TEXCOORD1.xy  — atlas UV (the ONLY real texture coordinate)
    float cornerIndex;      // TEXCOORD2.x   — quad corner ID (0,1,2,3)
    float halfW, halfH;     // TEXCOORD3.xy  — billboard half-width & half-height
    float pivotX, pivotY;   // TEXCOORD4.xy  — pivot offset in world units
    float sunScale;         // TEXCOORD5.x   — directional light intensity scale
    float alphaRef;         // TEXCOORD6.x   — alpha test threshold (0..1)
};
Source: Scene3D/MgPackedParticleShaders.cpp:160-171


D3DVERTEXELEMENT9 decl[] = {
    { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
    { 0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },  // rot, alphaMul, colorMul
    { 0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },  // actual UV
    { 0, 48, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },  // cornerIndex
    { 0, 52, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },  // halfW, halfH
    { 0, 60, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },  // pivotX, pivotY
    { 0, 68, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },  // sunScale
    { 0, 72, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6 },  // alphaRef
    D3DDECL_END()
};
Detailed explanation:

This is the single most important pattern to understand about how this engine works. The Pandemic Magellan engine treats D3D9 vertex declaration semantics as nothing more than register binding slots. The names TEXCOORD, BINORMAL, BLENDINDICES are completely meaningless — they're just a way to get data into specific vertex shader input registers.

In the particle system above, we have 7 TEXCOORD slots and only one of them (TEXCOORD1) is an actual texture coordinate. The rest pack:

TEXCOORD0: A rotation angle in radians, an alpha multiplier, and a color multiplier — three unrelated per-particle control values crammed into a FLOAT3
TEXCOORD2: A single float identifying which corner of the quad this vertex belongs to (0 = bottom-left, 1 = bottom-right, 2 = top-right, 3 = top-left). The vertex shader uses this to compute the actual billboard corner position from the center + camera vectors + dimensions
TEXCOORD3: The half-width and half-height of the billboard in world units — the VS multiplies these by camera right/up vectors to produce the actual corner offset
TEXCOORD4: The pivot point offset — particles in this engine can rotate around a non-center pivot (like sparks rotating from their base)
TEXCOORD5: A sun-facing brightness scale — the VS uses this to modulate vertex color based on sun direction dot product
TEXCOORD6: An alpha test reference value per-particle — different particles in the same batch can have different alpha thresholds
We decoded all of this by disassembling the game's compiled vertex shader Mg_VP_Particles_Billboard_Vd_A.vso and mapping each dcl_texcoord input register to how the shader math actually uses it. Then we reproduced this in our Scene3D particle renderer.

The same pattern applies to your foliage system:

Your TEXCOORD1 = instance world position (translation)
Your TEXCOORD2 = orientation angles for basis frame
Your TEXCOORD3.z = per-instance random Y rotation
These are NOT texture coordinates. They're per-instance transform parameters packed into TEXCOORD slots because the engine's vertex declaration builder just assigns incrementing TEXCOORD indices to every per-instance data field.

Broader implication: Any time you encounter a LOTR:Conquest vertex declaration with 3+ TEXCOORD elements, assume most of them are packing non-UV data. The only way to know what they actually contain is to read the vertex shader that consumes them. The D3D semantic name tells you nothing.

This also means: when you switch to FFP and strip away the vertex shader, you need to identify which TEXCOORD (if any) is the real UV and keep only that one bound. All the other TEXCOORDs need to be read on CPU and their logic reproduced in your wrapper code. For particles, TEXCOORD1 is the real UV. For foliage, you'd need to check the shader.

The vertex declaration data types matter too. Notice how the engine precisely matches the FLOAT type to the data:

3 values → FLOAT3 (TEXCOORD0: rot + alphaMul + colorMul)
2 values → FLOAT2 (TEXCOORD1: u,v / TEXCOORD3: halfW,halfH / TEXCOORD4: pivotX,pivotY)
1 value → FLOAT1 (TEXCOORD2: cornerIndex / TEXCOORD5: sunScale / TEXCOORD6: alphaRef)
This is consistent — the FLOAT type in the vertex declaration tells you how many components that TEXCOORD slot carries, even though it doesn't tell you what they mean.

4. Rest-Pose Skin Baking — Breakable/Destructible Mesh Transform
Source: Scene3D/LevelScene.cpp:774-825


// ── Read bone/skin data for breakable mesh rest-pose pre-skinning ─────────
bool hasSkinData = false;
std::vector<float> restSkinMats;
if (mdl.skin_binds_num > 0 && mdl.bones_num > 0
    && mdl.bone_transforms_offset != 0 && mdl.skin_binds_offset != 0
    && mdl.skin_order_offset != 0 && mdl.bone_parents_offset != 0)
{
    uint64_t btEnd = (uint64_t)mdl.bone_transforms_offset + (uint64_t)mdl.bones_num * 64u;
    uint64_t sbEnd = (uint64_t)mdl.skin_binds_offset + (uint64_t)mdl.skin_binds_num * 64u;
    uint64_t soEnd = (uint64_t)mdl.skin_order_offset + (uint64_t)mdl.skin_binds_num * 4u;
    uint64_t bpEnd = (uint64_t)mdl.bone_parents_offset + (uint64_t)mdl.bones_num * 4u;

    if (btEnd <= b1.size() && sbEnd <= b1.size() && soEnd <= b1.size() && bpEnd <= b1.size())
    {
        const float*    boneXforms  = reinterpret_cast<const float*>(&b1[mdl.bone_transforms_offset]);
        const float*    skinBinds   = reinterpret_cast<const float*>(&b1[mdl.skin_binds_offset]);
        const uint32_t* skinOrder   = reinterpret_cast<const uint32_t*>(&b1[mdl.skin_order_offset]);
        const int32_t*  boneParents = reinterpret_cast<const int32_t*>(&b1[mdl.bone_parents_offset]);

        // Step 1: Build bone world matrices by walking parent hierarchy
        std::vector<float> boneWorld(mdl.bones_num * 16);
        for (uint32_t b = 0; b < mdl.bones_num; ++b)
        {
            const float* local = &boneXforms[b * 16];
            int32_t par = boneParents[b];
            if (par < 0 || (uint32_t)par >= mdl.bones_num) {
                // Root bone: world = local
                memcpy(&boneWorld[b * 16], local, 64);
            } else {
                // Child bone: world = local * parent_world
                const float* pw = &boneWorld[(uint32_t)par * 16];
                float* w = &boneWorld[b * 16];
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        w[r*4+c] = local[r*4+0]*pw[0*4+c] + local[r*4+1]*pw[1*4+c]
                                  + local[r*4+2]*pw[2*4+c] + local[r*4+3]*pw[3*4+c];
            }
        }

        // Step 2: Compute rest skin matrices: skinBind[i] * boneWorld[skinOrder[i]]
        restSkinMats.resize(mdl.skin_binds_num * 16);
        hasSkinData = true;
        for (uint32_t i = 0; i < mdl.skin_binds_num; ++i)
        {
            uint32_t boneIdx = skinOrder[i];
            if (boneIdx >= mdl.bones_num) { hasSkinData = false; break; }

            const float* sb = &skinBinds[i * 16];   // inverse bind matrix
            const float* bw = &boneWorld[boneIdx * 16]; // world-space bone
            float* sm = &restSkinMats[i * 16];

            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    sm[r*4+c] = sb[r*4+0]*bw[0*4+c] + sb[r*4+1]*bw[1*4+c]
                              + sb[r*4+2]*bw[2*4+c] + sb[r*4+3]*bw[3*4+c];
        }
    }
}
Source: Scene3D/LevelScene.cpp:1017-1039 — Per-vertex application:


// Apply rest-pose skin offset for breakable/skinned meshes
if (hasSkinData)
{
    uint32_t blendIdx = bufSkinOff;  // default: per-part index from BufferInfo byte 272
    if (hasBlendIdx)
    {
        // Per-vertex blend indices exist — pick the bone with highest weight
        uint8_t bw4[4] = {255, 0, 0, 0};
        uint8_t bi4[4];
        if ((vbi.fmt1 & 0x400u) != 0u)
            memcpy(bw4, vp + 12, 4);            // BlendWeight at byte 12
        memcpy(bi4, vp + blendIdxByteOff, 4);   // BlendIndices
        int maxW = -1, maxK = 0;
        for (int k = 0; k < 4; ++k) {
            if ((int)bw4[k] > maxW) { maxW = (int)bw4[k]; maxK = k; }
        }
        blendIdx = (uint32_t)bi4[maxK] + bufSkinOff;
    }
    if (blendIdx < mdl.skin_binds_num) {
        const float* sm = &restSkinMats[blendIdx * 16];
        float ox = lv.x, oy = lv.y, oz = lv.z;
        lv.x = ox*sm[0] + oy*sm[4] + oz*sm[8]  + sm[12];
        lv.y = ox*sm[1] + oy*sm[5] + oz*sm[9]  + sm[13];
        lv.z = ox*sm[2] + oy*sm[6] + oz*sm[10] + sm[14];
    }
}
Detailed explanation:

This was one of the hardest bugs we solved. Breakable objects (buildings that can be destroyed, destructible props, potentially destructible trees) in LOTR:Conquest use a skinning system where the model's vertices are stored in bind pose (local to the bone), not in world-ready coordinates. At runtime, the game's vertex shader multiplies each vertex by its bone matrix from g__matrixPalette (VS constant registers c0-c176, 59 bones x 3 registers each). But if you're bypassing the vertex shader — which is exactly what your FFP approach does — you get vertices stuck at the origin in bind pose.

The fix is to bake the rest-pose bone transform into the vertex positions at load time (or at draw time on CPU). Here's how the data is organized in the PAK file's Block1:

Data structures in Block1 (all offsets from ModelInfo):

bone_transforms — Array of bones_num 4x4 float matrices (64 bytes each). Each is a local-space transform relative to the bone's parent. Row-major layout.

bone_parents — Array of bones_num int32 values. bone_parents[i] is the index of bone i's parent, or -1 for root bones. Used to build the hierarchy.

skin_binds — Array of skin_binds_num 4x4 float matrices (64 bytes each). These are inverse bind matrices — they transform from model space to bone space. Think of them as the "undo" of the bone's rest position.

skin_order — Array of skin_binds_num uint32 values. skin_order[i] maps skin bind index i to its corresponding bone index. This indirection exists because not every bone has a corresponding skin bind.

The algorithm:

Step 1: Walk the bone hierarchy bottom-up. For each bone, compute boneWorld[i] = boneLocal[i] * boneWorld[parent[i]]. Root bones (parent = -1) just copy their local transform as their world transform.

Step 2: For each skin bind, compute restSkinMat[i] = skinBind[i] * boneWorld[skinOrder[i]]. This gives you the complete transform from model-space vertex position to world-space rest-pose position.

Step 3: Per vertex, transform the position: newPos = restSkinMat[blendIdx] * oldPos.

The tricky part — bufSkinOff: Many skinned meshes in this game do NOT have per-vertex BlendIndices in the vertex format (fmt1 bits 0x400/0x800 are not set). Instead, each mesh part (BufferInfo struct) has a field at byte offset 272 called bufSkinOff. ALL vertices in that mesh part use the same blend index = bufSkinOff. This is an optimization — if an entire mesh part belongs to one bone, there's no need to store per-vertex indices.

When per-vertex BlendIndices DO exist, you read the 4-byte packed indices, read the 4-byte packed weights, find which bone has the highest weight, and use bi4[maxK] + bufSkinOff as the final index into the restSkinMats array.

Relevance to foliage: If any foliage models in the game have skin_binds_num > 0 (destructible trees, breakable vegetation), they'll suffer the same bind-pose-at-origin problem when you bypass the vertex shader. Your per-instance World matrix alone won't fix it — you'd get the instance positioned correctly but the mesh geometry itself would be in the wrong local space. You'd need to either bake the rest skin transform into the vertex data (like we do), or compose it with your per-instance World matrix.

5. Level Rotation Detection — Pre-Rotation Coordinate Space
Source: Scene3D/LevelScene.cpp:1578-1603


// ── Detect level building rotation from first _BL_ instance with zero translation ──
// Each level uses a different coordinate rotation for buildings (90° for Helm's Deep,
// -45° for Isengard, etc.).  Collision data shares this space.
// We detect it from the data — never hardcoded.
{
    memset(m_levelRot, 0, sizeof(m_levelRot));
    m_levelRot[0] = m_levelRot[4] = m_levelRot[8] = 1.0f; // default identity
    for (int ii = 0; ii < (int)m_instances.size(); ++ii)
    {
        const LevelInstance& inst = m_instances[ii];
        const std::string& mn = inst.meshName;
        // Look for _BL_ building meshes with zero/near-zero translation
        if (mn.find("_BL_") == std::string::npos) continue;
        float t2 = inst.mat[12]*inst.mat[12] + inst.mat[13]*inst.mat[13] + inst.mat[14]*inst.mat[14];
        if (t2 > 1.0f) continue; // skip buildings with non-zero translation
        // Extract 3x3 rotation
        m_levelRot[0]=inst.mat[0]; m_levelRot[1]=inst.mat[1]; m_levelRot[2]=inst.mat[2];
        m_levelRot[3]=inst.mat[4]; m_levelRot[4]=inst.mat[5]; m_levelRot[5]=inst.mat[6];
        m_levelRot[6]=inst.mat[8]; m_levelRot[7]=inst.mat[9]; m_levelRot[8]=inst.mat[10];
        if (dbg) fprintf(dbg, "LevelRot detected from '%s': [%.3f,%.3f,%.3f / %.3f,%.3f,%.3f / %.3f,%.3f,%.3f]\n",
                        mn.c_str(), m_levelRot[0],m_levelRot[1],
Here's what our Scene3D codebase has that directly helps their foliage fix:

1. Normal Unpacking (LevelScene.cpp:61-66)
The engine packs normals into 4 bytes (Unorm4x8). We decoded the exact formula from reverse-engineering the game's FUN_004068d0:


static void UnpackNormal(uint32_t packed, float& nx, float& ny, float& nz)
{
    nx = ((packed        & 0xFF) / 127.5f) - 1.0f;
    ny = (((packed >>  8) & 0xFF) / 127.5f) - 1.0f;
    nz = (((packed >> 16) & 0xFF) / 127.5f) - 1.0f;
    ZNormalize3f(nx, ny, nz);  // rsqrtss + Newton-Raphson renormalize
}
The 4th byte (alpha/W channel, bits 24-31) is unused for normals. Memory layout is [R, G, B, A] = [nx, ny, nz, unused] when read via raw CPU memcpy.

Critical for their case: When D3D9 hardware reads a D3DDECLTYPE_D3DCOLOR element through the vertex declaration, it silently swizzles BGRA→RGBA. So if their foliage vertex declaration tags the normal as D3DCOLOR type, the GPU sees byte[2]=R, byte[1]=G, byte[0]=B — the XYZ mapping flips. Their wrapper needs to either expand D3DCOLOR normals to FLOAT3 on CPU (like we do), or account for the swizzle. If they're stripping the vertex shader and going FFP, Remix will read whatever the declaration says, and D3DCOLOR normals will have swapped X/Z components unless expanded.

We re-normalize after decode because the 8-bit quantization introduces error — a unit normal packed to bytes and unpacked back won't have length 1.0 exactly. Our ZNormalize3f (ZeroMath.h:456) does SSE rsqrtss with a Newton-Raphson refinement step, matching how the game engine normalizes internally.

2. Vertex Format Bitfield Decoder (LevelScene.cpp:978-1004)
Every vertex buffer in the game's PAK files has a VBuffInfo header containing a fmt1 field. This single uint32 encodes the complete vertex attribute layout as bitflags. We decoded this by cross-referencing the game's Rust-based PAK parser (lotrc-0.6.0/src/pak/mod.rs: from_vertex_format()) with actual vertex buffer data:


// LOTRC static-mesh vertex layout decoded from VBuffInfo.fmt1:
//   byte  0-11: Position      (3 x float32)        fmt1 & 0x001
//   byte 12+  : BlendWeight   (Unorm4x8, 4 bytes)  fmt1 & 0x400  (skinned meshes)
//   byte   +  : BlendIndices  (Unorm4x8, 4 bytes)  fmt1 & 0x800  (skinned meshes)
//   byte   +  : Normal        (Unorm4x8, 4 bytes)  fmt1 & 0x002
//   byte   +  : Color(0)      (Unorm4x8, 4 bytes)  fmt1 & 0x100
//   byte   +  : Color(1)      (Unorm4x8, 4 bytes)  fmt1 & 0x200
//   byte   +  : UV0           (2 x float32)        (fmt1>>2)&0xF >= 1
//   byte   +  : Tangent       (Unorm4x8, 4 bytes)  fmt1 & 0x040  (after UV)

uint32_t normalByteOff = 12u;
if ((vbi.fmt1 & 0x400u) != 0u) normalByteOff += 4u;  // BlendWeight before normal
if ((vbi.fmt1 & 0x800u) != 0u) normalByteOff += 4u;  // BlendIndices before normal

uint32_t colorByteOff = normalByteOff;
if ((vbi.fmt1 & 0x002u) != 0u) colorByteOff += 4u;   // Normal before color
bool hasColor = (vbi.fmt1 & 0x100u) != 0u && (vStride >= colorByteOff + 4u);

uint32_t uvByteOff = normalByteOff;
if ((vbi.fmt1 & 0x002u) != 0u) uvByteOff += 4u;  // Normal present
if ((vbi.fmt1 & 0x100u) != 0u) uvByteOff += 4u;  // Color(0) present
if ((vbi.fmt1 & 0x200u) != 0u) uvByteOff += 4u;  // Color(1) present
bool hasUV = (((vbi.fmt1 >> 2) & 0xFu) >= 1u) && (vStride >= uvByteOff + 8u);

uint32_t blendIdxByteOff = 12u;
if ((vbi.fmt1 & 0x400u) != 0u) blendIdxByteOff += 4u;
bool hasBlendIdx = (vbi.fmt1 & 0x800u) != 0u && hasSkinData
                && (blendIdxByteOff + 4u <= vStride);
The bit ordering matters — attributes are packed in the order listed above. Position is always first at byte 0. BlendWeight comes before BlendIndices, which comes before Normal, which comes before Colors, which comes before UVs, which comes before Tangent. You accumulate offsets in that exact order.

The UV count is encoded in bits 2-5: (fmt1 >> 2) & 0xF. A value of 1 means one UV set (FLOAT2 = 8 bytes), 2 means two UV sets (16 bytes), etc.

Bit 0x040000 (b1 flag): If set, positions and normals use 16-byte aligned Vector4 instead of the compact layout above. We haven't encountered this in foliage meshes but it exists in the format.

Common fmt1 values we've catalogued:

fmt1	Stride	Layout	Usage
0x00000247	32	Pos(12) + Norm(4) + Color1(4) + UV0(8) + Tangent(4)	Most static world meshes
0x00000347	36	Pos(12) + Norm(4) + Color0(4) + Color1(4) + UV0(8) + Tangent(4)	Meshes with AO bake
0x00000003	16	Pos(12) + Norm(4)	Terrain (no UV, no color)
0x00000C47	40	Pos(12) + BW(4) + BI(4) + Norm(4) + Color1(4) + UV0(8) + Tangent(4)	Skinned/breakable
Why this matters for their foliage: If they need to parse foliage vertex data from PAK files to pre-process it, or if they need to understand what's at each byte offset in a locked vertex buffer, fmt1 tells them without guessing. The runtime vertex declaration the game creates at D3D level should match this layout, but the type mappings may differ (e.g., the game may declare Normal as D3DCOLOR type even though it's stored as raw bytes).

3. TEXCOORD Abuse — Engine-Wide Pattern (MgPackedParticleShaders.cpp:16-170)
This is the most directly relevant finding. The Pandemic Magellan engine systematically repurposes TEXCOORD semantics to pack arbitrary per-vertex data. The D3D semantic name tells you nothing — you must read the shader to know what each TEXCOORD actually contains.

We fully decoded and re-implemented the particle system's vertex layout in our Scene3D viewer. The particle vertex uses 7 TEXCOORD slots, and only one of them is actual UV:


struct PackedParticleVertex  // 76 bytes per vertex, single stream
{
    float px, py, pz;                    // POSITION.xyz — particle center in world space
    float cr, cg, cb, ca;               // COLOR.rgba   — per-particle tint

    float rot, alphaMul, colorMul;       // TEXCOORD0.xyz — rotation angle (radians),
                                         //   alpha multiplier, color multiplier
                                         //   NOT texture coordinates at all

    float u, v;                          // TEXCOORD1.xy  — atlas UV
                                         //   THE ONLY ACTUAL UV IN THE ENTIRE VERTEX

    float cornerIndex;                   // TEXCOORD2.x   — quad corner ID (0,1,2,3)
                                         //   used by VS to expand point → billboard quad

    float halfW, halfH;                  // TEXCOORD3.xy  — billboard half-width, half-height
                                         //   in world units, NOT texture coordinates

    float pivotX, pivotY;                // TEXCOORD4.xy  — pivot offset in world units
                                         //   billboard rotates around this point

    float sunScale;                      // TEXCOORD5.x   — directional light contribution
                                         //   scales sun lighting per-particle

    float alphaRef;                      // TEXCOORD6.x   — alpha test threshold (0..1)
                                         //   per-particle alpha cutoff for LOD
};
The D3D9 vertex declaration we create in Scene3D to match this:


D3DVERTEXELEMENT9 decl[] = {
    { 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
    { 0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, // rot,alphaMul,colorMul
    { 0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 }, // actual UV
    { 0, 48, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 }, // cornerIndex
    { 0, 52, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 }, // halfW,halfH
    { 0, 60, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 }, // pivotX,pivotY
    { 0, 68, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 }, // sunScale
    { 0, 72, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6 }, // alphaRef
    D3DDECL_END()
};
This directly parallels their foliage layout. Their foliage fix describes:

TEXCOORD1 = instance world position (not UV)
TEXCOORD2 = orientation angles (not UV)
TEXCOORD3.z = per-instance random rotation (not UV)
Same engine, same pattern. The Magellan engine treats D3D semantic names purely as register binding slots with zero semantic meaning. TEXCOORD, BINORMAL, BLENDINDICES — they're all just "give me a register to put data in."

Practical implication for any future geometry type they encounter: When a vertex declaration has 3+ TEXCOORD slots, assume most of them are NOT texture coordinates. The actual UV is usually TEXCOORD0 or TEXCOORD1 (check which one the pixel shader samples from). Everything else is transform data, control parameters, LOD indices, or animation inputs that the vertex shader consumes. When converting to FFP, they need to identify the one real UV and strip everything else.

4. Rest-Pose Skin Baking (LevelScene.cpp:774-1039)
We discovered that models with skin_binds_num > 0 store their vertex positions in bind-pose local space, not world space or even object space. Without baking the rest-pose bone transforms into the vertices at load time, these meshes render at the wrong location — typically collapsed near the origin or offset by the bone chain length.

The full pipeline from our Scene3D loader:


// ── Step 1: Read raw data from PAK Block1 ──
const float*    boneXforms  = &b1[mdl.bone_transforms_offset];  // local 4x4 per bone
const float*    skinBinds   = &b1[mdl.skin_binds_offset];       // inverse bind 4x4 per skin bind
const uint32_t* skinOrder   = &b1[mdl.skin_order_offset];       // skin bind → bone index mapping
const int32_t*  boneParents = &b1[mdl.bone_parents_offset];     // parent bone index (-1 = root)

// ── Step 2: Build bone world matrices (hierarchy walk) ──
std::vector<float> boneWorld(mdl.bones_num * 16);
for (uint32_t b = 0; b < mdl.bones_num; ++b)
{
    const float* local = &boneXforms[b * 16];
    int32_t par = boneParents[b];
    if (par < 0 || (uint32_t)par >= mdl.bones_num) {
        // Root bone: world = local
        memcpy(&boneWorld[b * 16], local, 64);
    } else {
        // Child bone: world = local * parent_world
        const float* pw = &boneWorld[(uint32_t)par * 16];
        float* w = &boneWorld[b * 16];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                w[r*4+c] = local[r*4+0]*pw[0*4+c] + local[r*4+1]*pw[1*4+c]
                          + local[r*4+2]*pw[2*4+c] + local[r*4+3]*pw[3*4+c];
    }
}

// ── Step 3: Compute final rest-pose skin matrices ──
// restSkinMat[i] = skinBind[i] * boneWorld[skinOrder[i]]
std::vector<float> restSkinMats(mdl.skin_binds_num * 16);
for (uint32_t i = 0; i < mdl.skin_binds_num; ++i)
{
    uint32_t boneIdx = skinOrder[i];
    const float* sb = &skinBinds[i * 16];   // inverse bind matrix
    const float* bw = &boneWorld[boneIdx * 16]; // bone world matrix
    float* sm = &restSkinMats[i * 16];

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            sm[r*4+c] = sb[r*4+0]*bw[0*4+c] + sb[r*4+1]*bw[1*4+c]
                      + sb[r*4+2]*bw[2*4+c] + sb[r*4+3]*bw[3*4+c];
}

// ── Step 4: Apply per-vertex at load time ──
// The twist: many skinned meshes DON'T have per-vertex BlendIndices
// (fmt1 flags 0x400/0x800 not set). Instead, each mesh part has a single
// bufSkinOff value at byte 272 of its BufferInfo struct — all vertices
// in that part use the same blend index.
uint32_t bufSkinOff = 0;
if (hasSkinData && hdr.buffer_info_size >= 276u)
    memcpy(&bufSkinOff, bi + 272, 4);  // per-part skin bind offset

// Per vertex:
uint32_t blendIdx = bufSkinOff;  // default: whole part uses same bone
if (hasBlendIdx) {
    // If vertex format HAS per-vertex BlendIndices, pick highest weight
    uint8_t bw4[4] = {255, 0, 0, 0};
    uint8_t bi4[4];
    if ((vbi.fmt1 & 0x400u) != 0u)
        memcpy(bw4, vp + 12, 4);  // BlendWeight
    memcpy(bi4, vp + blendIdxByteOff, 4);  // BlendIndices
    int maxW = -1, maxK = 0;
    for (int k = 0; k < 4; ++k) {
        if ((int)bw4[k] > maxW) { maxW = (int)bw4[k]; maxK = k; }
    }
    blendIdx = (uint32_t)bi4[maxK] + bufSkinOff;
}

if (blendIdx < mdl.skin_binds_num) {
    const float* sm = &restSkinMats[blendIdx * 16];
    float ox = lv.x, oy = lv.y, oz = lv.z;
    lv.x = ox*sm[0] + oy*sm[4] + oz*sm[8]  + sm[12];
    lv.y = ox*sm[1] + oy*sm[5] + oz*sm[9]  + sm[13];
    lv.z = ox*sm[2] + oy*sm[6] + oz*sm[10] + sm[14];
}
Why this matters for foliage: Destructible trees, breakable fences, and collapsible props in LOTR:C use this skinned system. The model has bones representing breakable segments. At rest, the pieces need to be in their assembled position — but the vertex data is in bind space. Without this bake, the geometry clusters at the origin. If their foliage models have skin_binds_num > 0 in the PAK data, their CPU-built World matrix alone won't position the mesh correctly — the vertices themselves need this pre-transform.

The bufSkinOff at BufferInfo byte 272 is the non-obvious part. We spent significant time debugging why some mesh parts rendered correctly and others didn't before finding this per-part offset. It acts as a base index added to whatever per-vertex BlendIndices says (or used alone when the vertex format has no BlendIndices at all).

5. Level Rotation Detection (LevelScene.cpp:1578-1603)
We discovered that certain level data is stored in pre-rotation space. The raw vertex/collision/instance data in the PAK assumes a base orientation, but each level applies a different global rotation at runtime. Helm's Deep is rotated 90°, Isengard is -45°, Minas Tirith has its own angle, etc.

Our detection method — completely data-driven, never hardcoded:


// Detect level building rotation from first _BL_ instance with zero translation.
// Each level uses a different coordinate rotation for buildings.
// Collision data shares this space.
memset(m_levelRot, 0, sizeof(m_levelRot));
m_levelRot[0] = m_levelRot[4] = m_levelRot[8] = 1.0f; // default identity

for (int ii = 0; ii < (int)m_instances.size(); ++ii)
{
    const LevelInstance& inst = m_instances[ii];
    const std::string& mn = inst.meshName;

    // Look for _BL_ building meshes with zero/near-zero translation
    if (mn.find("_BL_") == std::string::npos) continue;

    float t2 = inst.mat[12]*inst.mat[12]
             + inst.mat[13]*inst.mat[13]
             + inst.mat[14]*inst.mat[14];
    if (t2 > 1.0f) continue; // skip buildings with non-zero translation

    // Extract 3x3 rotation from instance transform matrix
    m_levelRot[0]=inst.mat[0]; m_levelRot[1]=inst.mat[1]; m_levelRot[2]=inst.mat[2];
    m_levelRot[3]=inst.mat[4]; m_levelRot[4]=inst.mat[5]; m_levelRot[5]=inst.mat[6];
    m_levelRot[6]=inst.mat[8]; m_levelRot[7]=inst.mat[9]; m_levelRot[8]=inst.mat[10];
    break; // first valid _BL_ is enough
}
The logic: _BL_ prefix identifies building meshes in the Magellan naming convention. A building instance with near-zero translation (length² < 1.0) is sitting at the level's origin — its rotation matrix IS the level's global rotation. We extract the upper 3x3 and apply it to all data that's in pre-rotation space (collision vertices, BVTree meshes, shape translations, capsule endpoints).

Why this matters for foliage: If foliage instance positions come from PAK instance data and that data is in pre-rotation space, their CPU-built World matrices would place foliage at correct-looking positions on some maps but rotated wrong on others. They might not notice on symmetrical maps, but Helm's Deep (90° off) or Isengard (-45° off) would be obviously broken — foliage floating over walls, embedded in terrain, etc. The fix is the same: extract the level rotation from a building instance and pre-multiply it into the foliage World matrices.

Summary Table
Finding	Scene3D Source	What it gives them
Normal decode formula	LevelScene.cpp:61-66	Exact byte→float conversion for packed normals, including the BGRA swizzle trap
fmt1 vertex bitfield	LevelScene.cpp:978-1004	Complete offline vertex layout for any mesh in the game
TEXCOORD abuse pattern	MgPackedParticleShaders.cpp:16-170	Proof that TEXCOORDs are engine-wide data packing slots, not UVs — with full 7-slot decoded example
Rest-pose skin baking	LevelScene.cpp:774-1039	Bone hierarchy walk + inverse bind + per-part bufSkinOff for breakable/destructible meshes
Level rotation detection	LevelScene.cpp:1578-1603	Data-driven global rotation extraction that affects all instance positioning

Here's what else from Scene3D is relevant for their proxy DLL work:

6. Particle Shader Constant Setup — Runtime Register Map
Source: Scene3D/MgPackedParticleShaders.cpp:313-349

This is us actually running the game's compiled particle vertex shader with the correct constant registers. This proves the register assignments work at runtime, not just in theory:


// Constants (from shader disassembly / CTAB):
//  VS: g__worldMatrix=c178..c181, g__viewProjMatrix=c239..c242,
//      g__cameraRight=c243, g__cameraUp=c244, g__cameraPos=c245

// Identity world matrix at c178-c181
D3DMATRIX world;
memset(&world, 0, sizeof(world));
world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1.0f;

// Combined ViewProj at c239-c242
D3DMATRIX viewProj;
device->GetTransform(D3DTS_VIEW, &view);
device->GetTransform(D3DTS_PROJECTION, &proj);
MulD3DMatrix(view, proj, viewProj);

device->SetVertexShaderConstantF(178, (const float*)&world, 4);      // g__worldMatrix
device->SetVertexShaderConstantF(239, (const float*)&viewProj, 4);   // g__viewProjMatrix

// Camera vectors for billboard facing
device->SetVertexShaderConstantF(243, cRight, 1);  // g__cameraRight
device->SetVertexShaderConstantF(244, cUp, 1);     // g__cameraUp
device->SetVertexShaderConstantF(245, cPos, 1);    // g__cameraPos

// Pixel shader: sun color at c2
float sunCol[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
device->SetPixelShaderConstantF(2, sunCol, 1);     // g__sunCol
Why this matters for them: This is a working, tested, runtime-verified example of feeding the Magellan engine's shader constants from CPU code. We load the game's actual compiled .vso and .pso bytecode files, set these exact registers, and the shaders render correctly. Their proxy intercepts these same SetVertexShaderConstantF calls — so they know for certain which registers carry which data. If they ever need to handle particles (or any other shader type), the register assignments are confirmed working at runtime, not just from static disassembly.

7. Particle Billboard Quad Building — CPU-Side Geometry Expansion
Source: Scene3D/MgPackedParticleShaders.cpp:419-567

This is our full CPU-side particle quad builder — we construct 6 vertices per particle (2 triangles) and submit via DrawPrimitiveUP. This is the same pattern their foliage fix uses ("one draw per instance") but for particles:


std::vector<PackedParticleVertex> verts;
verts.reserve(particleCount * 6);

for (int drawIdx = 0; drawIdx < particleCount; ++drawIdx)
{
    const Particle& p = emitter->particles[i];

    // ... UV atlas frame selection, pivot, rotation, color ...

    const float halfW = p.width * 0.5f;
    const float halfH = p.height * 0.5f;
    const float pivotOffX = (0.5f - pivotX) * p.width;
    const float pivotOffY = (0.5f - pivotY) * p.height;
    const float rot = p.rotation(1);

    // Corner indices: the VS reads this to know which corner of the
    // billboard quad this vertex represents, then expands it using
    // cameraRight/cameraUp vectors + halfW/halfH dimensions
    const float idxBL = 1.0f;   // bottom-left
    const float idxBR = 0.0f;   // bottom-right
    const float idxTR = 3.0f;   // top-right
    const float idxTL = 2.0f;   // top-left

    PackedParticleVertex vBL;
    vBL.px = p.position(0); vBL.py = p.position(1); vBL.pz = p.position(2);
    vBL.cr = cr; vBL.cg = cg; vBL.cb = cb; vBL.ca = ca;
    vBL.rot = rot; vBL.alphaMul = alphaMul; vBL.colorMul = colorMul;
    vBL.u = u0; vBL.v = v1_uv;
    vBL.cornerIndex = idxBL;
    vBL.halfW = halfW; vBL.halfH = halfH;
    vBL.pivotX = pivotOffX; vBL.pivotY = pivotOffY;
    vBL.sunScale = sunScale;
    vBL.alphaRef = alphaRef;

    // Other 3 corners clone from BL, just change UV and cornerIndex
    PackedParticleVertex vBR = vBL; vBR.u = u1; vBR.cornerIndex = idxBR;
    PackedParticleVertex vTR = vBL; vTR.u = u1; vTR.v = v0_uv; vTR.cornerIndex = idxTR;
    PackedParticleVertex vTL = vBL; vTL.v = v0_uv; vTL.cornerIndex = idxTL;

    // 2 triangles: BL-BR-TR, BL-TR-TL
    verts.push_back(vBL); verts.push_back(vBR); verts.push_back(vTR);
    verts.push_back(vBL); verts.push_back(vTR); verts.push_back(vTL);
}

device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, primCount, &verts[0], sizeof(PackedParticleVertex));
Why this matters for them: If they ever need to handle particles for Remix (which they will — particles are currently being dropped as alpha-blended overlays), this is a complete working implementation. The key insight is that the game's particle VS does billboard expansion on GPU using cornerIndex + cameraRight/cameraUp + halfW/halfH. For FFP conversion, they'd need to do that expansion on CPU instead — compute the actual 4 corner world positions and submit pre-expanded quads. Our code does the CPU-side UV atlas frame selection and per-particle property interpolation; they'd just need to add the billboard corner expansion step.

We also handle back-to-front sorting (DepthCmp at line 390-416) which is important for alpha-blended particles that Remix would need to see in correct order.

8. Terrain UV Generation — No-UV Meshes Need Position-Based Projection
Source: Scene3D/LevelScene.cpp:1094-1101


else
{
    // No UV channels in vertex format: generate world-space XZ tiling UV.
    // Terrain and similar meshes use position-based texture projection.
    // Blender mat scale = 0.004 -> tile every 250 world units.
    lv.u = lv.x * 0.004f;
    lv.v = lv.z * 0.004f;
}
Why this matters for them: Terrain meshes in LOTR:C have fmt1 = 0x00000003 — just Position + Normal, no UV at all. The game computes UVs in the pixel shader using PS constant registers c212-c213 (UV transform rows): u = dot(pos4, c212), v = dot(pos4, c213). If their proxy encounters a no-TEXCOORD mesh, it's terrain, and they need to either compute UVs on CPU from the position (like we do above) or capture c212/c213 from the game's PS constant writes and apply them. Without this, terrain renders untextured.

9. Render State Setup — What FFP Needs for Foliage
Source: Scene3D/MgPackedParticleShaders.cpp:280-307 (particle render states)
Source: Scene3D/LevelScene.cpp:2419-2536 (level geometry render states)

Our particle renderer shows the complete D3D9 state setup for alpha-blended geometry:


device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);       // foliage is double-sided
device->SetRenderState(D3DRS_LIGHTING, FALSE);
device->SetRenderState(D3DRS_ZENABLE, TRUE);
device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);          // alpha particles don't write depth
device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

// Sampler: linear filtering, clamp
device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

// Unbind higher texture stages
device->SetTexture(1, NULL);
device->SetTexture(2, NULL);
device->SetTexture(3, NULL);
Why this matters for foliage specifically: Foliage uses alpha test (not alpha blend) for leaf cutout — D3DRS_ALPHATESTENABLE = TRUE with D3DRS_ALPHAREF set from g__speedTreeAlphaRefs (VS constant c23). Their FFP foliage draw needs alpha test ON with the correct reference value, z-write ON (foliage is opaque behind the alpha cutout), and D3DCULL_NONE (leaves are rendered double-sided). If they use the particle blend states by mistake, foliage will be transparent and not write depth, causing rendering artifacts.

10. LevelVertex — Our Decoded Output Format
Source: Scene3D/LevelScene.h:35-44


// FVF: D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1  (36 bytes)
// Color holds per-vertex baked AO/lighting from the game's Color(0) channel.
struct LevelVertex
{
    float x, y, z;    // position
    float nx, ny, nz; // decoded normal (FLOAT3, already unpacked from Unorm4x8)
    uint32_t color;   // vertex color / ambient occlusion (D3DCOLOR ARGB)
    float u, v;       // texture coordinates
};
Source: Scene3D/LevelScene.cpp:317-322


D3DVERTEXELEMENT9 decl[] = {
    { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },
    { 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
    { 0, 28, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    D3DDECL_END()
};
Why this matters: This is what the game's vertex data looks like after we decode it into FFP-friendly format. Position as FLOAT3, normal expanded to FLOAT3 (not packed D3DCOLOR), one color channel, one UV set. This is essentially the target format for their proxy's vertex expansion — the simplest vertex layout that FFP and Remix can both understand. 36 bytes per vertex, clean standard attributes, no packed types, no TEXCOORD abuse.

That's everything from Scene3D that's relevant to their runtime proxy work. The core additions beyond what I covered before are:

#6: Proven runtime register assignments (not just static disassembly)
#7: Complete CPU-side billboard quad expansion (pattern for handling particles in Remix)
#8: Terrain has no UVs — needs position-based UV generation
#9: Correct render states for foliage (alpha test) vs particles (alpha blend)
#10: The target FFP vertex format after all decoding is done

Sure — here's what our DLL proxies add, with full source code:

11. D3D9 Proxy Architecture — Full IDirect3DDevice9 Wrapper
Source: Engine/DLL/D3D9Proxy/src/d3d9device.h:23-29


class ProxyIDirect3DDevice9 : public IDirect3DDevice9 {
private:
    IDirect3DDevice9* m_pReal;

public:
    ProxyIDirect3DDevice9(IDirect3DDevice9* pReal);
    IDirect3DDevice9* GetReal() { return m_pReal; }
Source: Engine/DLL/D3D9Proxy/src/d3d9device.cpp:1-18


#include "d3d9device.h"
#include "memorypatch.h"

bool g_WireframeEnabled = false;

// Frame counter for freeze detection - exported for watchdog
volatile DWORD g_FrameCounter = 0;

ProxyIDirect3DDevice9::ProxyIDirect3DDevice9(IDirect3DDevice9* pReal) : m_pReal(pReal) {}

// IUnknown
HRESULT __stdcall ProxyIDirect3DDevice9::QueryInterface(REFIID riid, void** ppvObj) { return m_pReal->QueryInterface(riid, ppvObj); }
ULONG __stdcall ProxyIDirect3DDevice9::AddRef() { return m_pReal->AddRef(); }
ULONG __stdcall ProxyIDirect3DDevice9::Release() {
    ULONG count = m_pReal->Release();
    if (count == 0) { Overlay_OnReset(); delete this; }
    return count;
}
Why this matters: This is a clean C++ IDirect3DDevice9 proxy pattern using COM inheritance. Every method passes through to the real device. The RTX group's wrapper uses a C vtable-thunking approach instead (raw function pointer arrays with ASM relay thunks). If they want to add new interception points, our pattern shows how to do it with proper C++ COM — every method is a one-liner passthrough that they can selectively add logic to. All 119 IDirect3DDevice9 methods are wrapped.

The key hook points where we inject behavior:

Source: Engine/DLL/D3D9Proxy/src/d3d9device.cpp:66-84


// BeginScene - set wireframe here!
HRESULT __stdcall ProxyIDirect3DDevice9::BeginScene() {
    HRESULT hr = m_pReal->BeginScene();
    Overlay_OnBeginScene(m_pReal);
    if (g_WireframeEnabled) {
        m_pReal->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    }
    return hr;
}

// EndScene - draw overlay here!
HRESULT __stdcall ProxyIDirect3DDevice9::EndScene() {
    g_FrameCounter++;  // Increment frame counter for freeze detection

    // Process danger state at end of each frame
    DangerOnEndOfFrame();

    m_pReal->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    Overlay_OnEndScene(m_pReal);
    return m_pReal->EndScene();
}
The passthrough draw calls — these are the exact methods the RTX group needs to intercept for foliage:


// Line 125-128 — currently passthrough, these are where foliage draw interception would go
HRESULT __stdcall ProxyIDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE Type, UINT StartVertex, UINT PrimitiveCount) 
    { return m_pReal->DrawPrimitive(Type, StartVertex, PrimitiveCount); }

HRESULT __stdcall ProxyIDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, INT BaseVertexIndex, 
    UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount) 
    { return m_pReal->DrawIndexedPrimitive(Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimitiveCount); }

HRESULT __stdcall ProxyIDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE Type, UINT PrimitiveCount, 
    const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) 
    { return m_pReal->DrawPrimitiveUP(Type, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride); }

HRESULT __stdcall ProxyIDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE Type, UINT MinVertexIndex, 
    UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, 
    const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) 
    { return m_pReal->DrawIndexedPrimitiveUP(Type, MinVertexIndex, NumVertices, PrimitiveCount, 
        pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride); }
And the shader/stream state tracking methods that are also passthrough but carry the data they'd need to capture:


// Line 131-147 — vertex declaration, shader constants, stream sources
HRESULT __stdcall ProxyIDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) 
    { return m_pReal->SetVertexDeclaration(pDecl); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pShader) 
    { return m_pReal->SetVertexShader(pShader); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) 
    { return m_pReal->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, 
    UINT OffsetInBytes, UINT Stride) 
    { return m_pReal->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber, UINT Setting) 
    { return m_pReal->SetStreamSourceFreq(StreamNumber, Setting); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pShader) 
    { return m_pReal->SetPixelShader(pShader); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) 
    { return m_pReal->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount); }

HRESULT __stdcall ProxyIDirect3DDevice9::SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) 
    { return m_pReal->SetTexture(Stage, pTexture); }
12. Havok Memory Pool Addresses + Crash Points
Source: Engine/DLL/D3D9Proxy/src/memorypatch.cpp:233-236


// Hard-coded pool addresses (must match game version)
static const DWORD POOL_BASE_PTR     = 0x00cd9970;
static const DWORD POOL_FREELIST_PTR = 0x00cd996c;
static const DWORD POOL_COUNT_PTR    = 0x00cd9974;
Source: Engine/DLL/D3D9Proxy/src/memorypatch.cpp:159-193


// Danger levels
enum DangerLevel {
    DANGER_NONE = 0,
    DANGER_LOW = 1,      // Minor anomalies detected
    DANGER_MEDIUM = 2,   // Significant pressure
    DANGER_HIGH = 3,     // Critical issues detected
    DANGER_CRITICAL = 4  // Imminent crash risk
};

// Danger state - global singleton
struct DangerState {
    volatile DWORD dangerLevel;
    volatile DWORD cooldownRemaining;
    volatile DWORD allocFailuresThisFrame;
    volatile DWORD allocSuccessesThisFrame;
    volatile DWORD totalAllocFailures;
    volatile DWORD totalAllocSuccesses;
    volatile DWORD lastPoolCount;
    volatile DWORD lastFreelistLength;
    volatile DWORD heapCorruptionCount;
    volatile DWORD consecutiveCleanFrames;
    volatile DWORD framesSinceLastCheck;
};

static DangerState g_DangerState = {0};
Source: Engine/DLL/D3D9Proxy/src/memorypatch.cpp:677-698


// Key RTTI addresses (from strings.txt analysis):
//   0x00A2C3A4: .?AVhkFreeListMemory@@
//   0x00A2E2F0: .?AVhkpWorld@@
//   0x00A2E47C: .?AVhkpEntity@@
//   0x00A2E4A8: .?AVhkpRigidBody@@

//   0x00993FE8: "No runtime block of size "
//   0x00994008: " currently available. Allocating new block from unmanaged memory."
//   0x0099404C: "Deallocating unmanaged big block."

//   hkBaseObject: +0x00 vtable

// Known Havok RTTI string addresses in the game binary
static const DWORD HAVOK_RTTI_hkFreeListMemory = 0x00A2C3A4;
static const DWORD HAVOK_RTTI_hkpWorld         = 0x00A2E2F0;
static const DWORD HAVOK_RTTI_hkpEntity        = 0x00A2E47C;
static const DWORD HAVOK_RTTI_hkpRigidBody     = 0x00A2E4A8;
Source: Engine/DLL/D3D9Proxy/src/memorypatch.cpp:510-547


// the crash at [EAX+0x40]. This identifies which member offset contains NULL.
//
// Target: FUN_008f7ec2 at 0x008F7EC2
// Crash: 0x008F7EC5 (offset +3) - reading [EAX+0x40] where EAX=0
// Goal: Find which [ECX+offset] loads the NULL value into EAX

// Original function pointer (set after patching)
static DWORD g_OriginalFunc_008f7ec2 = 0x008F7EC2;
Source: Engine/DLL/D3D9Proxy/src/memorypatch.cpp:614-630


// Key offsets for FUN_008f7ec2:
// +0x30 = array pointer, +0x40 = count
// Also log surrounding offsets for context
DWORD offsets[] = {0x04, 0x08, 0x0C, 0x10, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44};
for (int i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
    __try {
        DWORD memberVal = *(DWORD*)(esi + offsets[i]);
        // ... log member values
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // ... handle access violation
    }
}
Why this matters for RTX Remix: RTX Remix adds significant per-frame overhead — it intercepts every draw call, locks vertex buffers, captures geometry, rebuilds scene graphs. This engine already crashes under memory pressure from its Havok physics allocator. The specific failure mode:

Havok's hkFreeListMemory pool at 0x00cd9970 runs out of blocks during heavy combat
The allocator falls through to unmanaged memory ("No runtime block of size X currently available")
A few frames later, FUN_008f7ec2 reads a NULL entity pointer at offset +0x40, crashes
Root cause: physics entity pool exhaustion, not a rendering bug
With Remix adding overhead, this will trigger more often. We built the DangerLevel system to detect when the pool is stressed and throttle effect spawning to prevent the crash. If the RTX group sees random crashes during gameplay (especially during large battles on Pelennor Fields or Minas Tirith), this is almost certainly the cause — not their rendering code.

13. ConquestConsole — D3D9 VTable Hook Pattern
Source: Engine/DLL/ConquestConsole-main/ConquestConsole-main/src/d3d9hook.h:1-44


#pragma once

#include <Windows.h>
#include <d3d9.h>

namespace D3D9Hook {

    // Function pointer types for D3D9 methods
    using EndScene_t = HRESULT(__stdcall*)(IDirect3DDevice9*);
    using Reset_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using Present_t = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

    // Initialize the D3D9 hook
    bool Initialize();

    // Cleanup and restore original functions
    void Shutdown();

    // Get the hooked device (valid after first EndScene call)
    IDirect3DDevice9* GetDevice();

    // Check if hook is active
    bool IsInitialized();

    // Callback type for rendering
    using RenderCallback = void(*)(IDirect3DDevice9*);

    // Set the callback function for rendering
    void SetRenderCallback(RenderCallback callback);

    // Wireframe mode control
    void SetWireframe(bool enabled);
    bool GetWireframe();

    // Internal: Original function pointers (exposed for advanced use)
    extern EndScene_t OriginalEndScene;
    extern Reset_t OriginalReset;
}
Why this matters: This is a vtable hooking approach rather than a proxy DLL approach. It patches the IDirect3DDevice9 vtable in-memory after the game creates the device. This is a different hooking layer that could conflict with Remix's own DLL interception. If the RTX group's users also have the ConquestConsole mod installed (which many LOTR:C players do), the two hooks will chain. Their Remix wrapper replaces d3d9.dll, so the game loads their DLL first. Then ConquestConsole injects and patches the vtable of whatever device object exists — which could be the Remix wrapper's device, the real device, or the proxy device depending on load order. They should be aware this mod exists in the community and test with it loaded.

Summary — Updated with DLL Proxy Additions
#	Finding	Source	What it gives them
1	Normal unpack formula	Scene3D/LevelScene.cpp:61-66	Exact byte→float conversion + BGRA trap
2	fmt1 vertex bitfield	Scene3D/LevelScene.cpp:978-1004	Complete offline vertex layout decoder
3	TEXCOORD abuse pattern	Scene3D/MgPackedParticleShaders.cpp:16-170	7-slot decoded example proving engine-wide pattern
4	Rest-pose skin baking	Scene3D/LevelScene.cpp:774-1039	Bone hierarchy + inverse bind + bufSkinOff
5	Level rotation detection	Scene3D/LevelScene.cpp:1578-1603	Data-driven global rotation from _BL_ instances
6	Particle shader constants	Scene3D/MgPackedParticleShaders.cpp:313-349	Runtime-verified register assignments
7	Billboard quad builder	Scene3D/MgPackedParticleShaders.cpp:419-567	CPU-side geometry expansion + depth sort
8	Terrain UV generation	Scene3D/LevelScene.cpp:1094-1101	No-UV meshes need position-based projection
9	Foliage render states	Scene3D/MgPackedParticleShaders.cpp:280-307	Alpha test vs alpha blend state differences
10	LevelVertex output format	Scene3D/LevelScene.h:38-44	Target FFP-friendly vertex layout
11	D3D9 proxy pattern	Engine/DLL/D3D9Proxy/src/d3d9device.cpp	Full C++ COM wrapper with all 119 method passthrough
12	Havok crash points	Engine/DLL/D3D9Proxy/src/memorypatch.cpp	Pool addresses, RTTI, crash functions — stability under Remix overhead
13	VTable hook pattern	Engine/DLL/ConquestConsole-main/src/d3d9hook.h	Community mod hook that may chain-conflict with Remix