#include "std_include.hpp"
#include "renderer.hpp"

#include "imgui.hpp"
#include "diagnostics.hpp"
#include "skinning.hpp"
#include "foliage.hpp"
#include "terrain.hpp"
#include "spray.hpp"
#include "particle.hpp"
#include "lighting.hpp"
#include "shared/common/ffp_state.hpp"

namespace comp
{
	namespace tex_addons
	{
		bool initialized = false;
		LPDIRECT3DTEXTURE9 icon = nullptr;

		void init_texture_addons(bool release)
		{
			if (release)
			{
				if (tex_addons::icon) tex_addons::icon->Release();
				return;
			}

			const auto dev = shared::globals::d3d_device;
			const char* icon_path = "rtx_comp\\textures\\icon.png";

			// Only load if the file exists — no icon is shipped by default
			if (GetFileAttributesA(icon_path) != INVALID_FILE_ATTRIBUTES)
			{
				HRESULT hr = D3DXCreateTextureFromFileA(dev, icon_path, &tex_addons::icon);
				if (FAILED(hr))
					shared::common::log("Renderer", std::format("Failed to load {}", icon_path), shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
			}

			tex_addons::initialized = true;
		}
	}


	// ----

	drawcall_mod_context& setup_context(IDirect3DDevice9* dev)
	{
		auto& ctx = renderer::dc_ctx;
		ctx.info.device_ptr = dev;
		return ctx;
	}


	// ----

	HRESULT renderer::on_draw_primitive(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType, const UINT& StartVertex, const UINT& PrimitiveCount)
	{
		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		lighting::ensure_lights_applied();

		static auto im = imgui::get();
		im->m_stats._drawcall_prim_incl_ignored.track_single();

		auto& ctx = setup_context(dev);
		auto& ffp = shared::common::ffp_state::get();
		ffp.increment_draw_count();

		auto hr = S_OK;

		if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.last_decl() && !ffp.cur_decl_has_pos_t() &&
			(ffp.cur_decl_has_position1() || ffp.cur_decl_is_skinned() ||
			 ffp.cur_decl_has_tangent()))
		{
			// Skinned/morphable/normal-mapped objects: keep original VS.
			// Normal-mapped draws (TANGENT) use multi-pass rendering that
			// breaks if FFP strips their shaders.
			ffp.disengage(dev);
			dev->SetTransform(D3DTS_WORLD,
				reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
			ffp.apply_camera_transforms(dev);
			hr = dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
			ffp.mark_transforms_dirty();
			im->m_stats._drawcall_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.last_decl() && !ffp.cur_decl_has_pos_t() &&
			ffp.cur_decl_is_billboard_particle() && particle::is_available())
		{
			hr = particle::get()->draw_particle_dp(dev, PrimitiveType, StartVertex, PrimitiveCount);
			im->m_stats._drawcall_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.last_decl() && !ffp.cur_decl_has_pos_t() &&
			ffp.cur_decl_is_spray() && spray::is_available())
		{
			hr = spray::get()->draw_spray_dp(dev, PrimitiveType, StartVertex, PrimitiveCount);
			im->m_stats._drawcall_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.last_decl() && !ffp.cur_decl_has_pos_t() &&
			!ffp.cur_decl_is_skinned() && !ffp.cur_decl_has_tangent() &&
			ffp.cur_decl_has_normal() && !ffp.cur_decl_has_binormal() &&
			!ffp.cur_draw_is_water() && ffp.cur_decl_has_texcoord())
		{
			ffp.engage(dev);
			ffp.setup_albedo_texture(dev);

			hr = dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
			im->m_stats._drawcall_prim.track_single();

			ffp.restore_textures(dev);
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.last_decl() && !ffp.cur_decl_has_pos_t() &&
			!ffp.cur_decl_has_texcoord() && ffp.cur_decl_has_normal())
		{
			hr = terrain::draw_terrain_dp(dev, PrimitiveType, StartVertex, PrimitiveCount);
			im->m_stats._drawcall_prim.track_single();
		}
		else
		{
			ffp.disengage(dev);
			// Provide D3D transform hints for shader-based draws so Remix
			// can position geometry correctly for raytracing.
			if (ffp.is_enabled() && ffp.view_proj_valid() && !ffp.cur_decl_has_pos_t())
			{
				dev->SetTransform(D3DTS_WORLD,
					reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
				ffp.apply_camera_transforms(dev);
			}
			hr = dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
			if (ffp.is_enabled() && ffp.view_proj_valid() && !ffp.cur_decl_has_pos_t())
				ffp.mark_transforms_dirty();
			im->m_stats._drawcall_prim.track_single();
			im->m_stats._drawcall_using_vs.track_single();
		}

		if (auto* d = diagnostics::get()) d->on_draw_primitive(ffp.draw_call_count(), PrimitiveType, StartVertex, PrimitiveCount);

		ctx.restore_all(dev);
		ctx.reset_context();

		return hr;
	}


	// ----

	HRESULT renderer::on_draw_indexed_prim(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType, const INT& BaseVertexIndex, const UINT& MinVertexIndex, const UINT& NumVertices, const UINT& startIndex, const UINT& primCount)
	{
		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
		}

		lighting::ensure_lights_applied();

		auto& ctx = setup_context(dev);
		const auto im = imgui::get();
		auto& ffp = shared::common::ffp_state::get();
		ffp.increment_draw_count();

		im->m_stats._drawcall_indexed_prim_incl_ignored.track_single();

		if (ctx.modifiers.do_not_render)
		{
			ctx.restore_all(dev);
			ctx.reset_context();
			return S_OK;
		}

		auto hr = S_OK;

		/*
		 * FFP draw routing for indexed draws:
		 *   Instanced foliage (TC1/TC2/TC3 on stream 1) → per-instance World loop
		 *   Skinned (>8 bones) + skinning module → CPU skinning
		 *   Skinned/Morphable/Normal-mapped → shader passthrough with transform hints
		 *   Spray billboard (Pos+Color+TC0-3, no normal) → CPU billboard expansion
		 *   Non-skinned + NORMAL + UV (no tangent) → FFP rigid draw
		 *   Terrain (NORMAL but no UV) → shader passthrough with identity World
		 *   Everything else → shader passthrough
		 */
		if (ffp.is_enabled() && ffp.view_proj_valid() && ffp.is_instanced() &&
			ffp.cur_decl_is_foliage() && !ffp.cur_decl_has_pos_t())
		{
			hr = foliage::draw_instanced_dip(dev, PrimitiveType, BaseVertexIndex,
				MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.cur_decl_is_skinned() && ffp.num_bones() > 8 &&
			!ffp.cur_decl_has_pos_t() && skinning::is_available())
		{
			hr = skinning::get()->draw_skinned_dip(dev, PrimitiveType, BaseVertexIndex,
				MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			!ffp.cur_decl_has_pos_t() &&
			(ffp.cur_decl_has_position1() || ffp.cur_decl_is_skinned() ||
			 ffp.cur_decl_has_tangent()))
		{
			// Skinned/morphable/normal-mapped objects: keep original VS.
			// Normal-mapped draws (TANGENT) use multi-pass rendering that
			// breaks if FFP strips their shaders.
			ffp.disengage(dev);
			dev->SetTransform(D3DTS_WORLD,
				reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
			ffp.apply_camera_transforms(dev);
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			ffp.mark_transforms_dirty();
			im->m_stats._drawcall_indexed_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			!ffp.cur_decl_has_pos_t() && ffp.cur_decl_is_spray() &&
			spray::is_available())
		{
			hr = spray::get()->draw_spray_dip(dev, PrimitiveType, BaseVertexIndex,
				MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			!ffp.cur_decl_is_skinned() && !ffp.cur_decl_has_tangent() &&
			!ffp.cur_decl_has_pos_t() &&
			ffp.cur_decl_has_normal() && !ffp.cur_decl_has_binormal() &&
			!ffp.cur_draw_is_water() && ffp.cur_decl_has_texcoord())
		{
			ffp.engage(dev);
			ffp.setup_albedo_texture(dev);

			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();

			ffp.restore_textures(dev);
		}
		else if (ffp.is_enabled() && ffp.view_proj_valid() &&
			!ffp.cur_decl_has_pos_t() && !ffp.cur_decl_has_texcoord() &&
			ffp.cur_decl_has_normal())
		{
			hr = terrain::draw_terrain_dip(dev, PrimitiveType, BaseVertexIndex,
				MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();
		}
		else
		{
			ffp.disengage(dev);
			if (ffp.is_enabled() && ffp.view_proj_valid() && !ffp.cur_decl_has_pos_t())
			{
				dev->SetTransform(D3DTS_WORLD,
					reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
				ffp.apply_camera_transforms(dev);
			}
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			if (ffp.is_enabled() && ffp.view_proj_valid() && !ffp.cur_decl_has_pos_t())
				ffp.mark_transforms_dirty();
			im->m_stats._drawcall_indexed_prim.track_single();
			im->m_stats._drawcall_indexed_prim_using_vs.track_single();
		}

		if (auto* d = diagnostics::get()) d->on_draw_indexed_prim(ffp.draw_call_count(), dev, PrimitiveType, BaseVertexIndex, NumVertices, primCount);

		ctx.restore_all(dev);
		ctx.reset_context();

		return hr;
	}

	// ---

	HRESULT renderer::on_draw_primitive_up(IDirect3DDevice9* dev, D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
	{
		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
		}

		auto& ffp = shared::common::ffp_state::get();
		ffp.increment_draw_count();

		if (ffp.is_enabled() && ffp.view_proj_valid() &&
			!ffp.cur_decl_has_pos_t() && ffp.cur_decl_has_normal() &&
			!ffp.cur_decl_has_binormal() && !ffp.cur_draw_is_water() &&
			ffp.cur_decl_has_texcoord())
		{
			ffp.engage(dev);
			ffp.setup_albedo_texture(dev);
			auto hr = dev->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
			ffp.restore_textures(dev);
			return hr;
		}

		ffp.disengage(dev);
		return dev->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
	}

	HRESULT renderer::on_draw_indexed_prim_up(IDirect3DDevice9* dev, D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
	{
		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
		}

		auto& ffp = shared::common::ffp_state::get();
		ffp.increment_draw_count();

		if (ffp.is_enabled() && ffp.view_proj_valid() &&
			!ffp.cur_decl_has_pos_t() && ffp.cur_decl_has_normal() &&
			!ffp.cur_decl_has_binormal() && !ffp.cur_draw_is_water() &&
			ffp.cur_decl_has_texcoord())
		{
			ffp.engage(dev);
			ffp.setup_albedo_texture(dev);
			auto hr = dev->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
			ffp.restore_textures(dev);
			return hr;
		}

		ffp.disengage(dev);
		return dev->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
	}

	// ---

	void renderer::manually_trigger_remix_injection(IDirect3DDevice9* dev)
	{
		if (!m_triggered_remix_injection)
		{
			auto& ctx = dc_ctx;

			dev->SetRenderState(D3DRS_FOGENABLE, FALSE);

			ctx.save_vs(dev);
			dev->SetVertexShader(nullptr);
			ctx.save_ps(dev);
			dev->SetPixelShader(nullptr);

			ctx.save_rs(dev, D3DRS_ZWRITEENABLE);
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

			IDirect3DVertexDeclaration9* saved_decl = nullptr;
			dev->GetVertexDeclaration(&saved_decl);
			dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

			struct CUSTOMVERTEX
			{
				float x, y, z, rhw;
				D3DCOLOR color;
			};

			const auto color = D3DCOLOR_COLORVALUE(0, 0, 0, 0);
			const auto w = -0.49f;
			const auto h = -0.495f;

			CUSTOMVERTEX vertices[] =
			{
				{ -0.5f, -0.5f, 0.0f, 1.0f, color },
				{     w, -0.5f, 0.0f, 1.0f, color },
				{ -0.5f,     h, 0.0f, 1.0f, color },
				{     w,     h, 0.0f, 1.0f, color }
			};

			dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(CUSTOMVERTEX));

			if (saved_decl)
			{
				dev->SetVertexDeclaration(saved_decl);
				saved_decl->Release();
			}

			ctx.restore_vs(dev);
			ctx.restore_ps(dev);
			ctx.restore_render_state(dev, D3DRS_ZWRITEENABLE);
			m_triggered_remix_injection = true;
		}
	}


	renderer::renderer()
	{
		p_this = this;

		// Initialize FFP state tracker
		shared::common::ffp_state::get().init(shared::globals::d3d_device);

		// GAME-SPECIFIC: Create hooks as required.
		// See documentation for per-object hook examples.

		m_initialized = true;
		shared::common::log("Renderer", "Module initialized.", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
	}

	renderer::~renderer()
	{
		tex_addons::init_texture_addons(true);
		p_this = nullptr;
	}
}
