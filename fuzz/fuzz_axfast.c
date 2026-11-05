/* fuzz_axfast.c - libFuzzer harness over the C API (ClusterFuzzLite), the logic of tests/fuzz.c driven by the fuzzer.
 * Byte 0 picks the path:
 *   bit 7 = 0, raw path: the rest is an archive. axfast_original_size; when it accepts (<= 16 MiB) axfast_decompress
 *      into a heap buffer of exactly that size, then 2 ranges (head, middle) into exact buffers. Only the sanitizers
 *      judge (ASan/UBSan): the plain decode has no checksum, a wrong-bytes success is allowed here.
 *   bit 7 = 1, VERIFY path: byte 0 & 7 picks the level (1..8), bytes 1..24 are 8 edits (3 bytes each: offset lo,
 *      offset hi, xor mask; offset counted after the 68-byte header, mask 0 = no edit), byte 25 a cut (0 = none, else
 *      keep size * byte / 256), the rest the original (<= 256 KiB). The original is compressed, the edits applied, the
 *      archive decoded; a decode whose XXH3 equals the header's (the verify=True path of the binding) must give exactly
 *      the original bytes, else abort() (the fuzzer reports it). -DFUZZ_AXFAST_CONTROL skips the XXH3 check: the same
 *      assert must then fire (negative control). */
#include "axfast.h"
#include <stdlib.h>
#include <string.h>
static void raw_decode(const unsigned char* z, size_t zn) {
    const int64_t claimed = axfast_original_size(z, zn);
    unsigned char* o; size_t len;
    if (claimed < 0 || claimed > (16 << 20)) return;
    o = (unsigned char*)malloc(claimed ? (size_t)claimed : 1);
    (void)axfast_decompress(z, zn, o, (size_t)claimed);
    free(o);
    len = claimed < 4096 ? (size_t)claimed : 4096;
    o = (unsigned char*)malloc(len ? len : 1);
    (void)axfast_decompress_range(z, zn, 0, len, o, len);
    (void)axfast_decompress_range(z, zn, (uint64_t)claimed / 2, len / 2, o, len / 2);
    free(o);
}
int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size) {
    const unsigned char* in; size_t n, zn, cap; unsigned char *z, *o; int64_t zs, v; int e;
    if (size < 1) return 0;
    if (!(data[0] & 0x80)) { raw_decode(data + 1, size - 1); return 0; }
    if (size < 26) return 0;
    in = data + 26; n = size - 26; if (n > (256u << 10)) n = 256u << 10;
    cap = axfast_bound(n); z = (unsigned char*)malloc(cap);
    zs = axfast_compress(in, n, z, cap, 1 + (data[0] & 7));
    if (zs <= 0) abort();                                       /* a valid input must compress */
    zn = (size_t)zs;
    for (e = 0; e < 8; e++) {
        const unsigned char* q = data + 1 + 3 * e; if (!q[2] || zn <= 68) continue;
        z[68 + ((size_t)q[0] | (size_t)q[1] << 8) % (zn - 68)] ^= q[2];
    }
    if (data[25]) zn = zn * data[25] / 256;
    raw_decode(z, zn);
    o = (unsigned char*)malloc(n ? n : 1);
    v = axfast_decompress(z, zn, o, n);
#ifndef FUZZ_AXFAST_CONTROL
    if (v >= 0 && axfast_xxh3(o, (size_t)v) != axfast_stored_xxh3(z, zn)) v = -1;   /* verify=True rejects it */
#endif
    if (v >= 0 && (v != (int64_t)n || memcmp(o, in, n))) abort();               /* verified success, other bytes */
    free(o); free(z);
    return 0;
}
