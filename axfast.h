/* axfast.h - aceapex-fast: the FAST profile of ACEAPEX as a C99 library in two files (axfast.h, axfast.c).
 * Format: ACEPX2 header version 3, profile 1 (docs/FORMAT_FAST.md); archives are byte-identical to libaceapex at
 * levels 11..18 (axfast levels 1..8) and each library reads the other's. No dependency, no threads, no global state;
 * every call may run concurrently with any other. Byte order: little-endian fields on every host (big-endian included).
 *
 * Results: bytes written (>= 0), or one of the errors below (the same values as aceapex.h).
 * An empty input compresses to the 68-byte empty archive of libaceapex (header version 2, no blocks). */
#ifndef AXFAST_H
#define AXFAST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AXFAST_VERSION_STRING "0.1.0"
#define AXFAST_ERR_BUFFER  -1     /* dst too small */
#define AXFAST_ERR_DATA    -2     /* not a valid archive, a bad level, or a range outside the original */
#define AXFAST_ERR_MEMORY  -3     /* scratch allocation failed */
#define AXFAST_LEVEL_MIN    1
#define AXFAST_LEVEL_MAX    8

/* worst-case archive size for n input bytes */
size_t  axfast_bound(size_t n);
/* n bytes at src -> archive at dst (cap bytes); level 1..8 (1: fastest encoder, 8: densest, optimal parse) */
int64_t axfast_compress(const void* src, size_t n, void* dst, size_t cap, int level);
/* original size stored in an archive (n bytes at src), or AXFAST_ERR_DATA */
int64_t axfast_original_size(const void* src, size_t n);
/* whole archive -> dst (cap >= original size); returns the original size */
int64_t axfast_decompress(const void* src, size_t n, void* dst, size_t cap);
/* bytes [off, off + len) of the original -> dst (cap >= len); decodes only the blocks that cover the range */
int64_t axfast_decompress_range(const void* src, size_t n, uint64_t off, uint64_t len, void* dst, size_t cap);
/* XXH3-64 (seed 0) of n bytes: the header checksum field; callers can verify a decode against it */
uint64_t axfast_xxh3(const void* p, size_t n);
/* the checksum field of an archive's header (version 2 or 3), or 0 when n < 68 */
uint64_t axfast_stored_xxh3(const void* src, size_t n);

#ifdef __cplusplus
}
#endif
#endif
