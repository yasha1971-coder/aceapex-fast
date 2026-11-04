/* bench_blocks.c - the C side of the Python overhead measurement. FILE at level 1 as (a) one archive per 1 MiB block
 * (each block its own archive) and (b) one archive of the whole file; best of R of axfast_decompress over all block
 * archives (each into the same 1 MiB buffer) and of the whole archive, into a reused buffer and (diagnostic: what a new Python bytes object costs) into
 * a fresh malloc per call. Prints MB/s. Usage: bench_blocks FILE R */
#define _POSIX_C_SOURCE 199309L
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define B (1u << 20)
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char** argv) {
    FILE* f; long n; unsigned char *in, *z, *o, *zb; int64_t zs, *bs; size_t nb, i, cb; int R, r; double best_b = 1e9, best_w = 1e9, best_bf = 1e9, best_wf = 1e9;
    if (argc < 3 || !(f = fopen(argv[1], "rb"))) return 2;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); R = atoi(argv[2]);
    in = malloc((size_t)n); o = malloc((size_t)n); z = malloc(axfast_bound((size_t)n));
    if (!in || !o || !z || fread(in, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    zs = axfast_compress(in, (size_t)n, z, axfast_bound((size_t)n), 1);
    nb = ((size_t)n + B - 1) / B; cb = axfast_bound(B); zb = malloc(nb * cb); bs = malloc(nb * sizeof *bs);
    for (i = 0; i < nb; i++) { const size_t k = (size_t)n - i * B < B ? (size_t)n - i * B : B; bs[i] = axfast_compress(in + i * B, k, zb + i * cb, cb, 1); }
    for (r = 0; r < R; r++) {
        double t = now();
        for (i = 0; i < nb; i++) if (axfast_decompress(zb + i * cb, (size_t)bs[i], o, B) < 0) return 4;   /* one 1 MiB buffer, as a Python bytes object freed and reused */
        t = now() - t; if (t < best_b) best_b = t;
        t = now(); if (axfast_decompress(z, (size_t)zs, o, (size_t)n) != n) return 4; t = now() - t; if (t < best_w) best_w = t;
        t = now();
        for (i = 0; i < nb; i++) { unsigned char* q = malloc(B); if (!q || axfast_decompress(zb + i * cb, (size_t)bs[i], q, B) < 0) return 4; free(q); }
        t = now() - t; if (t < best_bf) best_bf = t;
        t = now(); { unsigned char* q = malloc((size_t)n); if (!q || axfast_decompress(z, (size_t)zs, q, (size_t)n) != n) return 4; free(q); } t = now() - t; if (t < best_wf) best_wf = t;
    }
    printf("C block %.1f whole %.1f block_fresh %.1f whole_fresh %.1f", n / best_b / 1e6, n / best_w / 1e6, n / best_bf / 1e6, n / best_wf / 1e6);
    return 0;
}
