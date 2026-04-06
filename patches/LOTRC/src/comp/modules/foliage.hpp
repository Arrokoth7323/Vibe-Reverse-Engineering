#pragma once

namespace comp
{
	/*
	 * Instanced foliage FFP conversion.
	 *
	 * Magellan engine foliage uses hardware instancing:
	 *   Stream 0 (INDEXEDDATA|N): per-vertex mesh (POSITION, COLOR, TEXCOORD0)
	 *   Stream 1 (INSTANCEDATA): per-instance (TEXCOORD1=worldPos, TEXCOORD2=orient, TEXCOORD3=wind)
	 *
	 * The vertex shader builds a basis frame from orient, applies Y-rotation from wind.z,
	 * and translates by worldPos. We replicate this on CPU as a per-instance World matrix.
	 */
	class foliage
	{
	public:
		// Draw instanced foliage with per-instance World matrix loop.
		// Returns S_OK on success, or falls through to shader path on failure.
		static HRESULT draw_instanced_dip(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
			UINT start_idx, UINT prim_count);

	private:
		static void build_world_matrix(const float* inst_pos, const float* orient,
			const float* wind, float* world);

		static IDirect3DVertexDeclaration9* s_ffp_decl;
		static void ensure_ffp_decl(IDirect3DDevice9* dev);
	};
}
