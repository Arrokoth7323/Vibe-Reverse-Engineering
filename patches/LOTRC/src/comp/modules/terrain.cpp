#include "std_include.hpp"
#include "terrain.hpp"

#include "shared/common/ffp_state.hpp"

namespace comp
{
	HRESULT terrain::draw_terrain_dip(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
		UINT start_idx, UINT prim_count)
	{
		auto& ffp = shared::common::ffp_state::get();

		// Restore original VS/PS — full shader pipeline for 12-texture compositing
		ffp.disengage(dev);

		// Identity WORLD (terrain positions are world-space, no object transform).
		// Correct camera VIEW/PROJ so Remix knows camera position for terrain baker.
		// Terrain VS reads camera from c239-c242 constants, these D3D transforms
		// only affect Remix's positioning — not the shader output.
		static const D3DMATRIX identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		dev->SetTransform(D3DTS_WORLD, &identity);
		ffp.apply_camera_transforms(dev);

		HRESULT hr = dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx,
			num_verts, start_idx, prim_count);

		ffp.mark_transforms_dirty();

		return hr;
	}

	HRESULT terrain::draw_terrain_dp(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, UINT start_vtx, UINT prim_count)
	{
		auto& ffp = shared::common::ffp_state::get();

		ffp.disengage(dev);

		static const D3DMATRIX identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		dev->SetTransform(D3DTS_WORLD, &identity);
		ffp.apply_camera_transforms(dev);

		HRESULT hr = dev->DrawPrimitive(pt, start_vtx, prim_count);

		ffp.mark_transforms_dirty();

		return hr;
	}
}
