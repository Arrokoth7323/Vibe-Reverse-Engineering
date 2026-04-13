#ifndef SHARED_LIGHT_DATA_H_
#define SHARED_LIGHT_DATA_H_

#include <stdint.h>

#define SHMEM_LIGHT_NAME "Local\\RemixLightData_LOTRC"
#define SHMEM_MAX_LIGHTS 768

#pragma pack(push, 1)

struct shared_point_light {
    float    pos[3];
    float    col[3];
    float    radius;       /* game outer attenuation radius (world units) */
    float    linear_att;   /* game linear attenuation factor */
    float    quad_att;     /* game quadratic attenuation factor */
    uint64_t stable_hash;  /* FNV-1a of quantized position — stable across reorders */
};
/* 9 floats (36) + 8 bytes hash = 44 bytes; packed */

struct shared_light_data {
    volatile uint32_t write_seq;        /* seqlock: odd = write in progress */
    uint32_t          frame_id;
    uint32_t          light_count;      /* 0..SHMEM_MAX_LIGHTS */
    volatile uint32_t server_active;    /* set to 1 by 64-bit DLL when consuming */
    uint32_t          _pad[12];         /* align header to 64 bytes */
    struct shared_point_light lights[SHMEM_MAX_LIGHTS];
};

#pragma pack(pop)

#endif /* SHARED_LIGHT_DATA_H_ */
