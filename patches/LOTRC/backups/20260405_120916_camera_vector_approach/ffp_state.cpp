#include "std_include.hpp"
#include "ffp_state.hpp"

namespace shared::common
{
	ffp_state& ffp_state::get()
	{
		static ffp_state instance;
		return instance;
	}

	void ffp_state::init(IDirect3DDevice9* /*real_device*/)
	{
		cfg_ = &config::get().ffp;
		enabled_ = cfg_->enabled;
		create_tick_ = GetTickCount();

		bool use_st = (vs_reg_view_start_ < 0);
		log("FFP", std::format("State tracker initialized, FFP={}", enabled_ ? "ON" : "OFF"));
		if (vs_reg_viewproj_start_ >= 0)
			log("FFP", std::format("ViewProj: c{}-c{} (identity View), World: c{}-c{}",
				vs_reg_viewproj_start_, vs_reg_viewproj_start_ + 3,
				vs_reg_world_start_, vs_reg_world_end_ - 1));
		else if (use_st)
			log("FFP", std::format("View/Proj: from SetTransform, World: c{}-c{}", vs_reg_world_start_, vs_reg_world_end_ - 1));
		else
			log("FFP", std::format("Registers: View=c{}-c{} Proj=c{}-c{} World=c{}-c{}",
				vs_reg_view_start_, vs_reg_view_end_ - 1,
				vs_reg_proj_start_, vs_reg_proj_end_ - 1,
				vs_reg_world_start_, vs_reg_world_end_ - 1));
	}

	// ---- State mutators ----

	void ffp_state::on_set_vs_const_f(UINT start_reg, const float* data, UINT count)
	{
		if (!data || start_reg + count > 256) return;

		std::memcpy(&vs_const_[start_reg * 4], data, count * 4 * sizeof(float));

		UINT end_reg = start_reg + count;

		// World matrix dirty tracking (always from VS constants)
		if (start_reg < static_cast<UINT>(vs_reg_world_end_) &&
			end_reg > static_cast<UINT>(vs_reg_world_start_))
		{
			world_dirty_ = true;
		}

		// View/Proj dirty tracking: only from VS constants when not using SetTransform mode
		if (vs_reg_view_start_ >= 0)
		{
			if (start_reg < static_cast<UINT>(vs_reg_proj_end_) &&
				end_reg > static_cast<UINT>(vs_reg_view_start_))
			{
				view_proj_dirty_ = true;
			}

			auto view_start = static_cast<UINT>(vs_reg_view_start_);
			auto proj_start = static_cast<UINT>(vs_reg_proj_start_);
			auto proj_end = static_cast<UINT>(vs_reg_proj_end_);

			if (start_reg <= view_start && end_reg >= proj_end)
				view_proj_valid_ = true;
			else if (start_reg == view_start && count >= 4 && vs_const_write_log_[proj_start])
				view_proj_valid_ = true;
			else if (start_reg == proj_start && count >= 4 && vs_const_write_log_[view_start])
				view_proj_valid_ = true;
		}

		// Concatenated ViewProj detection: game uploads ViewProj to a single register range.
		// Decomposition into separate View + Projection happens in apply_transforms().
		if (vs_reg_viewproj_start_ >= 0)
		{
			auto vp_start = static_cast<UINT>(vs_reg_viewproj_start_);
			if (start_reg <= vp_start && end_reg >= vp_start + 4)
			{
				view_proj_valid_ = true;
				view_proj_dirty_ = true;
			}
		}

		for (UINT i = 0; i < count; i++)
			vs_const_write_log_[start_reg + i] = 1;

		// Bone palette detection (for skinning module)
		if (config::get().skinning.enabled &&
			start_reg >= static_cast<UINT>(vs_reg_bone_threshold_) &&
			count >= static_cast<UINT>(vs_bone_min_regs_) &&
			(count % static_cast<UINT>(vs_regs_per_bone_)) == 0)
		{
			bone_start_reg_ = static_cast<int>(start_reg);
			num_bones_ = static_cast<int>(count) / vs_regs_per_bone_;
		}
	}

	void ffp_state::on_set_ps_const_f(UINT start_reg, const float* data, UINT count)
	{
		if (!data || start_reg + count > 32) return;

		std::memcpy(&ps_const_[start_reg * 4], data, count * 4 * sizeof(float));
	}

	void ffp_state::on_set_vertex_shader(IDirect3DVertexShader9* shader)
	{
		if (shader) shader->AddRef();
		if (last_vs_) last_vs_->Release();
		last_vs_ = shader;
		ffp_active_ = false;
	}

	void ffp_state::on_set_pixel_shader(IDirect3DPixelShader9* shader)
	{
		if (shader) shader->AddRef();
		if (last_ps_) last_ps_->Release();
		last_ps_ = shader;
	}

	void ffp_state::on_set_texture(UINT stage, IDirect3DBaseTexture9* texture)
	{
		if (stage < 8)
			cur_texture_[stage] = texture;
	}

	void ffp_state::on_set_stream_source(UINT stream, IDirect3DVertexBuffer9* vb, UINT offset, UINT stride)
	{
		if (stream < 4)
		{
			stream_vb_[stream] = vb;
			stream_offset_[stream] = offset;
			stream_stride_[stream] = stride;
		}
	}

	void ffp_state::on_set_vertex_declaration(IDirect3DVertexDeclaration9* decl)
	{
		last_decl_ = decl;
		cur_decl_is_skinned_ = false;
		cur_decl_has_texcoord_ = false;
		cur_decl_has_normal_ = false;
		cur_decl_has_color_ = false;
		cur_decl_has_pos_t_ = false;
		cur_decl_texcoord_type_ = -1;
		cur_decl_texcoord_off_ = 0;
		cur_decl_num_weights_ = 0;
		cur_decl_blend_weight_off_ = 0;
		cur_decl_blend_weight_type_ = 0;
		cur_decl_blend_indices_off_ = 0;
		cur_decl_pos_off_ = 0;
		cur_decl_normal_off_ = 0;
		cur_decl_normal_type_ = -1;
		fvf_pos_t_ = false;

		if (!decl) return;

		UINT num_elems = 0;
		if (FAILED(decl->GetDeclaration(nullptr, &num_elems))) return;
		if (num_elems == 0 || num_elems > 32) return;

		D3DVERTEXELEMENT9 elems[32];
		if (FAILED(decl->GetDeclaration(elems, &num_elems))) return;

		bool has_blend_weight = false;
		bool has_blend_indices = false;

		for (UINT e = 0; e < num_elems; e++)
		{
			const auto& el = elems[e];
			if (el.Stream == 0xFF) break;

			switch (el.Usage)
			{
			case D3DDECLUSAGE_POSITIONT:
				cur_decl_has_pos_t_ = true;
				break;

			case D3DDECLUSAGE_BLENDWEIGHT:
				has_blend_weight = true;
				cur_decl_blend_weight_off_ = el.Offset;
				cur_decl_blend_weight_type_ = el.Type;
				break;

			case D3DDECLUSAGE_BLENDINDICES:
				has_blend_indices = true;
				cur_decl_blend_indices_off_ = el.Offset;
				break;

			case D3DDECLUSAGE_POSITION:
				if (el.Stream == 0)
					cur_decl_pos_off_ = el.Offset;
				break;

			case D3DDECLUSAGE_NORMAL:
				if (el.Stream == 0)
				{
					cur_decl_has_normal_ = true;
					cur_decl_normal_off_ = el.Offset;
					cur_decl_normal_type_ = el.Type;
				}
				break;

			case D3DDECLUSAGE_TEXCOORD:
				if (el.UsageIndex == 0 && el.Stream == 0)
				{
					cur_decl_has_texcoord_ = true;
					cur_decl_texcoord_type_ = el.Type;
					cur_decl_texcoord_off_ = el.Offset;
				}
				break;

			case D3DDECLUSAGE_COLOR:
				cur_decl_has_color_ = true;
				break;
			}
		}

		if (has_blend_weight && has_blend_indices)
		{
			cur_decl_is_skinned_ = true;

			switch (cur_decl_blend_weight_type_)
			{
			case D3DDECLTYPE_FLOAT1:  cur_decl_num_weights_ = 1; break;
			case D3DDECLTYPE_FLOAT2:  cur_decl_num_weights_ = 2; break;
			case D3DDECLTYPE_FLOAT3:  cur_decl_num_weights_ = 3; break;
			case D3DDECLTYPE_FLOAT4:  cur_decl_num_weights_ = 3; break;
			case D3DDECLTYPE_UBYTE4N: cur_decl_num_weights_ = 3; break;
			default:                  cur_decl_num_weights_ = 3; break;
			}
		}
	}

	void ffp_state::on_set_fvf(DWORD fvf)
	{
		// D3DFVF_XYZRHW = 0x004 — pre-transformed screen-space vertices (Scaleform UI)
		fvf_pos_t_ = (fvf & D3DFVF_XYZRHW) != 0;
		// SetFVF implicitly clears the vertex declaration
		last_decl_ = nullptr;
	}

	void ffp_state::on_set_transform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix)
	{
		if (!matrix) return;
		// ViewProj from VS constants takes priority
		if (vs_reg_viewproj_start_ >= 0 || vs_reg_view_start_ >= 0) return;

		if (state == D3DTS_VIEW)
		{
			st_view_ = *matrix;
			st_view_valid_ = true;
			view_proj_dirty_ = true;
			if (st_proj_valid_) view_proj_valid_ = true;
		}
		else if (state == D3DTS_PROJECTION)
		{
			st_proj_ = *matrix;
			st_proj_valid_ = true;
			view_proj_dirty_ = true;
			if (st_view_valid_) view_proj_valid_ = true;
		}
	}

	void ffp_state::on_present()
	{
		frame_count_++;
		ffp_setup_ = false;
		draw_call_count_ = 0;
		scene_count_ = 0;
		// Safety net: restore shaders if a draw path exited without calling disengage.
		// No-op when already disengaged (the normal case).
		disengage(shared::globals::d3d_device);
		std::memset(vs_const_write_log_, 0, sizeof(vs_const_write_log_));
	}

	void ffp_state::on_begin_scene()
	{
		ffp_setup_ = false;
		scene_count_++;
	}

	void ffp_state::on_reset()
	{
		if (last_vs_) { last_vs_->Release(); last_vs_ = nullptr; }
		if (last_ps_) { last_ps_->Release(); last_ps_ = nullptr; }
		last_decl_ = nullptr;

		// Default-pool resources (textures, VBs) are released by the game before Reset
		std::memset(cur_texture_, 0, sizeof(cur_texture_));
		std::memset(stream_vb_, 0, sizeof(stream_vb_));
		std::memset(stream_offset_, 0, sizeof(stream_offset_));
		std::memset(stream_stride_, 0, sizeof(stream_stride_));

		view_proj_valid_ = false;
		ffp_setup_ = false;
		world_dirty_ = false;
		view_proj_dirty_ = false;
		ffp_active_ = false;
		bone_start_reg_ = 0;
		num_bones_ = 0;
		st_view_valid_ = false;
		st_proj_valid_ = false;
		st_view_ = {};
		st_proj_ = {};

		cur_decl_is_skinned_ = false;
		cur_decl_has_texcoord_ = false;
		cur_decl_has_normal_ = false;
		cur_decl_has_color_ = false;
		cur_decl_has_pos_t_ = false;
		fvf_pos_t_ = false;
		cur_decl_texcoord_type_ = -1;
		cur_decl_texcoord_off_ = 0;
		cur_decl_num_weights_ = 0;
		cur_decl_blend_weight_off_ = 0;
		cur_decl_blend_weight_type_ = 0;
		cur_decl_blend_indices_off_ = 0;
		cur_decl_pos_off_ = 0;
		cur_decl_normal_off_ = 0;
		cur_decl_normal_type_ = -1;

		std::memset(vs_const_write_log_, 0, sizeof(vs_const_write_log_));

		log("FFP", "State reset");
	}

	// ---- State consumers ----

	void ffp_state::engage(IDirect3DDevice9* dev)
	{
		if (!cfg_ || !enabled_ || !dev) return;

		if (!ffp_active_)
		{
			dev->SetVertexShader(nullptr);
			dev->SetPixelShader(nullptr);
			ffp_active_ = true;
		}

		setup_texture_stages(dev);

		if (!ffp_setup_)
		{
			setup_lighting(dev);
			ffp_setup_ = true;
		}
	}

	void ffp_state::disengage(IDirect3DDevice9* dev)
	{
		if (!ffp_active_ || !dev) return;

		dev->SetVertexShader(last_vs_);
		dev->SetPixelShader(last_ps_);
		ffp_active_ = false;
	}

	void ffp_state::setup_albedo_texture(IDirect3DDevice9* dev)
	{
		if (!cfg_ || !dev) return;

		int as = cfg_->albedo_stage;
		auto* albedo = (as >= 0 && as < 8) ? cur_texture_[as] : cur_texture_[0];

		dev->SetTexture(0, albedo);
		for (DWORD ts = 1; ts < 8; ts++)
			dev->SetTexture(ts, nullptr);
	}

	void ffp_state::restore_textures(IDirect3DDevice9* dev)
	{
		if (!dev) return;

		for (DWORD ts = 0; ts < 8; ts++)
			dev->SetTexture(ts, cur_texture_[ts]);
	}

	// ---- Internal helpers ----

	void ffp_state::apply_transforms(IDirect3DDevice9* dev)
	{
		if (view_proj_dirty_)
		{
			if (vs_reg_viewproj_start_ >= 0 && view_proj_valid_ &&
				vs_reg_cam_right_ >= 0 && vs_reg_cam_up_ >= 0 && vs_reg_cam_pos_ >= 0)
			{
				// Build View from the game's camera vectors (c243=right, c244=up, c245=pos).
				// Forward = cross(right, up). Then derive Projection = View_inv * ViewProj.
				const float* R = &vs_const_[vs_reg_cam_right_ * 4];  // right.xyz
				const float* U = &vs_const_[vs_reg_cam_up_ * 4];     // up.xyz
				const float* E = &vs_const_[vs_reg_cam_pos_ * 4];    // eye position.xyz

				// Forward = cross(Right, Up)
				float Fx = R[1] * U[2] - R[2] * U[1];
				float Fy = R[2] * U[0] - R[0] * U[2];
				float Fz = R[0] * U[1] - R[1] * U[0];

				// Normalize forward
				float flen = sqrtf(Fx * Fx + Fy * Fy + Fz * Fz);
				if (flen > 1e-6f)
				{
					float inv_flen = 1.0f / flen;
					Fx *= inv_flen; Fy *= inv_flen; Fz *= inv_flen;

					// D3D9 row-vector View: pos * View transforms world→view
					// Columns = camera axes, row 3 = -dot(axis, eye)
					D3DMATRIX view = {};
					view._11 = R[0]; view._12 = U[0]; view._13 = Fx; view._14 = 0;
					view._21 = R[1]; view._22 = U[1]; view._23 = Fy; view._24 = 0;
					view._31 = R[2]; view._32 = U[2]; view._33 = Fz; view._34 = 0;
					view._41 = -(R[0]*E[0] + R[1]*E[1] + R[2]*E[2]);
					view._42 = -(U[0]*E[0] + U[1]*E[1] + U[2]*E[2]);
					view._43 = -(Fx*E[0] + Fy*E[1] + Fz*E[2]);
					view._44 = 1;

					// Projection = View_inv * VP
					// View_inv for orthonormal rotation + translation:
					//   R_inv = R^T, T_inv = -R^T * T  →  V_inv = [R^T | eye]
					// V_inv[row][col] with axes as rows:
					float vi[16];
					vi[0]  = R[0]; vi[1]  = R[1]; vi[2]  = R[2]; vi[3]  = 0;
					vi[4]  = U[0]; vi[5]  = U[1]; vi[6]  = U[2]; vi[7]  = 0;
					vi[8]  = Fx;   vi[9]  = Fy;   vi[10] = Fz;   vi[11] = 0;
					vi[12] = E[0]; vi[13] = E[1]; vi[14] = E[2]; vi[15] = 1;

					// P = V_inv * VP  (row-major 4x4 multiply)
					const float* vp = &vs_const_[vs_reg_viewproj_start_ * 4];
					D3DMATRIX proj = {};
					float* p = reinterpret_cast<float*>(&proj);
					for (int r = 0; r < 4; r++)
						for (int c = 0; c < 4; c++)
						{
							float sum = 0;
							for (int k = 0; k < 4; k++)
								sum += vi[r * 4 + k] * vp[k * 4 + c];
							p[r * 4 + c] = sum;
						}

					dev->SetTransform(D3DTS_VIEW, &view);
					dev->SetTransform(D3DTS_PROJECTION, &proj);
				}
			}
			else if (st_view_valid_ && st_proj_valid_)
			{
				dev->SetTransform(D3DTS_VIEW, &st_view_);
				dev->SetTransform(D3DTS_PROJECTION, &st_proj_);
			}

			view_proj_dirty_ = false;
		}

		if (world_dirty_)
		{
			dev->SetTransform(D3DTS_WORLD,
				reinterpret_cast<const D3DMATRIX*>(&vs_const_[vs_reg_world_start_ * 4]));
			world_dirty_ = false;
		}
	}

	void ffp_state::setup_lighting(IDirect3DDevice9* dev)
	{
		dev->SetRenderState(D3DRS_LIGHTING, FALSE);

		D3DMATERIAL9 mat = {};
		mat.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
		mat.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
		dev->SetMaterial(&mat);
	}

	void ffp_state::setup_texture_stages(IDirect3DDevice9* dev)
	{
		// Stage 0: modulate texture color with vertex/material diffuse
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

		// Disable stages 1-7: the game binds shadow maps, LUTs, normal maps etc.
		// on higher stages for its pixel shaders. In FFP mode those become active
		// and Remix may consume the wrong textures.
		for (DWORD s = 1; s <= 7; s++)
		{
			dev->SetTextureStageState(s, D3DTSS_COLOROP, D3DTOP_DISABLE);
			dev->SetTextureStageState(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
	}

	// ---- Utility ----

	void ffp_state::mat4_transpose(float* dst, const float* src)
	{
		dst[0]  = src[0];  dst[1]  = src[4];  dst[2]  = src[8];  dst[3]  = src[12];
		dst[4]  = src[1];  dst[5]  = src[5];  dst[6]  = src[9];  dst[7]  = src[13];
		dst[8]  = src[2];  dst[9]  = src[6];  dst[10] = src[10]; dst[11] = src[14];
		dst[12] = src[3];  dst[13] = src[7];  dst[14] = src[11]; dst[15] = src[15];
	}

	bool ffp_state::mat4_is_interesting(const float* m)
	{
		bool all_zero = true;
		for (int i = 0; i < 16; i++)
		{
			if (m[i] != 0.0f) { all_zero = false; break; }
		}
		if (all_zero) return false;

		// Check for identity
		if (m[0] == 1.0f && m[1] == 0.0f && m[2] == 0.0f  && m[3] == 0.0f &&
			m[4] == 0.0f && m[5] == 1.0f && m[6] == 0.0f  && m[7] == 0.0f &&
			m[8] == 0.0f && m[9] == 0.0f && m[10] == 1.0f && m[11] == 0.0f &&
			m[12] == 0.0f && m[13] == 0.0f && m[14] == 0.0f && m[15] == 1.0f)
			return false;

		return true;
	}
}
