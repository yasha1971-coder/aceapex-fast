/* rt.c - fastlib round trip for one file: levels 1..8, archive size, FNV-1a of the archive, decode == input, range of
 * the middle third == slice. Plain C99 + axfast only (builds for any target: qemu-s390x, -m32). Usage: rt FILE */
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
    FILE* f; long n; unsigned char *in, *z, *o; int lv, bad = 0;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) return 2;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    in = (unsigned char*)malloc((size_t)n + 1); o = (unsigned char*)malloc((size_t)n + 1); z = (unsigned char*)malloc(axfast_bound((size_t)n));
    if (!in || !o || !z || (n && fread(in, 1, (size_t)n, f) != (size_t)n)) return 2;
    fclose(f);
    for (lv = 1; lv <= 8; lv++) {
        const int64_t zs = axfast_compress(in, (size_t)n, z, axfast_bound((size_t)n), lv);
        const int64_t d = zs > 0 ? axfast_decompress(z, (size_t)zs, o, (size_t)n) : -9;
        const uint64_t off = (uint64_t)n / 3, len = (uint64_t)n / 3;
        const int64_t r = zs > 0 ? axfast_decompress_range(z, (size_t)zs, off, len, o, (size_t)n) : -9;
        unsigned long long h = 1469598103934665603ull; int64_t i; int ok;
        for (i = 0; i < zs; i++) h = (h ^ z[i]) * 1099511628211ull;
        ok = d == n && r == (int64_t)len && (len == 0 || !memcmp(o, in + off, (size_t)len));
        if (d == n && n && axfast_decompress(z, (size_t)zs, o, (size_t)n) == n && memcmp(o, in, (size_t)n)) ok = 0;
        bad += !ok;
        printf("L%d %lld B fnv %016llx rt %s\n", lv, (long long)zs, h, ok ? "ok" : "FAIL");
    }
    free(in); free(o); free(z);
    return bad != 0;
}
