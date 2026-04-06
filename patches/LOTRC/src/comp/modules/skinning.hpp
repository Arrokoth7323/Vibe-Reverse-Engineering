#pragma once

namespace comp
{
	/*
	 * Skinning module for FFP conversion.
	 *
	 * CPU-skins vertices (bone blending on the CPU), outputs pre-transformed
	 * geometry to FFP. Hash stability comes from Remix config excluding positions
	 * from the asset hash (rtx.geometryAssetHashRuleString = "indices,geometrydescriptor").
	 */
	class skinning final : public shared::common::loader::component_module
	{
	public:
		skinning();
		~skinning();

		static inline skinning* p_this = nullptr;
		static skinning* get() { return p_this; }

		static bool is_available()
		{
			return p_this != nullptr;
		}

		// Called from renderer when cur_decl_is_skinned
		HRESULT draw_skinned_dip(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
			UINT start_idx, UINT prim_count);

		// Called on device reset
		void on_reset();

	private:
		// CPU-skinned vertex: pos(12) + normal(12) + uv(8) = 32 bytes
		static constexpr int SKIN_VTX_SIZE = 32;
		static constexpr int SKIN_CACHE_SIZE = 64;
		static constexpr int MAX_BONES = 59;

		IDirect3DVertexDeclaration9* skin_exp_decl_ = nullptr;

		// Expanded vertex buffer cache
		IDirect3DVertexBuffer9* skin_exp_vb_[SKIN_CACHE_SIZE] = {};
		unsigned int skin_exp_key_[SKIN_CACHE_SIZE] = {};
		unsigned int skin_exp_nv_[SKIN_CACHE_SIZE] = {};

		// Pre-computed bone_world matrices for current draw
		float bone_world_[MAX_BONES][16];
		int num_bones_ready_ = 0;
		bool is_crowd_inst_ = false;

		void create_expanded_decl(IDirect3DDevice9* dev);
		void release_cache();
		void prepare_bone_matrices();
		static void build_crowd_world(const float* tc5, float* world);

		// Vertex expansion with CPU skinning
		IDirect3DVertexBuffer9* get_skinned_vb(IDirect3DDevice9* dev,
			IDirect3DVertexBuffer9* src_vb, UINT stream_off, INT base_vtx,
			UINT min_vtx, UINT num_verts, UINT stride);
		void skin_vertex(float* dst, const unsigned char* src);

		// Format decoders
		static float half_to_float(unsigned short h);
		static void decode_normal(const unsigned char* src, int type, float* out);
	};
}
