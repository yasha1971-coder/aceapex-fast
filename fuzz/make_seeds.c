/* make_seeds.c - seed corpus for fuzz_axfast (input layout: fuzz_axfast.c). Raw-path seeds: byte 0 = 0 and a valid
 * archive (levels 1, 3, 5, 8 of text, DNA lines, zeros, random; 300 B .. 70 KB); VERIFY-path seeds: byte 0 =
 * 0x80 | (level - 1), 25 zero bytes (no edit, no cut), then the original. Usage: make_seeds DIR */
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static void gen(int kind, unsigned char* p, size_t n) {
    static const char* w[] = {"the ", "block ", "archive ", "genome ", "of ", "and\n"}; size_t i;
    for (i = 0; i < n; ) {
        if (kind == 0) { const char* s = w[rnd() % 6]; while (*s && i < n) p[i++] = (unsigned char)*s++; }
        else if (kind == 1) { p[i] = (i % 61 == 60) ? '\n' : (unsigned char)"ACGT"[rnd() & 3]; i++; }
        else if (kind == 2) p[i++] = 0;
        else p[i++] = (unsigned char)rnd();
    }
}
static void put(const char* dir, const char* nm, int k, const unsigned char* a, size_t an, const unsigned char* b, size_t bn) {
    char path[4096]; FILE* f; snprintf(path, sizeof path, "%s/%s%d", dir, nm, k);
    if (!(f = fopen(path, "wb"))) return;
    fwrite(a, 1, an, f); fwrite(b, 1, bn, f); fclose(f);
}
int main(int argc, char** argv) {
    static const int lv[] = {1, 3, 5, 8}; static const size_t sz[] = {300, 70000};
    unsigned char hdr[26]; int kind, s, j, k = 0;
    if (argc < 2) return 1;
    for (kind = 0; kind < 4; kind++) for (s = 0; s < 2; s++) for (j = 0; j < 4; j++) {
        const size_t n = sz[s], cap = axfast_bound(n); unsigned char* in = (unsigned char*)malloc(n); unsigned char* z = (unsigned char*)malloc(cap);
        int64_t zs; gen(kind, in, n); zs = axfast_compress(in, n, z, cap, lv[j]);
        if (zs <= 0) return 1;
        memset(hdr, 0, sizeof hdr); put(argv[1], "raw", k, hdr, 1, z, (size_t)zs);
        hdr[0] = (unsigned char)(0x80 | (lv[j] - 1)); put(argv[1], "verify", k, hdr, 26, in, n);
        free(in); free(z); k++;
    }
    printf("%d raw + %d verify seeds\n", k, k);
    return 0;
}
