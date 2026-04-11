#include "std_include.hpp"
#include "lighting.hpp"

#include "shared/common/ffp_state.hpp"
#include "shared/common/shared_light_data.h"
#include "shared/globals.hpp"

#include <cstring>
#include <cmath>
#include <cstdint>

namespace comp
{

	// Static member definitions
	lighting::tracked_light lighting::s_tracked[MAX_TRACKED] = {};
	int  lighting::s_tracked_count = 0;
	void* lighting::s_shmem_handle = nullptr;
	shared_light_data* lighting::s_shm = nullptr;

	int lighting::active_point_count() { return s_tracked_count; }
	void lighting::on_begin_scene() {}
	void lighting::ensure_lights_applied() {}
	void lighting::draw_light_instances() {}

	// FNV-1a hash of quantized position — same 2-unit grid as accumulator dedup
	static uint64_t position_hash(const float pos[3])
	{
		constexpr float QUANT = 0.5f;
		int32_t qx = static_cast<int32_t>(std::floor(pos[0] * QUANT + 0.5f));
		int32_t qy = static_cast<int32_t>(std::floor(pos[1] * QUANT + 0.5f));
		int32_t qz = static_cast<int32_t>(std::floor(pos[2] * QUANT + 0.5f));

		uint64_t h = 14695981039346656037ULL;
		auto mix = [&](int32_t v) {
			for (int b = 0; b < 4; b++) {
				h ^= static_cast<uint8_t>(v >> (b * 8));
				h *= 1099511628211ULL;
			}
		};
		mix(qx); mix(qy); mix(qz);
		return h;
	}

	void lighting::init_shared_memory()
	{
		if (s_shm) return;

		HANDLE h = CreateFileMappingA(
			INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
			0, sizeof(shared_light_data), SHMEM_LIGHT_NAME);

		if (!h)
		{
			shared::common::log("Lighting",
				std::format("CreateFileMapping failed: {}", GetLastError()));
			return;
		}

		auto* ptr = static_cast<shared_light_data*>(
			MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_light_data)));

		if (!ptr)
		{
			shared::common::log("Lighting",
				std::format("MapViewOfFile failed: {}", GetLastError()));
			CloseHandle(h);
			return;
		}

		std::memset(ptr, 0, sizeof(shared_light_data));
		s_shmem_handle = h;
		s_shm = ptr;
		shared::common::log("Lighting", "Shared memory created for 64-bit bridge");
	}

	void lighting::write_shared_memory()
	{
		if (!s_shm) return;

		// Seqlock: mark write-in-progress
		InterlockedIncrement(reinterpret_cast<volatile LONG*>(&s_shm->write_seq));

		int count = s_tracked_count < SHMEM_MAX_LIGHTS ? s_tracked_count : SHMEM_MAX_LIGHTS;
		s_shm->light_count = static_cast<uint32_t>(count);

		for (int i = 0; i < count; i++)
		{
			auto& dst = s_shm->lights[i];
			auto& src = s_tracked[i];
			dst.pos[0] = src.pos[0]; dst.pos[1] = src.pos[1]; dst.pos[2] = src.pos[2];
			dst.col[0] = src.col[0]; dst.col[1] = src.col[1]; dst.col[2] = src.col[2];
			dst.radius     = src.radius;
			dst.linear_att = src.linear_att;
			dst.quad_att   = src.quad_att;
			dst.stable_hash = src.stable_hash;
		}

		s_shm->frame_id++;

		// Seqlock: mark write-complete
		InterlockedIncrement(reinterpret_cast<volatile LONG*>(&s_shm->write_seq));
	}

	void lighting::update_lights()
	{
		auto& ffp = shared::common::ffp_state::get();
		init_shared_memory();

		// --- Step 1: Mark all tracked lights inactive ---
		for (int i = 0; i < s_tracked_count; i++)
			s_tracked[i].active = false;

		// --- Step 2: Ingest accumulator into tracked table ---
		const auto* accum = ffp.accumulated_lights();
		int accum_count = ffp.accumulated_light_count();

		for (int a = 0; a < accum_count; a++)
		{
			uint64_t hash = position_hash(accum[a].pos);
			float outerR = accum[a].att[1];
			float radius = outerR > 0.1f ? outerR : 25.0f;

			// Search for existing tracked light with same hash
			int found = -1;
			for (int i = 0; i < s_tracked_count; i++)
			{
				if (s_tracked[i].stable_hash == hash)
				{
					found = i;
					break;
				}
			}

			if (found >= 0)
			{
				// Update existing — refresh data, reset TTL
				auto& t = s_tracked[found];
				t.pos[0] = accum[a].pos[0]; t.pos[1] = accum[a].pos[1]; t.pos[2] = accum[a].pos[2];
				t.col[0] = accum[a].col[0]; t.col[1] = accum[a].col[1]; t.col[2] = accum[a].col[2];
				t.radius     = radius;
				t.linear_att = accum[a].att[2];
				t.quad_att   = accum[a].att[3];
				t.ttl    = GRACE_FRAMES;
				t.active = true;
			}
			else if (s_tracked_count < MAX_TRACKED)
			{
				// Insert new
				auto& t = s_tracked[s_tracked_count++];
				t.pos[0] = accum[a].pos[0]; t.pos[1] = accum[a].pos[1]; t.pos[2] = accum[a].pos[2];
				t.col[0] = accum[a].col[0]; t.col[1] = accum[a].col[1]; t.col[2] = accum[a].col[2];
				t.radius      = radius;
				t.linear_att  = accum[a].att[2];
				t.quad_att    = accum[a].att[3];
				t.stable_hash = hash;
				t.ttl    = GRACE_FRAMES;
				t.active = true;
			}
		}

		// --- Step 3: Expire inactive lights ---
		for (int i = 0; i < s_tracked_count; )
		{
			if (!s_tracked[i].active)
			{
				s_tracked[i].ttl--;
				if (s_tracked[i].ttl <= 0)
				{
					// Swap with last and shrink
					s_tracked[i] = s_tracked[--s_tracked_count];
					continue; // re-check swapped element at index i
				}
			}
			i++;
		}

		// --- Step 4: Write to shared memory ---
		write_shared_memory();

		// Diagnostic logging (fires when count changes)
		static int s_last_logged = -1;
		bool server_active = s_shm && s_shm->server_active;
		if (s_tracked_count != s_last_logged)
		{
			s_last_logged = s_tracked_count;
			shared::common::log("Lighting",
				std::format("tracked={} (accum={}) server={}",
					s_tracked_count, accum_count, server_active ? "YES" : "no"));
		}
	}

	void lighting::on_reset()
	{
		s_tracked_count = 0;
		std::memset(s_tracked, 0, sizeof(s_tracked));

		shared::common::ffp_state::get().clear_accumulated_lights();
		shared::common::log("Lighting", "on_reset: cleared all tracked lights");

		if (s_shm)
		{
			InterlockedIncrement(reinterpret_cast<volatile LONG*>(&s_shm->write_seq));
			s_shm->light_count = 0;
			s_shm->frame_id++;
			InterlockedIncrement(reinterpret_cast<volatile LONG*>(&s_shm->write_seq));
		}
	}
}
