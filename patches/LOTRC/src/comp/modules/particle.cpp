#include "std_include.hpp"
#include "particle.hpp"

#include "shared/common/ffp_state.hpp"

namespace comp
{
	particle::particle()
	{
		p_this = this;
		shared::common::log("Particle", "Module initialized (shader passthrough with transform hints).",
			shared::common::LOG_TYPE::LOG_TYPE_WARN);
	}

	particle::~particle()
	{
		on_reset();
		p_this = nullptr;
	}

	HRESULT particle::draw_particle_dp(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, UINT start_vtx, UINT prim_count)
	{
		// Particles are CPU-expanded quads — the vertex buffer already contains
		// world-space corner positions. Just pass through with the original VS/PS
		// and set D3D transform hints so Remix knows the camera setup.
		auto& ffp = shared::common::ffp_state::get();
		ffp.disengage(dev);
		dev->SetTransform(D3DTS_WORLD,
			reinterpret_cast<const D3DMATRIX*>(&ffp.vs_const_data()[178 * 4]));
		ffp.apply_camera_transforms(dev);
		HRESULT hr = dev->DrawPrimitive(pt, start_vtx, prim_count);
		ffp.mark_transforms_dirty();
		return hr;
	}

	void particle::on_reset()
	{
		// Nothing to release — no expanded VBs or custom decls
	}
}
