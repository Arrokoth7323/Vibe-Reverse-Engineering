#include "std_include.hpp"
#include "foliage.hpp"

#include "shared/common/config.hpp"
#include "shared/common/ffp_state.hpp"

namespace comp
{
	IDirect3DVertexDeclaration9* foliage::s_ffp_decl = nullptr;

	void foliage::ensure_ffp_decl(IDirect3DDevice9* dev)
	{
		if (s_ffp_decl) return;

		auto& ffp = shared::common::ffp_state::get();
		auto* decl = ffp.last_decl();
		if (!decl) return;

		UINT num_elems = 0;
		if (FAILED(decl->GetDeclaration(nullptr, &num_elems))) return;
		if (num_elems == 0 || num_elems > 32) return;

		D3DVERTEXELEMENT9 src[32];
		if (FAILED(decl->GetDeclaration(src, &num_elems))) return;

		// Copy only stream 0 elements (strip instance stream)
		D3DVERTEXELEMENT9 dst[16];
		UINT dst_count = 0;
		for (UINT e = 0; e < num_elems && src[e].Stream != 0xFF; e++)
		{
			if (src[e].Stream == 0 && dst_count < 15)
				dst[dst_count++] = src[e];
		}

		dst[dst_count] = D3DDECL_END();
		dev->CreateVertexDeclaration(dst, &s_ffp_decl);
	}

	void foliage::build_world_matrix(const float* inst_pos, const float* orient,
		const float* wind, float* world)
	{
		constexpr float TWO_PI = 6.28318548f;
		constexpr float PI = 3.14159274f;

		// Build basis frame from orientation
		float bz[3] = { 1.0f, orient[0] * TWO_PI, 0.0f };
		float bx[3] = { 0.0f, orient[1] * TWO_PI, 1.0f };

		// Normalize bz
		float len = std::sqrt(bz[0] * bz[0] + bz[1] * bz[1] + bz[2] * bz[2]);
		if (len > 0.00001f) { float inv = 1.0f / len; bz[0] *= inv; bz[1] *= inv; bz[2] *= inv; }

		// Normalize bx
		len = std::sqrt(bx[0] * bx[0] + bx[1] * bx[1] + bx[2] * bx[2]);
		if (len > 0.00001f) { float inv = 1.0f / len; bx[0] *= inv; bx[1] *= inv; bx[2] *= inv; }

		// by = normalize(cross(bx, bz))
		float by[3] = {
			bx[1] * bz[2] - bx[2] * bz[1],
			bx[2] * bz[0] - bx[0] * bz[2],
			bx[0] * bz[1] - bx[1] * bz[0]
		};
		len = std::sqrt(by[0] * by[0] + by[1] * by[1] + by[2] * by[2]);
		if (len > 0.00001f) { float inv = 1.0f / len; by[0] *= inv; by[1] *= inv; by[2] *= inv; }

		// Y-axis rotation from wind.z
		float frac_val = wind[2] * 0.00277777785f + 0.5f;
		frac_val = frac_val - std::floor(frac_val);
		float theta = frac_val * TWO_PI - PI;
		float sinT = std::sin(theta);
		float cosT = std::cos(theta);

		// Combined: RotY(theta) * BasisFrame
		// Row 0 = cos*bx + sin*bz
		world[0]  = cosT * bx[0] + sinT * bz[0];
		world[1]  = cosT * bx[1] + sinT * bz[1];
		world[2]  = cosT * bx[2] + sinT * bz[2];
		world[3]  = 0.0f;
		// Row 1 = by
		world[4]  = by[0];
		world[5]  = by[1];
		world[6]  = by[2];
		world[7]  = 0.0f;
		// Row 2 = -sin*bx + cos*bz
		world[8]  = -sinT * bx[0] + cosT * bz[0];
		world[9]  = -sinT * bx[1] + cosT * bz[1];
		world[10] = -sinT * bx[2] + cosT * bz[2];
		world[11] = 0.0f;
		// Row 3 = translation
		world[12] = inst_pos[0];
		world[13] = inst_pos[1];
		world[14] = inst_pos[2];
		world[15] = 1.0f;
	}

	HRESULT foliage::draw_instanced_dip(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
		UINT start_idx, UINT prim_count)
	{
		auto& ffp = shared::common::ffp_state::get();

		UINT inst_count = ffp.instance_count();
		int fol_stream = ffp.cur_decl_foliage_stream();
		auto* inst_vb = ffp.stream_vb(fol_stream);
		UINT inst_stride = ffp.stream_stride(fol_stream);
		UINT inst_off = ffp.stream_offset(fol_stream);
		int tc1_off = ffp.cur_decl_foliage_tc1_off();
		int tc2_off = ffp.cur_decl_foliage_tc2_off();
		int tc3_off = ffp.cur_decl_foliage_tc3_off();

		if (!inst_vb || inst_stride < 12 || inst_count == 0)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		ensure_ffp_decl(dev);
		if (!s_ffp_decl)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		// Lock instance VB
		unsigned char* inst_data = nullptr;
		if (FAILED(inst_vb->Lock(inst_off, inst_count * inst_stride,
			reinterpret_cast<void**>(&inst_data), D3DLOCK_READONLY)))
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		// Disable hardware instancing
		dev->SetStreamSourceFreq(0, 1);
		dev->SetStreamSourceFreq(1, 1);
		dev->SetStreamSource(1, nullptr, 0, 0);
		// Also stream 2 if present
		if (ffp.stream_vb(2))
		{
			dev->SetStreamSourceFreq(2, 1);
			dev->SetStreamSource(2, nullptr, 0, 0);
		}

		// Switch to FFP
		ffp.engage(dev);

		// Use stream-0-only declaration
		IDirect3DVertexDeclaration9* orig_decl = nullptr;
		dev->GetVertexDeclaration(&orig_decl);
		dev->SetVertexDeclaration(s_ffp_decl);

		ffp.setup_albedo_texture(dev);

		// Draw each instance with its own World matrix
		HRESULT hr = S_OK;
		for (UINT i = 0; i < inst_count; i++)
		{
			const unsigned char* inst = inst_data + i * inst_stride;
			auto* pos  = reinterpret_cast<const float*>(inst + tc1_off);
			auto* ori  = reinterpret_cast<const float*>(inst + tc2_off);
			auto* wind = reinterpret_cast<const float*>(inst + tc3_off);

			D3DMATRIX world_mat = {};
			build_world_matrix(pos, ori, wind, reinterpret_cast<float*>(&world_mat));
			dev->SetTransform(D3DTS_WORLD, &world_mat);

			hr = dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		inst_vb->Unlock();

		// Restore
		dev->SetVertexDeclaration(orig_decl);
		if (orig_decl) orig_decl->Release();

		// Restore stream frequencies and bindings
		dev->SetStreamSourceFreq(0, ffp.stream_freq(0));
		dev->SetStreamSourceFreq(1, ffp.stream_freq(1));
		dev->SetStreamSource(1, ffp.stream_vb(1), ffp.stream_offset(1), ffp.stream_stride(1));
		if (ffp.stream_vb(2))
		{
			dev->SetStreamSourceFreq(2, ffp.stream_freq(2));
			dev->SetStreamSource(2, ffp.stream_vb(2), ffp.stream_offset(2), ffp.stream_stride(2));
		}

		ffp.restore_textures(dev);
		return hr;
	}
}
