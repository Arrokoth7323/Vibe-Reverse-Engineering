#include "std_include.hpp"
#include "skinning.hpp"

#include "shared/common/config.hpp"
#include "shared/common/ffp_state.hpp"

namespace comp
{
	skinning::skinning()
	{
		p_this = this;
		shared::common::log("Skinning", "Module initialized (CPU skinning).", shared::common::LOG_TYPE::LOG_TYPE_WARN);
	}

	skinning::~skinning()
	{
		release_cache();
		if (skin_exp_decl_)
		{
			skin_exp_decl_->Release();
			skin_exp_decl_ = nullptr;
		}
		p_this = nullptr;
	}

	HRESULT skinning::draw_skinned_dip(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
		UINT start_idx, UINT prim_count)
	{
		auto& ffp = shared::common::ffp_state::get();

		// Skip FFP for Z-pass / shadow draws: degenerate VP produces garbage.
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

		if (!skin_exp_decl_)
			create_expanded_decl(dev);

		if (!skin_exp_decl_)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		// Pre-compute bone matrices (model-space for crowd, world-space for regular)
		prepare_bone_matrices();

		// CPU-skin vertices
		auto* src_vb = ffp.stream_vb(0);
		UINT stride = ffp.stream_stride(0);
		UINT soff = ffp.stream_offset(0);
		auto* exp_vb = get_skinned_vb(dev, src_vb, soff, base_vtx, min_vtx, num_verts, stride);

		if (!exp_vb)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		// Engage FFP: null shaders, decompose c239->View/Proj, setup textures/lighting.
		ffp.engage(dev);

		IDirect3DVertexDeclaration9* orig_decl = nullptr;
		dev->GetVertexDeclaration(&orig_decl);
		dev->SetVertexDeclaration(skin_exp_decl_);
		dev->SetStreamSource(0, exp_vb, 0, SKIN_VTX_SIZE);

		ffp.setup_albedo_texture(dev);

		HRESULT hr;

		if (is_crowd_inst_)
		{
			// Crowd multi-instance path: model-space VB + per-instance World matrix
			UINT inst_count = ffp.is_instanced() ? ffp.instance_count() : 1;
			int tc5_stream = ffp.cur_decl_texcoord5_stream();
			auto* inst_vb = ffp.stream_vb(tc5_stream);
			UINT inst_stride = ffp.stream_stride(tc5_stream);
			UINT inst_off = ffp.stream_offset(tc5_stream) + ffp.cur_decl_texcoord5_off();

			// Disable hardware instancing and unbind instance stream
			dev->SetStreamSourceFreq(0, 1);
			dev->SetStreamSourceFreq(1, 1);
			dev->SetStreamSource(1, nullptr, 0, 0);

			if (!inst_vb || inst_stride == 0 || inst_count == 0)
			{
				// No instance data - draw once with identity
				static const D3DMATRIX identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
				dev->SetTransform(D3DTS_WORLD, &identity);
				hr = dev->DrawIndexedPrimitive(pt, -static_cast<INT>(min_vtx),
					min_vtx, num_verts, start_idx, prim_count);
			}
			else
			{
				// Lock all instance data at once
				unsigned char* inst_data = nullptr;
				if (FAILED(inst_vb->Lock(inst_off, inst_count * inst_stride,
					reinterpret_cast<void**>(&inst_data), D3DLOCK_READONLY)))
				{
					static const D3DMATRIX identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
					dev->SetTransform(D3DTS_WORLD, &identity);
					hr = dev->DrawIndexedPrimitive(pt, -static_cast<INT>(min_vtx),
						min_vtx, num_verts, start_idx, prim_count);
				}
				else
				{
					hr = S_OK;
					for (UINT i = 0; i < inst_count; i++)
					{
						auto* tc5 = reinterpret_cast<const float*>(inst_data + i * inst_stride);
						D3DMATRIX world = {};
						build_crowd_world(tc5, reinterpret_cast<float*>(&world));
						dev->SetTransform(D3DTS_WORLD, &world);
						hr = dev->DrawIndexedPrimitive(pt, -static_cast<INT>(min_vtx),
							min_vtx, num_verts, start_idx, prim_count);
					}
					inst_vb->Unlock();
				}
			}

			// Restore instance stream
			auto* s1_vb = ffp.stream_vb(1);
			if (s1_vb)
				dev->SetStreamSource(1, s1_vb, ffp.stream_offset(1), ffp.stream_stride(1));
			dev->SetStreamSourceFreq(0, ffp.stream_freq(0));
			dev->SetStreamSourceFreq(1, ffp.stream_freq(1));
		}
		else
		{
			// Regular skinned: bone_world already includes World, positions are world-space
			static const D3DMATRIX identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
			dev->SetTransform(D3DTS_WORLD, &identity);
			hr = dev->DrawIndexedPrimitive(pt, -static_cast<INT>(min_vtx),
				min_vtx, num_verts, start_idx, prim_count);
		}

		// Restore
		dev->SetVertexDeclaration(orig_decl);
		if (orig_decl) orig_decl->Release();
		dev->SetStreamSource(0, src_vb, ffp.stream_offset(0), stride);
		ffp.restore_textures(dev);

		return hr;
	}

	void skinning::on_reset()
	{
		release_cache();
		if (skin_exp_decl_)
		{
			skin_exp_decl_->Release();
			skin_exp_decl_ = nullptr;
		}
	}

	void skinning::create_expanded_decl(IDirect3DDevice9* dev)
	{
		// CPU-skinned vertex: position + normal + texcoord (no blend data)
		D3DVERTEXELEMENT9 elems[] = {
			{ 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
			{ 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			D3DDECL_END()
		};

		HRESULT hr = dev->CreateVertexDeclaration(elems, &skin_exp_decl_);
		if (FAILED(hr))
		{
			shared::common::log("Skinning", "Failed to create vertex declaration!",
				shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
			skin_exp_decl_ = nullptr;
		}
	}

	void skinning::release_cache()
	{
		for (int i = 0; i < SKIN_CACHE_SIZE; i++)
		{
			if (skin_exp_vb_[i])
			{
				skin_exp_vb_[i]->Release();
				skin_exp_vb_[i] = nullptr;
			}
			skin_exp_key_[i] = 0;
			skin_exp_nv_[i] = 0;
		}
	}

	void skinning::prepare_bone_matrices()
	{
		auto& ffp = shared::common::ffp_state::get();

		int bone_start = ffp.bone_start_reg();
		int num_bones = ffp.num_bones();
		int regs_per_bone = ffp.regs_per_bone();
		const float* vs_const = ffp.vs_const_data();
		const float* world = vs_const + ffp.reg_world_start() * 4;

		// Detect crowd _inst draws: TEXCOORD5 on a separate stream carries instance transform
		is_crowd_inst_ = ffp.cur_decl_has_texcoord5() && ffp.cur_decl_texcoord5_stream() != 0;

		num_bones_ready_ = (num_bones > MAX_BONES) ? MAX_BONES : num_bones;
		if (bone_start < ffp.reg_bone_threshold() || num_bones_ready_ <= 0)
		{
			num_bones_ready_ = 0;
			return;
		}

		for (int i = 0; i < num_bones_ready_; i++)
		{
			const float* b = &vs_const[(bone_start + i * regs_per_bone) * 4];

			// Transpose shader 3x4 row-major bone -> D3D 4x4 for row-vector multiply
			float bone[16];
			if (regs_per_bone == 3)
			{
				bone[0]  = b[0]; bone[1]  = b[4]; bone[2]  = b[8];  bone[3]  = 0.0f;
				bone[4]  = b[1]; bone[5]  = b[5]; bone[6]  = b[9];  bone[7]  = 0.0f;
				bone[8]  = b[2]; bone[9]  = b[6]; bone[10] = b[10]; bone[11] = 0.0f;
				bone[12] = b[3]; bone[13] = b[7]; bone[14] = b[11]; bone[15] = 1.0f;
			}
			else
			{
				shared::common::ffp_state::mat4_transpose(bone, b);
			}

			if (is_crowd_inst_)
			{
				// Crowd: bones operate in model-space only
				std::memcpy(bone_world_[i], bone, sizeof(float) * 16);
			}
			else
			{
				// Regular skinned: bone_world = bone * World
				shared::common::ffp_state::mat4_multiply(bone_world_[i], bone, world);
			}
		}
	}

	void skinning::build_crowd_world(const float* tc5, float* world)
	{
		// TEXCOORD5 = (inst_x, inst_y, inst_z, rotation_angle)
		float angle = tc5[3];
		float s = std::sin(angle);
		float c = std::cos(angle);

		world[0] = c;     world[1] = 0.0f;  world[2] = s;     world[3] = 0.0f;
		world[4] = 0.0f;  world[5] = 1.0f;  world[6] = 0.0f;  world[7] = 0.0f;
		world[8] = -s;    world[9] = 0.0f;  world[10] = c;    world[11] = 0.0f;
		world[12] = tc5[0]; world[13] = tc5[1]; world[14] = tc5[2]; world[15] = 1.0f;
	}

	IDirect3DVertexBuffer9* skinning::get_skinned_vb(IDirect3DDevice9* dev,
		IDirect3DVertexBuffer9* src_vb, UINT stream_off, INT base_vtx,
		UINT min_vtx, UINT num_verts, UINT stride)
	{
		if (!src_vb || stride == 0 || num_verts == 0) return nullptr;

		// Re-skin every frame: bone matrices change with animation.
		// Reuse VB allocation when possible (same slot = same size).
		unsigned int key = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(src_vb));
		key ^= num_verts * 0x517CC1B7u;
		int slot = key % SKIN_CACHE_SIZE;

		// Reuse existing buffer if same vertex count, otherwise recreate
		if (skin_exp_vb_[slot] && skin_exp_nv_[slot] != num_verts)
		{
			skin_exp_vb_[slot]->Release();
			skin_exp_vb_[slot] = nullptr;
		}

		// Lock source VB
		unsigned char* src_data = nullptr;
		UINT read_off = stream_off + static_cast<UINT>(base_vtx + min_vtx) * stride;
		if (FAILED(src_vb->Lock(read_off, num_verts * stride, reinterpret_cast<void**>(&src_data), D3DLOCK_READONLY)))
			return nullptr;

		// Create VB if needed
		if (!skin_exp_vb_[slot])
		{
			if (FAILED(dev->CreateVertexBuffer(num_verts * SKIN_VTX_SIZE,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
				&skin_exp_vb_[slot], nullptr)))
			{
				src_vb->Unlock();
				return nullptr;
			}
			skin_exp_nv_[slot] = num_verts;
		}

		// Lock and skin
		float* dst_data = nullptr;
		if (FAILED(skin_exp_vb_[slot]->Lock(0, num_verts * SKIN_VTX_SIZE,
			reinterpret_cast<void**>(&dst_data), D3DLOCK_DISCARD)))
		{
			src_vb->Unlock();
			return nullptr;
		}

		for (UINT v = 0; v < num_verts; v++)
			skin_vertex(&dst_data[v * (SKIN_VTX_SIZE / sizeof(float))], &src_data[v * stride]);

		skin_exp_vb_[slot]->Unlock();
		src_vb->Unlock();

		return skin_exp_vb_[slot];
	}

	void skinning::skin_vertex(float* dst, const unsigned char* src)
	{
		auto& ffp = shared::common::ffp_state::get();

		// Read source position (FLOAT3)
		auto* pos = reinterpret_cast<const float*>(&src[ffp.cur_decl_pos_off()]);
		float px = pos[0], py = pos[1], pz = pos[2];

		// Read blend weights
		float w[4] = { 0, 0, 0, 0 };
		auto bw_type = ffp.cur_decl_blend_weight_type();
		auto bw_off = ffp.cur_decl_blend_weight_off();

		if (bw_type == D3DDECLTYPE_D3DCOLOR)
		{
			// Memory [B,G,R,A] -> GPU RGBA: x=R(byte2), y=G(byte1), z=B(byte0)
			auto* bw = &src[bw_off];
			w[0] = bw[2] / 255.0f;
			w[1] = bw[1] / 255.0f;
			w[2] = bw[0] / 255.0f;
			w[3] = 1.0f - w[0] - w[1] - w[2];
		}
		else if (bw_type == D3DDECLTYPE_FLOAT3 || bw_type == D3DDECLTYPE_FLOAT2 || bw_type == D3DDECLTYPE_FLOAT1)
		{
			auto* bw = reinterpret_cast<const float*>(&src[bw_off]);
			int nw = ffp.cur_decl_num_weights();
			w[0] = (nw >= 1) ? bw[0] : 0.0f;
			w[1] = (nw >= 2) ? bw[1] : 0.0f;
			w[2] = (nw >= 3) ? bw[2] : 0.0f;
			w[3] = 1.0f - w[0] - w[1] - w[2];
		}
		else if (bw_type == D3DDECLTYPE_UBYTE4N)
		{
			auto* bw = &src[bw_off];
			w[0] = bw[0] / 255.0f;
			w[1] = bw[1] / 255.0f;
			w[2] = bw[2] / 255.0f;
			w[3] = 1.0f - w[0] - w[1] - w[2];
		}

		// Read blend indices (UBYTE4, sequential bone numbers)
		auto* bi = &src[ffp.cur_decl_blend_indices_off()];

		// CPU skin: localPos = sum(wi * pos * bone[bi])
		float lx = 0, ly = 0, lz = 0;
		for (int k = 0; k < 4; k++)
		{
			if (w[k] == 0.0f) continue;
			int idx = bi[k];
			if (idx >= num_bones_ready_) idx = 0;
			const float* m = bone_world_[idx];

			// pos * M (row-vector): out.x = px*m[0] + py*m[4] + pz*m[8] + m[12]
			lx += w[k] * (px * m[0] + py * m[4] + pz * m[8]  + m[12]);
			ly += w[k] * (px * m[1] + py * m[5] + pz * m[9]  + m[13]);
			lz += w[k] * (px * m[2] + py * m[6] + pz * m[10] + m[14]);
		}

		// Write position (model-space for crowd, world-space for regular)
		dst[0] = lx; dst[1] = ly; dst[2] = lz;

		// Normal - decode and rotate by first bone (approximation)
		float nx, ny, nz;
		if (ffp.cur_decl_has_normal())
		{
			float rn[3];
			decode_normal(&src[ffp.cur_decl_normal_off()], ffp.cur_decl_normal_type(), rn);
			nx = rn[0]; ny = rn[1]; nz = rn[2];
		}
		else
		{
			nx = 0.0f; ny = 1.0f; nz = 0.0f;
		}

		// Rotate normal by dominant bone's 3x3 rotation
		int dom = bi[0];
		if (dom >= num_bones_ready_) dom = 0;
		const float* m = bone_world_[dom];
		dst[3] = nx * m[0] + ny * m[4] + nz * m[8];
		dst[4] = nx * m[1] + ny * m[5] + nz * m[9];
		dst[5] = nx * m[2] + ny * m[6] + nz * m[10];

		// Texcoord
		if (ffp.cur_decl_has_texcoord())
		{
			int tc_type = ffp.cur_decl_texcoord_type();
			int tc_off = ffp.cur_decl_texcoord_off();
			if (tc_type == D3DDECLTYPE_FLOAT2 || tc_type == D3DDECLTYPE_FLOAT3 || tc_type == D3DDECLTYPE_FLOAT4)
			{
				auto* tc = reinterpret_cast<const float*>(&src[tc_off]);
				dst[6] = tc[0]; dst[7] = tc[1];
			}
			else if (tc_type == D3DDECLTYPE_FLOAT16_2)
			{
				auto* h = reinterpret_cast<const unsigned short*>(&src[tc_off]);
				dst[6] = half_to_float(h[0]);
				dst[7] = half_to_float(h[1]);
			}
			else
			{
				dst[6] = dst[7] = 0.0f;
			}
		}
		else
		{
			dst[6] = dst[7] = 0.0f;
		}
	}

	float skinning::half_to_float(unsigned short h)
	{
		unsigned int sign = (h >> 15) & 0x1;
		unsigned int exp = (h >> 10) & 0x1F;
		unsigned int mant = h & 0x3FF;

		if (exp == 0)
		{
			if (mant == 0) {
				unsigned int f = sign << 31;
				float result;
				std::memcpy(&result, &f, sizeof(float));
				return result;
			}
			while (!(mant & 0x400)) { mant <<= 1; exp--; }
			exp++; mant &= ~0x400u;
		}
		else if (exp == 31)
		{
			unsigned int f = (sign << 31) | 0x7F800000u | (mant << 13);
			float result;
			std::memcpy(&result, &f, sizeof(float));
			return result;
		}

		exp += (127 - 15);
		unsigned int f = (sign << 31) | (exp << 23) | (mant << 13);
		float result;
		std::memcpy(&result, &f, sizeof(float));
		return result;
	}

	void skinning::decode_normal(const unsigned char* src, int type, float* out)
	{
		switch (type)
		{
		case D3DDECLTYPE_FLOAT3:
		{
			auto* fp = reinterpret_cast<const float*>(src);
			out[0] = fp[0]; out[1] = fp[1]; out[2] = fp[2];
			break;
		}
		case D3DDECLTYPE_D3DCOLOR:
		{
			// Memory [B,G,R,A] -> GPU RGBA: x=R(byte2), y=G(byte1), z=B(byte0)
			out[0] = src[2] / 127.5f - 1.0f;
			out[1] = src[1] / 127.5f - 1.0f;
			out[2] = src[0] / 127.5f - 1.0f;
			break;
		}
		case D3DDECLTYPE_FLOAT16_2:
		{
			auto* h = reinterpret_cast<const unsigned short*>(src);
			out[0] = half_to_float(h[0]);
			out[1] = half_to_float(h[1]);
			out[2] = 0.0f;
			break;
		}
		case D3DDECLTYPE_DEC3N:
		{
			unsigned int packed = *reinterpret_cast<const unsigned int*>(src);
			int x = static_cast<int>((packed >>  0) & 0x3FF); if (x & 0x200) x |= ~0x3FF;
			int y = static_cast<int>((packed >> 10) & 0x3FF); if (y & 0x200) y |= ~0x3FF;
			int z = static_cast<int>((packed >> 20) & 0x3FF); if (z & 0x200) z |= ~0x3FF;
			out[0] = x / 511.0f;
			out[1] = y / 511.0f;
			out[2] = z / 511.0f;
			break;
		}
		case D3DDECLTYPE_UBYTE4N:
		{
			out[0] = (src[0] / 127.5f) - 1.0f;
			out[1] = (src[1] / 127.5f) - 1.0f;
			out[2] = (src[2] / 127.5f) - 1.0f;
			break;
		}
		default:
			out[0] = 0.0f; out[1] = 1.0f; out[2] = 0.0f;
			break;
		}
	}
}
