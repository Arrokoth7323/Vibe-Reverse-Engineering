#pragma once

namespace comp
{
	/*
	 * Spray billboard module for FFP conversion.
	 *
	 * CPU-expands billboard quads for 2D crowd sprite sheets (rotation-sliced
	 * atlas textures). Computes world-space billboard positions, viewing-angle
	 * rotation slice, and animation frame to produce simple Pos+Normal+Color+UV
	 * vertices that Remix can raytrace with correct atlas cell UVs.
	 */
	class spray final : public shared::common::loader::component_module
	{
	public:
		spray();
		~spray();

		static inline spray* p_this = nullptr;
		static spray* get() { return p_this; }

		static bool is_available()
		{
			return p_this != nullptr;
		}

		HRESULT draw_spray_dip(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
			UINT start_idx, UINT prim_count);

		HRESULT draw_spray_dp(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, UINT start_vtx, UINT prim_count);

		void on_reset();

	private:
		// Expanded vertex: pos(12) + normal(12) + color(4) + uv(8) = 36 bytes
		static constexpr int SPRAY_VTX_SIZE = 36;
		static constexpr int SPRAY_CACHE_SIZE = 8;

		IDirect3DVertexDeclaration9* spray_exp_decl_ = nullptr;

		// Expanded vertex buffer cache
		IDirect3DVertexBuffer9* spray_exp_vb_[SPRAY_CACHE_SIZE] = {};
		unsigned int spray_exp_key_[SPRAY_CACHE_SIZE] = {};
		unsigned int spray_exp_nv_[SPRAY_CACHE_SIZE] = {};

		// Corner expansion tables (from VS c182-c185)
		// Index 0-3: cornerSign.x (right), cornerSign.y (up), cornerUV.u, cornerUV.v
		static constexpr float CORNER_SIGN[4][2] = {
			{  1.0f, -1.0f },  // corner 0: right, down
			{ -1.0f, -1.0f },  // corner 1: left, down
			{ -1.0f,  1.0f },  // corner 2: left, up
			{  1.0f,  1.0f },  // corner 3: right, up
		};
		static constexpr float CORNER_UV[4][2] = {
			{ 0.0f, 1.0f },   // corner 0
			{ 1.0f, 1.0f },   // corner 1
			{ 1.0f, 0.0f },   // corner 2
			{ 0.0f, 0.0f },   // corner 3
		};

		void create_expanded_decl(IDirect3DDevice9* dev);
		void release_cache();

		IDirect3DVertexBuffer9* expand_spray_vb(IDirect3DDevice9* dev,
			IDirect3DVertexBuffer9* src_vb, UINT stream_off, INT base_vtx,
			UINT min_vtx, UINT num_verts, UINT stride);

		void expand_vertex(float* dst, const unsigned char* src,
			const float* world_mat, const float* cam_right, const float* cam_up,
			const float* cam_pos, const float* crowd_ctrl1, const float* crowd_ctrl2,
			const float* crowd_ctrl3);
	};
}
