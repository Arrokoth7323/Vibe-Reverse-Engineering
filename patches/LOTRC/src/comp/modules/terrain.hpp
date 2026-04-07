#pragma once

namespace comp
{
	/*
	 * Terrain draw handler — shader passthrough with identity D3D transforms.
	 *
	 * Terrain vertices have Position + Normal but no UV coordinates. The terrain
	 * VS computes screen position from VS constants (c239-c242 ViewProj), and
	 * the PS composites 12 textures via c212/c213 UV projection.
	 *
	 * This module restores original shaders (via disengage) and sets identity
	 * D3D transforms before the draw. This clears stale FFP transforms that would
	 * confuse Remix's positioning, while preserving the full shader pipeline for
	 * terrain baker texture quality.
	 */
	class terrain
	{
	public:
		static HRESULT draw_terrain_dip(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
			UINT start_idx, UINT prim_count);

		static HRESULT draw_terrain_dp(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, UINT start_vtx, UINT prim_count);
	};
}
