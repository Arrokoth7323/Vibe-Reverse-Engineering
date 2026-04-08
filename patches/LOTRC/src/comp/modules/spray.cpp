#include "std_include.hpp"
#include "spray.hpp"

#include "shared/common/config.hpp"
#include "shared/common/ffp_state.hpp"

namespace comp
{
	spray::spray()
	{
		p_this = this;
		shared::common::log("Spray", "Module initialized (CPU billboard expansion).",
			shared::common::LOG_TYPE::LOG_TYPE_WARN);
	}

	spray::~spray()
	{
		release_cache();
		if (spray_exp_decl_)
		{
			spray_exp_decl_->Release();
			spray_exp_decl_ = nullptr;
		}
		p_this = nullptr;
	}

	HRESULT spray::draw_spray_dip(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
		UINT start_idx, UINT prim_count)
	{
		auto& ffp = shared::common::ffp_state::get();

		// Skip Z-pass / shadow draws: degenerate VP produces garbage
		{
			const float* vp = ffp.vs_const_data() + ffp.reg_viewproj_start() * 4;
			float c0x = vp[0], c0y = vp[4], c0z = vp[8];
			float xs = c0x * c0x + c0y * c0y + c0z * c0z;
			float c1x = vp[1], c1y = vp[5], c1z = vp[9];
			float ys = c1x * c1x + c1y * c1y + c1z * c1z;
			if (xs < 0.01f || ys < 0.01f)
			{
				ffp.disengage(dev);
				return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
			}
		}

		if (!spray_exp_decl_)
			create_expanded_decl(dev);

		if (!spray_exp_decl_)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		// CPU-expand billboard vertices
		auto* src_vb = ffp.stream_vb(0);
		UINT stride = ffp.stream_stride(0);
		UINT soff = ffp.stream_offset(0);
		auto* exp_vb = expand_spray_vb(dev, src_vb, soff, base_vtx, min_vtx, num_verts, stride);

		if (!exp_vb)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		// Engage FFP
		ffp.engage(dev);

		IDirect3DVertexDeclaration9* orig_decl = nullptr;
		dev->GetVertexDeclaration(&orig_decl);
		dev->SetVertexDeclaration(spray_exp_decl_);
		dev->SetStreamSource(0, exp_vb, 0, SPRAY_VTX_SIZE);

		ffp.setup_albedo_texture(dev);

		// Identity WORLD — positions are already world-space
		static const D3DMATRIX identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		dev->SetTransform(D3DTS_WORLD, &identity);

		HRESULT hr = dev->DrawIndexedPrimitive(pt, -static_cast<INT>(min_vtx),
			min_vtx, num_verts, start_idx, prim_count);

		// Restore
		dev->SetVertexDeclaration(orig_decl);
		if (orig_decl) orig_decl->Release();
		dev->SetStreamSource(0, src_vb, ffp.stream_offset(0), stride);
		ffp.restore_textures(dev);
		ffp.mark_transforms_dirty();

		return hr;
	}

	HRESULT spray::draw_spray_dp(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, UINT start_vtx, UINT prim_count)
	{
		// Non-indexed path fallback — shader passthrough with transform hints
		auto& ffp = shared::common::ffp_state::get();
		ffp.disengage(dev);
		dev->SetTransform(D3DTS_WORLD,
			reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
		ffp.apply_camera_transforms(dev);
		HRESULT hr = dev->DrawPrimitive(pt, start_vtx, prim_count);
		ffp.mark_transforms_dirty();
		return hr;
	}

	void spray::on_reset()
	{
		release_cache();
		if (spray_exp_decl_)
		{
			spray_exp_decl_->Release();
			spray_exp_decl_ = nullptr;
		}
	}

	void spray::create_expanded_decl(IDirect3DDevice9* dev)
	{
		D3DVERTEXELEMENT9 elems[] = {
			{ 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
			{ 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
			{ 0, 28, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			D3DDECL_END()
		};

		HRESULT hr = dev->CreateVertexDeclaration(elems, &spray_exp_decl_);
		if (FAILED(hr))
		{
			shared::common::log("Spray", "Failed to create vertex declaration!",
				shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
			spray_exp_decl_ = nullptr;
		}
	}

	void spray::release_cache()
	{
		for (int i = 0; i < SPRAY_CACHE_SIZE; i++)
		{
			if (spray_exp_vb_[i])
			{
				spray_exp_vb_[i]->Release();
				spray_exp_vb_[i] = nullptr;
			}
			spray_exp_key_[i] = 0;
			spray_exp_nv_[i] = 0;
		}
	}

	IDirect3DVertexBuffer9* spray::expand_spray_vb(IDirect3DDevice9* dev,
		IDirect3DVertexBuffer9* src_vb, UINT stream_off, INT base_vtx,
		UINT min_vtx, UINT num_verts, UINT stride)
	{
		if (!src_vb || stride == 0 || num_verts == 0) return nullptr;

		unsigned int key = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(src_vb));
		key ^= num_verts * 0x517CC1B7u;
		int slot = key % SPRAY_CACHE_SIZE;

		if (spray_exp_vb_[slot] && spray_exp_nv_[slot] != num_verts)
		{
			spray_exp_vb_[slot]->Release();
			spray_exp_vb_[slot] = nullptr;
		}

		// Lock source VB
		unsigned char* src_data = nullptr;
		UINT read_off = stream_off + static_cast<UINT>(base_vtx + min_vtx) * stride;
		if (FAILED(src_vb->Lock(read_off, num_verts * stride,
			reinterpret_cast<void**>(&src_data), D3DLOCK_READONLY)))
			return nullptr;

		if (!spray_exp_vb_[slot])
		{
			if (FAILED(dev->CreateVertexBuffer(num_verts * SPRAY_VTX_SIZE,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
				&spray_exp_vb_[slot], nullptr)))
			{
				src_vb->Unlock();
				return nullptr;
			}
			spray_exp_nv_[slot] = num_verts;
		}

		float* dst_data = nullptr;
		if (FAILED(spray_exp_vb_[slot]->Lock(0, num_verts * SPRAY_VTX_SIZE,
			reinterpret_cast<void**>(&dst_data), D3DLOCK_DISCARD)))
		{
			src_vb->Unlock();
			return nullptr;
		}

		// Read VS constants for expansion
		auto& ffp = shared::common::ffp_state::get();
		const float* vs = ffp.vs_const_data();
		const float* world_mat  = &vs[178 * 4];  // c178-c181: g__worldMatrix
		const float* cam_right  = &vs[243 * 4];  // c243: g__cameraRight
		const float* cam_up     = &vs[244 * 4];  // c244: g__cameraUp
		const float* cam_pos    = &vs[245 * 4];  // c245: g__cameraPos
		const float* ctrl1      = &vs[20 * 4];   // c20: g__crowdControl1
		const float* ctrl2      = &vs[21 * 4];   // c21: g__crowdControl2
		const float* ctrl3      = &vs[22 * 4];   // c22: g__crowdControl3

		for (UINT v = 0; v < num_verts; v++)
		{
			expand_vertex(&dst_data[v * (SPRAY_VTX_SIZE / sizeof(float))],
				&src_data[v * stride],
				world_mat, cam_right, cam_up, cam_pos,
				ctrl1, ctrl2, ctrl3);
		}

		spray_exp_vb_[slot]->Unlock();
		src_vb->Unlock();

		return spray_exp_vb_[slot];
	}

	void spray::expand_vertex(float* dst, const unsigned char* src,
		const float* world_mat, const float* cam_right, const float* cam_up,
		const float* cam_pos, const float* ctrl1, const float* ctrl2,
		const float* ctrl3)
	{
		auto& ffp = shared::common::ffp_state::get();

		// Read source position (FLOAT3, offset 0)
		auto* pos = reinterpret_cast<const float*>(&src[ffp.cur_decl_pos_off()]);
		float px = pos[0], py = pos[1], pz = pos[2];

		// Read color (D3DCOLOR)
		DWORD color;
		std::memcpy(&color, &src[ffp.cur_decl_color_off()], sizeof(DWORD));

		// Read TC0 (FLOAT2): .x = rotation angle (degrees), .y = time seed
		auto* tc0 = reinterpret_cast<const float*>(&src[ffp.cur_decl_texcoord_off()]);
		float rot_deg = tc0[0];
		float time_seed = tc0[1];

		// Read half-width, half-height, corner index
		float half_w = *reinterpret_cast<const float*>(&src[ffp.cur_decl_tc1_off()]);
		float half_h = *reinterpret_cast<const float*>(&src[ffp.cur_decl_tc2_off()]);
		float corner_f = *reinterpret_cast<const float*>(&src[ffp.cur_decl_tc3_off()]);
		int corner = static_cast<int>(corner_f);
		if (corner < 0) corner = 0;
		if (corner > 3) corner = 3;

		// World transform: pos * worldMatrix (row-vector multiply)
		const float* m = world_mat;
		float wx = px * m[0] + py * m[4] + pz * m[8]  + m[12];
		float wy = px * m[1] + py * m[5] + pz * m[9]  + m[13];
		float wz = px * m[2] + py * m[6] + pz * m[10] + m[14];

		// Billboard expansion: worldPos + camRight * sign.x * halfW + camUp * sign.y * halfH
		float sx = CORNER_SIGN[corner][0];
		float sy = CORNER_SIGN[corner][1];

		dst[0] = wx + cam_right[0] * sx * half_w + cam_up[0] * sy * half_h;
		dst[1] = wy + cam_right[1] * sx * half_w + cam_up[1] * sy * half_h;
		dst[2] = wz + cam_right[2] * sx * half_w + cam_up[2] * sy * half_h;

		// Sub-pixel Y jitter to nudge Remix generation hash each frame.
		static DWORD s_jitter_start = GetTickCount();
		float jt = (GetTickCount() - s_jitter_start) * 0.001f;
		dst[1] += std::sin(jt * 6.2832f) * 0.001f;

		// Normal: fixed up (0, 1, 0)
		dst[3] = 0.0f;
		dst[4] = 1.0f;
		dst[5] = 0.0f;

		// Color: pass-through
		std::memcpy(&dst[6], &color, sizeof(DWORD));

		// Atlas UV computation
		// Viewing angle: atan2(look.x, look.z) + rotation
		float lx = cam_pos[0] - wx;
		float lz = cam_pos[2] - wz;
		float len = std::sqrt(lx * lx + lz * lz);
		if (len > 0.0001f) { lx /= len; lz /= len; }

		float angle_rad = std::atan2(lx, lz);
		float angle_deg = angle_rad * 57.2957802f + rot_deg + 180.0f;

		// Normalize to [0, 1)
		float norm_angle = angle_deg * (1.0f / 360.0f);
		norm_angle = norm_angle - std::floor(norm_angle);

		// Slice index (horizontal rotation)
		float slice_count = ctrl2[0];  // c21.x
		if (slice_count < 1.0f) slice_count = 1.0f;
		float slice_f = norm_angle * slice_count;
		int slice = static_cast<int>(slice_f);
		if (slice >= static_cast<int>(slice_count)) slice = 0;

		// Animation frame — wall-clock time because the Zero Engine pauses
		// both c20.y and c196.y when the camera is stationary.
		static DWORD s_start_tick = GetTickCount();
		float period = ctrl1[0];   // c20.x (seconds)
		float time = (GetTickCount() - s_start_tick) * 0.001f;
		float frame_count = ctrl1[2]; // c20.z
		if (period < 0.001f) period = 1.0f;
		if (frame_count < 1.0f) frame_count = 1.0f;

		float anim_time = std::fmod(time + time_seed, period);
		if (anim_time < 0.0f) anim_time += period;
		float frame_f = anim_time / period * frame_count;
		int frame = static_cast<int>(frame_f);
		if (frame >= static_cast<int>(frame_count)) frame = 0;

		// Atlas cell UV: (slice + cornerU) * cellWidth, (frame + cornerV) * cellHeight
		float cell_w = ctrl3[0];  // c22.x = 1 / numSlices
		float cell_h = ctrl3[1];  // c22.y = 1 / numFrames

		float u = (static_cast<float>(slice) + CORNER_UV[corner][0]) * cell_w;
		float v = (static_cast<float>(frame) + CORNER_UV[corner][1]) * cell_h;

		dst[7] = u;
		dst[8] = v;
	}
}
