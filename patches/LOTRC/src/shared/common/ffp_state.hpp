#pragma once
#include "config.hpp"

namespace shared::common
{
	/*
	 * FFP state tracker — captures D3D9 state needed for fixed-function pipeline conversion.
	 *
	 * The D3D9Device proxy calls on_xxx() methods when the game sets shaders, constants,
	 * textures, vertex declarations, etc. The renderer reads state via accessors to make
	 * draw routing decisions, then calls engage/disengage to switch between shader and FFP modes.
	 */
	class ffp_state
	{
	public:
		static ffp_state& get();

		void init(IDirect3DDevice9* real_device);

		// --- State mutators (called from D3D9Device interceptions) ---

		void on_set_vs_const_f(UINT start_reg, const float* data, UINT count);
		void on_set_ps_const_f(UINT start_reg, const float* data, UINT count);
		void on_set_vertex_shader(IDirect3DVertexShader9* shader);

		void on_set_pixel_shader(IDirect3DPixelShader9* shader);

		void on_set_texture(UINT stage, IDirect3DBaseTexture9* texture);
		void on_set_stream_source(UINT stream, IDirect3DVertexBuffer9* vb, UINT offset, UINT stride);
		void on_set_stream_source_freq(UINT stream, UINT freq);
		void on_set_vertex_declaration(IDirect3DVertexDeclaration9* decl);
		void on_set_fvf(DWORD fvf);
		void on_set_transform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix);
		void on_present();
		void on_begin_scene();
		void on_reset();

		// --- State consumers (called from renderer draw routing) ---

		void engage(IDirect3DDevice9* dev);
		void disengage(IDirect3DDevice9* dev);
		void reset_stale_transforms(IDirect3DDevice9* dev);

		// Set camera VIEW/PROJ from decomposed VP constants (c239-c242) without
		// touching WORLD or engaging FFP. For terrain: correct camera info for Remix
		// while keeping original shaders active.
		void apply_camera_transforms(IDirect3DDevice9* dev);

		// Bind albedo texture to stage 0, NULL stages 1-7. Call before draw.
		void setup_albedo_texture(IDirect3DDevice9* dev);

		// Restore original texture bindings on all 8 stages. Call after draw.
		void restore_textures(IDirect3DDevice9* dev);

		// --- Read-only accessors for renderer ---

		bool is_enabled() const { return enabled_; }
		void set_enabled(bool e) { enabled_ = e; }

		bool view_proj_valid() const { return view_proj_valid_; }
		bool is_ffp_active() const { return ffp_active_; }
		bool cur_decl_is_skinned() const { return cur_decl_is_skinned_; }
		bool cur_decl_has_normal() const { return cur_decl_has_normal_; }
		bool cur_decl_has_pos_t() const { return cur_decl_has_pos_t_ || fvf_pos_t_; }
		bool cur_decl_has_texcoord() const { return cur_decl_has_texcoord_; }
		bool cur_decl_has_color() const { return cur_decl_has_color_; }
		bool cur_decl_has_binormal() const { return cur_decl_has_binormal_; }
		bool cur_decl_has_tangent() const { return cur_decl_has_tangent_; }
		bool cur_decl_has_position1() const { return cur_decl_has_position1_; }
		bool cur_draw_is_water() const { return water_material_active_; }
		bool cur_decl_is_spray() const { return cur_decl_is_spray_; }
		int cur_decl_color_off() const { return cur_decl_color_off_; }
		int cur_decl_tc1_off() const { return cur_decl_tc1_off_; }
		int cur_decl_tc2_off() const { return cur_decl_tc2_off_; }
		int cur_decl_tc3_off() const { return cur_decl_tc3_off_; }
		bool cur_decl_has_texcoord5() const { return cur_decl_has_texcoord5_; }
		int cur_decl_texcoord_type() const { return cur_decl_texcoord_type_; }
		int cur_decl_texcoord5_off() const { return cur_decl_texcoord5_off_; }
		int cur_decl_texcoord5_type() const { return cur_decl_texcoord5_type_; }
		int cur_decl_texcoord5_stream() const { return cur_decl_texcoord5_stream_; }

		IDirect3DVertexShader9* last_vs() const { return last_vs_; }
		IDirect3DPixelShader9* last_ps() const { return last_ps_; }
		IDirect3DVertexDeclaration9* last_decl() const { return last_decl_; }

		// --- Diagnostic data access ---

		const float* vs_const_data() const { return vs_const_; }
		const float* ps_const_data() const { return ps_const_; }
		const int* vs_const_write_log() const { return vs_const_write_log_; }
		UINT draw_call_count() const { return draw_call_count_; }
		UINT frame_count() const { return frame_count_; }
		UINT scene_count() const { return scene_count_; }
		DWORD create_tick() const { return create_tick_; }

		// Texture tracking (stages 0-7)
		IDirect3DBaseTexture9* cur_texture(UINT stage) const { return stage < 8 ? cur_texture_[stage] : nullptr; }

		// Stream source tracking
		IDirect3DVertexBuffer9* stream_vb(UINT stream) const { return stream < 4 ? stream_vb_[stream] : nullptr; }
		UINT stream_offset(UINT stream) const { return stream < 4 ? stream_offset_[stream] : 0; }
		UINT stream_stride(UINT stream) const { return stream < 4 ? stream_stride_[stream] : 0; }
		UINT stream_freq(UINT stream) const { return stream < 4 ? stream_freq_[stream] : 1; }
		bool is_instanced() const { return (stream_freq_[0] & 0x40000000u) != 0; }
		UINT instance_count() const { return stream_freq_[0] & 0x3FFFFFFFu; }

		// Foliage declaration (instanced, stream 1 has TC1/TC2/TC3)
		bool cur_decl_is_foliage() const { return cur_decl_foliage_; }
		int cur_decl_foliage_tc1_off() const { return cur_decl_foliage_tc1_off_; }
		int cur_decl_foliage_tc2_off() const { return cur_decl_foliage_tc2_off_; }
		int cur_decl_foliage_tc3_off() const { return cur_decl_foliage_tc3_off_; }
		int cur_decl_foliage_stream() const { return cur_decl_foliage_stream_; }

		// VS constant register layout (game-specific, edit defaults when porting)
		int reg_view_start() const { return vs_reg_view_start_; }
		int reg_view_end() const { return vs_reg_view_end_; }
		int reg_proj_start() const { return vs_reg_proj_start_; }
		int reg_proj_end() const { return vs_reg_proj_end_; }
		int reg_viewproj_start() const { return vs_reg_viewproj_start_; }
		int reg_world_start() const { return vs_reg_world_start_; }
		int reg_world_end() const { return vs_reg_world_end_; }
		int reg_bone_threshold() const { return vs_reg_bone_threshold_; }
		int regs_per_bone() const { return vs_regs_per_bone_; }
		int bone_min_regs() const { return vs_bone_min_regs_; }

		// Skinning data (populated by skinning module via on_set_vs_const_f bone detection)
		int bone_start_reg() const { return bone_start_reg_; }
		int num_bones() const { return num_bones_; }
		int cur_decl_num_weights() const { return cur_decl_num_weights_; }
		int cur_decl_blend_weight_off() const { return cur_decl_blend_weight_off_; }
		int cur_decl_blend_weight_type() const { return cur_decl_blend_weight_type_; }
		int cur_decl_blend_indices_off() const { return cur_decl_blend_indices_off_; }
		int cur_decl_pos_off() const { return cur_decl_pos_off_; }
		int cur_decl_normal_off() const { return cur_decl_normal_off_; }
		int cur_decl_normal_type() const { return cur_decl_normal_type_; }
		int cur_decl_texcoord_off() const { return cur_decl_texcoord_off_; }

		// Foliage wind: read g__time.y from c196 for sway computation
		float time_y() const { return vs_const_[196 * 4 + 1]; }

		void increment_draw_count() { draw_call_count_++; }

		// Signal that D3D transforms were modified externally (e.g. terrain identity reset)
		// so the next engage() re-applies the correct FFP transforms.
		void mark_transforms_dirty() { world_dirty_ = true; view_proj_dirty_ = true; }

		// --- Utility ---

		static void mat4_transpose(float* dst, const float* src);
		static void mat4_multiply(float* dst, const float* a, const float* b);
		static bool mat4_is_interesting(const float* m);
		static void decompose_view_proj(const float* vp, D3DMATRIX& view, D3DMATRIX& proj);

	private:
		bool enabled_ = true;

		// VS/PS constant capture
		float vs_const_[256 * 4] = {};
		float ps_const_[224 * 4] = {};

		// Dirty tracking
		bool world_dirty_ = false;
		bool view_proj_dirty_ = false;
		bool view_proj_valid_ = false;
		bool ffp_active_ = false;
		bool ffp_setup_ = false;
		bool transforms_stale_ = false;

		// SetTransform-captured View/Proj (for games that use D3D SetTransform directly)
		D3DMATRIX st_view_ = {};
		D3DMATRIX st_proj_ = {};
		bool st_view_valid_ = false;
		bool st_proj_valid_ = false;

		// Shader tracking
		IDirect3DVertexShader9* last_vs_ = nullptr;
		IDirect3DPixelShader9* last_ps_ = nullptr;

		// Vertex declaration tracking
		IDirect3DVertexDeclaration9* last_decl_ = nullptr;
		bool cur_decl_is_skinned_ = false;
		bool cur_decl_has_texcoord_ = false;
		bool cur_decl_has_normal_ = false;
		bool cur_decl_has_color_ = false;
		bool cur_decl_has_binormal_ = false;
		bool cur_decl_has_tangent_ = false;
		bool cur_decl_has_position1_ = false;  // Morph target (POSITION with UsageIndex >= 1)
		bool cur_decl_has_pos_t_ = false;
		bool fvf_pos_t_ = false;  // SetFVF with D3DFVF_XYZRHW
		bool water_material_active_ = false;  // Gerstner wave constants written since last VS change
		bool cur_decl_has_texcoord5_ = false;
		int cur_decl_texcoord_type_ = -1;
		int cur_decl_texcoord_off_ = 0;
		int cur_decl_texcoord5_off_ = 0;
		int cur_decl_texcoord5_type_ = -1;
		int cur_decl_texcoord5_stream_ = -1;

		// Skinning-related declaration data
		int cur_decl_num_weights_ = 0;
		int cur_decl_blend_weight_off_ = 0;
		int cur_decl_blend_weight_type_ = 0;
		int cur_decl_blend_indices_off_ = 0;
		int cur_decl_pos_off_ = 0;
		int cur_decl_normal_off_ = 0;
		int cur_decl_normal_type_ = -1;

		// VS constant register layout (LOTRC Zero Engine)
		// ViewProj: c239-c242 (concatenated, decomposed into View + Proj via column extraction)
		// World: c178-c181 (g__worldMatrix, row-major 4x4)
		// Bones: c0-c176 (g__matrixPalette, 59 bones x 3 regs, float3x4)
		int vs_reg_view_start_ = -1;   // -1 = not from VS constants (separate View)
		int vs_reg_view_end_ = -1;
		int vs_reg_proj_start_ = -1;   // -1 = not from VS constants (separate Proj)
		int vs_reg_proj_end_ = -1;
		int vs_reg_viewproj_start_ = 239; // concatenated ViewProj register (-1 = disabled)
		int vs_reg_world_start_ = 178;
		int vs_reg_world_end_ = 182;
		int vs_reg_bone_threshold_ = 0;
		int vs_reg_bone_end_ = 177;  // exclusive upper bound: palette c0-c176 (59*3)
		int vs_regs_per_bone_ = 3;
		int vs_bone_min_regs_ = 3;

		// Bone detection
		int bone_start_reg_ = 0;
		int num_bones_ = 0;

		// Texture tracking
		IDirect3DBaseTexture9* cur_texture_[8] = {};

		// Stream source tracking
		IDirect3DVertexBuffer9* stream_vb_[4] = {};
		UINT stream_offset_[4] = {};
		UINT stream_stride_[4] = {};
		UINT stream_freq_[4] = { 1, 1, 1, 1 };

		// Foliage declaration (instanced, TC1/TC2/TC3 on stream 1)
		bool cur_decl_foliage_ = false;
		int cur_decl_foliage_tc1_off_ = -1;
		int cur_decl_foliage_tc2_off_ = -1;
		int cur_decl_foliage_tc3_off_ = -1;
		int cur_decl_foliage_stream_ = -1;

		// Spray billboard declaration (single-stream: Pos+Color+TC0+TC1+TC2+TC3, no Normal)
		bool cur_decl_is_spray_ = false;
		int cur_decl_color_off_ = 0;
		int cur_decl_tc1_off_ = -1;
		int cur_decl_tc2_off_ = -1;
		int cur_decl_tc3_off_ = -1;

		// Frame/draw counters
		UINT frame_count_ = 0;
		UINT draw_call_count_ = 0;
		UINT scene_count_ = 0;
		DWORD create_tick_ = 0;

		// VS constant write log (for diagnostics: which registers have been written)
		int vs_const_write_log_[256] = {};

		// Cached config
		const config::ffp_settings* cfg_ = nullptr;

		// Internal helpers
		void setup_lighting(IDirect3DDevice9* dev);
		void setup_texture_stages(IDirect3DDevice9* dev);
		void apply_transforms(IDirect3DDevice9* dev);
	};
}
