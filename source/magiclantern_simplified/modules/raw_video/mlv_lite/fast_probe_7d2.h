/* Local LLM-assisted diagnostic, not an upstream contribution.
 * Canon 7D2 1.1.2 descriptors verified from ROM, 0xfe0fef46:
 * descriptor stride 12, destination +0, source +4, length +8.
 * 0xfe0ff00a rejects counts above 8. No sensor buffers may be used here.
 */
#ifndef FAST_PROBE_7D2_H
#define FAST_PROBE_7D2_H
#include <stddef.h>
#include <stdint.h>
struct fast_7d2_segment { void *dst; const void *src; uint32_t bytes; };
typedef int (*fast_7d2_transfer)(const struct fast_7d2_segment *, unsigned int);
static inline int fast_7d2_overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn)
{
    return a <= b ? b - a < an : a - b < bn;
}

/* Both allocations are caller-owned, disjoint and stable for the whole test.
 * Source uses the real 3472-byte row spacing; destination is packed.
 * Returns 0 only after all output bytes and both 64-byte guards match.
 */
static inline int fast_7d2_probe(void *dst, size_t dst_capacity,
                               void *src, size_t src_capacity,
                               unsigned int rows, unsigned int width,
                               fast_7d2_transfer transfer)
{
    if (!dst || !src || !transfer || rows == 0 || rows > 8 ||
        width == 0 || width > 3206 ||
        dst_capacity < (size_t)rows * width + 128 ||
        src_capacity < (size_t)rows * 3472 ||
        fast_7d2_overlap((uintptr_t)src, rows * 3472,
                         (uintptr_t)dst, rows * width + 128))
        return -1000;
    uint8_t *out = dst, *in = src;
    for (size_t i = 0; i < (size_t)rows * 3472; i++)
        in[i] = (uint8_t)(i * 37 + i / 257 + 81);
    for (size_t i = 0; i < (size_t)rows * width + 128; i++)
        out[i] = 0xa5;
    struct fast_7d2_segment list[8];
    for (unsigned int row = 0; row < rows; row++)
    {
        list[row].dst = out + 64 + row * width;
        list[row].src = in + 252 + row * 3472;
        list[row].bytes = width;
    }
    int rc = transfer(list, rows);
    if (rc != 0) return rc;
    for (unsigned int i = 0; i < 64; i++)
        if (out[i] != 0xa5 || out[64 + rows * width + i] != 0xa5)
            return -1001;
    for (unsigned int row = 0; row < rows; row++)
        for (unsigned int x = 0; x < width; x++)
        {
            size_t i = 252 + row * 3472 + x;
            if (out[64 + row * width + x] != (uint8_t)(i * 37 + i / 257 + 81))
                return -1002;
        }
    return 0;
}
/* Bounded full-frame transfer, at most 131 calls for the measured readout.
 * No pixel processing: only selected packed 14-bit rows are transferred. */
static inline int fast_7d2_frame(void *dst, size_t dst_capacity,
                               const void *src, size_t src_capacity,
                               int source_height, int y, int height,
                               fast_7d2_transfer transfer)
{
    if (!dst || !src || !transfer || source_height <= 0 || source_height > 4096 ||
        y < 0 || (y & 1) || height <= 0 || height > 1048 || (height & 7) ||
        y > source_height || height > source_height - y ||
        src_capacity < (size_t)source_height * 3472 ||
        dst_capacity < (size_t)height * 3206 ||
        fast_7d2_overlap((uintptr_t)src, src_capacity,
                         (uintptr_t)dst, (size_t)height * 3206))
        return -1000;
    for (int row = 0; row < height; row += 8)
    {
        struct fast_7d2_segment list[8];
        for (int i = 0; i < 8; i++)
        {
            list[i].dst = (uint8_t *)dst + (size_t)(row + i) * 3206;
            list[i].src = (const uint8_t *)src + (size_t)(y + row + i) * 3472 + 252;
            list[i].bytes = 3206;
        }
        int rc = transfer(list, 8);
        if (rc != 0) return rc;
    }
    return 0;
}
#endif
