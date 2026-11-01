/* Local experimental capture path, generated with LLM assistance.
 * Copies the measured picture region directly to a frame slot; it does not
 * modify the Canon buffer or repack a previously captured full-width slot.
 * Timing on the actual 7D2 must be validated before production use.
 */
#ifndef MLV_LITE_ACTIVE_7D2_H
#define MLV_LITE_ACTIVE_7D2_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ACTIVE_7D2_LEFT 144
#define ACTIVE_7D2_RIGHT 1976
#define ACTIVE_7D2_WIDTH (ACTIVE_7D2_RIGHT - ACTIVE_7D2_LEFT)
#define ACTIVE_7D2_SOURCE_PITCH 3472
#define ACTIVE_7D2_PITCH (ACTIVE_7D2_WIDTH * 14 / 8)

/* Source and destination are distinct, live allocations owned by the recorder.
 * Explicit capacities permit the host test to check the same bounds as capture.
 * Even horizontal/vertical offsets retain the RGGB Bayer phase.
 */
static inline int active_7d2_copy(void *dst, size_t dst_bytes,
                                const void *src, size_t src_bytes,
                                int source_height, int y, int height)
{
    if (!dst || !src || dst == src || source_height <= 0 ||
        source_height > 4096 || y < 0 || height <= 0 || (y & 1) ||
        (height & 7) || y > source_height || height > source_height - y ||
        src_bytes < (size_t) source_height * ACTIVE_7D2_SOURCE_PITCH ||
        dst_bytes < (size_t) height * ACTIVE_7D2_PITCH)
        return 0;

    const uint8_t *in = (const uint8_t *)src +
        (size_t)y * ACTIVE_7D2_SOURCE_PITCH + ACTIVE_7D2_LEFT * 14 / 8;
    uint8_t *out = dst;
    for (int row = 0; row < height; row++)
    {
        memcpy(out, in, ACTIVE_7D2_PITCH);
        in += ACTIVE_7D2_SOURCE_PITCH;
        out += ACTIVE_7D2_PITCH;
    }
    return 1;
}
#endif
