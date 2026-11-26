/* tb3bench.c - TB-3-ARM-1: one decoder variant per binary (axfast.c built with -DAXF_FP=N).
 *   tb3bench comp FILE LEVEL OUT     compress with axfast (level), decode once, compare, write OUT
 *   tb3bench dec  ARCHIVE ORIG K      decode K times, compare each output, print the median and the best MB/s
 * The shell alternates the binaries round by round (rotating order). */
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned char* rd(const char* p, size_t* n) {
    FILE* f = fopen(p, "rb"); unsigned char* b; if (!f) exit(2);
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*n + 1); if (*n && fread(b, 1, *n, f) != *n) exit(2); fclose(f); return b; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }
int main(int argc, char** argv) {
    size_t n, an; unsigned char *in, *a, *o; int k, K;
    if (argc < 5) return 2;
    if (!strcmp(argv[1], "comp")) {
        int64_t z, r; FILE* f; in = rd(argv[2], &n); a = malloc(axfast_bound(n)); o = malloc(n + 1);
        z = axfast_compress(in, n, a, axfast_bound(n), atoi(argv[3])); r = z > 0 ? axfast_decompress(a, (size_t)z, o, n) : -1;
        if (r != (int64_t)n || memcmp(o, in, n)) { printf("RT_FAIL %s\n", argv[2]); return 1; }
        f = fopen(argv[4], "wb"); if (!f || fwrite(a, 1, (size_t)z, f) != (size_t)z) return 2; fclose(f);
        printf("comp %s L%s -> %lld B (%.4f %%) rt_ok\n", argv[2], argv[3], (long long)z, 100.0 * (double)z / (double)n); return 0;
    }
    a = rd(argv[2], &an); in = rd(argv[3], &n); o = malloc(n + 64); K = atoi(argv[4]);
    {   double* v = malloc(sizeof(double) * (size_t)K);
        for (k = 0; k < K; k++) {
            double t0 = now(); int64_t r = axfast_decompress(a, an, o, n); double t1 = now();
            if (r != (int64_t)n || memcmp(o, in, n)) { printf("RT_FAIL\n"); return 1; }
            v[k] = (double)n / 1e6 / (t1 - t0);
        }
        qsort(v, (size_t)K, sizeof(double), cmpd);
        printf("median %.1f best %.1f\n", v[K / 2], v[K - 1]);
    }
    return 0;
}
