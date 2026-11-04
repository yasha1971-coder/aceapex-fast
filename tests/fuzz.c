/* fuzz.c - gate 3 of fastlib step 1 (README): corrupted archives under ASan+UBSan.
 * Valid archives: levels 1..8 of seeded inputs (text, DNA, zeros, random, short periods; 1 B .. 2.2 MiB, 1-3 blocks).
 * Mutants: 1-8 bit flips, byte overwrites, truncation, extension, header field edits (orig, block size, block count,
 * min_match, payload, profile, reserved), block table edits, a random block body, a block header (ntok/next/tail) edit.
 * Each mutant: axfast_original_size; when it accepts (<= 64 MiB) the decode goes into a heap buffer of exactly that size
 * (ASan sees any byte written past it) and must return that size or an error; then 4 ranges into exact buffers.
 * Negative control (mode "control"): a buffer one byte shorter than the original passed with the full capacity - ASan
 * must report it (the caller runs this mode and expects a failure). Usage: fuzz SECONDS | fuzz control */
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static unsigned char* gen(int kind, size_t n) {
    unsigned char* p = (unsigned char*)malloc(n ? n : 1); size_t i; static const char* w[] = {"the ", "block ", "archive ", "genome ", "of ", "and "};
    for (i = 0; i < n; ) {
        if (kind == 0) { const char* s = w[rnd() % 6]; while (*s && i < n) p[i++] = (unsigned char)*s++; }
        else if (kind == 1) { p[i] = (i % 61 == 60) ? '\n' : (unsigned char)"ACGT"[rnd() & 3]; i++; }
        else if (kind == 2) p[i++] = 0;
        else if (kind == 3) p[i++] = (unsigned char)rnd();
        else { const size_t per = 1 + rnd() % 13; size_t k; for (k = 0; k < 5000 && i < n; k++, i++) p[i] = (unsigned char)('a' + (i % per)); if (i < n) p[i++] = (unsigned char)rnd(); }
    }
    return p;
}
static void wr32(unsigned char* p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void wr64(unsigned char* p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t rd32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const unsigned char* p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }
int main(int argc, char** argv) {
    static const size_t sizes[] = {1, 7, 80, 300, 4096, 70000, 1048576, 1048577 + 4096, 2300000};
    long total = 0, accepted = 0, rejected = 0, wrong = 0; time_t t0 = time(NULL); int secs = argc > 1 ? atoi(argv[1]) : 30;
    if (argc > 1 && !strcmp(argv[1], "control")) {
        const size_t n = 100000; unsigned char* in = gen(0, n); unsigned char* z = (unsigned char*)malloc(axfast_bound(n));
        const int64_t zs = axfast_compress(in, n, z, axfast_bound(n), 3); unsigned char* o = (unsigned char*)malloc(n - 1);
        printf("control: decoding %zu bytes into a %zu-byte buffer passed as %zu\n", n, n - 1, n); fflush(stdout);
        printf("rc %lld (an ASan report must come before this line)\n", (long long)axfast_decompress(z, (size_t)zs, o, n));
        return 0;
    }
    while (time(NULL) - t0 < secs) {
        const size_t n = sizes[rnd() % 9]; const int kind = (int)(rnd() % 5), lv = 1 + (int)(rnd() % 8);
        unsigned char* in = gen(kind, n); const size_t cap = axfast_bound(n); unsigned char* z = (unsigned char*)malloc(cap);
        const int64_t zs = axfast_compress(in, n, z, cap, lv); int m;
        if (zs <= 0) { printf("compress failed n %zu level %d rc %lld\n", n, lv, (long long)zs); return 1; }
        for (m = 0; m < 300; m++) {
            size_t bn = (size_t)zs; unsigned char* b; int k, op = (int)(rnd() % 10); int64_t s;
            if (op == 4) bn = (size_t)(rnd() % (uint64_t)zs);                  /* truncation */
            if (op == 5) bn = (size_t)zs + 1 + rnd() % 64;                     /* extension */
            b = (unsigned char*)malloc(bn ? bn : 1); memcpy(b, z, bn < (size_t)zs ? bn : (size_t)zs);
            if (bn > (size_t)zs) for (k = (int)zs; k < (int)bn; k++) b[k] = (unsigned char)rnd();
            if (op <= 1 && bn) for (k = 0; k < 1 + (int)(rnd() % 8); k++) b[rnd() % bn] ^= (unsigned char)(1u << (rnd() % 8));
            if (op == 2 && bn) for (k = 0; k < 1 + (int)(rnd() % 4); k++) b[rnd() % bn] = (unsigned char)rnd();
            if (op == 3 && bn >= 68) { const int f = (int)(rnd() % 7);       /* header fields */
                if (f == 0) wr64(b + 12, rd64(b + 12) + (rnd() % 3) - 1 + (rnd() & 1 ? 0 : (uint64_t)rnd() % 5000000));
                if (f == 1) wr32(b + 20, (uint32_t)(rnd() % 3 ? 4096u << (rnd() % 9) : rnd()));
                if (f == 2) wr32(b + 24, rd32(b + 24) + (uint32_t)(rnd() % 3) - 1);
                if (f == 3) wr32(b + 40, (uint32_t)(rnd() % 40));
                if (f == 4) wr64(b + 44, rd64(b + 44) + (rnd() % 129) - 64);
                if (f == 5) wr32(b + 36, (uint32_t)(rnd() % 3));
                if (f == 6) b[52 + rnd() % 16] = (unsigned char)rnd(); }
            if (op == 6 && bn >= 84) { const uint32_t nb = rd32(b + 24); if (nb < 100000 && 68 + 8 * ((size_t)nb + 1) <= bn) {   /* table */
                    const size_t e = 68 + 8 * (rnd() % (nb + 1)); wr64(b + e, rd64(b + e) + (rnd() % 257) - 128); } }
            if ((op == 7 || op == 8) && bn >= 84) { const uint32_t nb = rd32(b + 24); if (nb && nb < 100000 && 68 + 8 * ((size_t)nb + 1) <= bn) {
                    const size_t base = 68 + 8 * ((size_t)nb + 1), bi = rnd() % nb; const uint64_t o0 = rd64(b + 68 + 8 * bi), o1 = rd64(b + 68 + 8 * (bi + 1));
                    if (o0 < o1 && base + o1 <= bn) {
                        if (op == 7) { uint64_t q; for (q = o0 + 12; q < o1; q++) if (rnd() % 16 == 0) b[base + q] = (unsigned char)rnd(); }   /* body */
                        else wr32(b + base + o0 + 4 * (rnd() % 3), rd32(b + base + o0 + 4 * (rnd() % 3)) + (uint32_t)(rnd() % 9) - 4); } } }   /* block header */
            if (op == 9 && bn) { const size_t st = (size_t)(rnd() % bn), ln = 1 + (size_t)(rnd() % 8); memset(b + st, 0, ln < bn - st ? ln : bn - st); }   /* zero run */
            total++;
            s = axfast_original_size(b, bn);
            if (s >= 0 && s <= (64 << 20)) {
                unsigned char* o = (unsigned char*)malloc(s ? (size_t)s : 1); const int64_t d = axfast_decompress(b, bn, o, (size_t)s); int r;
                if (d != s && d >= 0) wrong++;
                if (d == s) accepted++; else rejected++;
                for (r = 0; r < 4 && s > 0; r++) { const uint64_t off = rnd() % (uint64_t)s, len = rnd() % ((uint64_t)s - off + 1);
                    unsigned char* q = (unsigned char*)malloc(len ? (size_t)len : 1); const int64_t x = axfast_decompress_range(b, bn, off, len, q, (size_t)len);
                    if (x >= 0 && x != (int64_t)len) wrong++;
                    free(q); }
                free(o);
            } else rejected++;
            free(b);
        }
        free(in); free(z);
    }
    printf("fuzz: %ld mutants in %d s: %ld decoded to the declared size, %ld rejected, %ld wrong return values\n", total, secs, accepted, rejected, wrong);
    return wrong != 0;
}
