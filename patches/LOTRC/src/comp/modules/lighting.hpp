#pragma once
#include "shared/common/shared_light_data.h"
#include <cstdint>

namespace comp
{
	// Scene lighting via shared memory → 64-bit remix_lights.dll.
	// Gathers dynamic lights from ffp_state accumulator each frame, maintains
	// a tracked light table with TTL-based grace period to prevent flicker
	// from per-frame culling. Writes to named shared memory consumed by a
	// 64-bit DLL that calls CreateLight/DrawLightInstance directly.
	//
	// Lifecycle: Present → update_lights() → writes shared memory
	class lighting
	{
	public:
		static void update_lights();
		static void ensure_lights_applied();
		static void draw_light_instances();
		static void on_begin_scene();
		static void on_reset();
		static int active_point_count();

	private:
		struct tracked_light {
			float    pos[3];
			float    col[3];
			float    radius;
			float    linear_att;
			float    quad_att;
			uint64_t stable_hash;
			int      ttl;       // frames remaining before expiry
			bool     active;    // seen this frame
		};

		static constexpr int MAX_TRACKED   = 512;
		static constexpr int GRACE_FRAMES  = 90;  // ~3 sec at 30fps

		static tracked_light s_tracked[MAX_TRACKED];
		static int s_tracked_count;

		// Shared memory for 64-bit bridge communication
		static void* s_shmem_handle;
		static struct shared_light_data* s_shm;
		static void init_shared_memory();
		static void write_shared_memory();
	};
}
