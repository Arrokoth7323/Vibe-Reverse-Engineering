#pragma once

namespace comp
{
	/*
	 * Particle passthrough module.
	 *
	 * Particles are CPU-expanded quads — the game writes pre-transformed billboard
	 * corner positions into dynamic VBs (c243/c244 camera vectors are never written
	 * for particle draws). The VS just does world*viewproj on the corners.
	 *
	 * This module provides shader passthrough with D3D transform hints so Remix
	 * knows the camera setup for raytracing. Without transform hints, Remix sees
	 * the geometry but can't position it correctly in the raytraced scene.
	 *
	 * Detection: has TEXCOORD4+ (pivot/sunScale/alphaRef), distinguishing billboard
	 * particle vertex declarations from spray (which only has TC0-TC3).
	 */
	class particle final : public shared::common::loader::component_module
	{
	public:
		particle();
		~particle();

		static inline particle* p_this = nullptr;
		static particle* get() { return p_this; }

		static bool is_available()
		{
			return p_this != nullptr;
		}

		HRESULT draw_particle_dp(IDirect3DDevice9* dev,
			D3DPRIMITIVETYPE pt, UINT start_vtx, UINT prim_count);

		void on_reset();
	};
}
