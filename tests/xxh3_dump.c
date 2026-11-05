/* xxh3_dump.c - axfast_xxh3 of a seeded buffer at every length 0..5000 and at 12 larger lengths (up to 64 MiB + 7),
 * one hex value per line: builds with and without SIMD (AXFAST_NO_SIMD), AVX2 and NEON must print the same lines.
 * tests/check.cpp compares the same values with the reference XXH3_64bits. Usage: xxh3_dump, or xxh3_dump FILE...
 * (the hash of each file, one hex value per line). */
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
    static const size_t big[] = {5001, 8191, 65536, 65537, 1048575, 1048576, 1000003, 4u << 20, (16u << 20) + 1, (32u << 20) - 1, 64u << 20, (64u << 20) + 7};
    const size_t N = (64u << 20) + 8; unsigned char* b = (unsigned char*)malloc(N); unsigned long long x = 0x9E3779B97F4A7C15ull; size_t i;
    if (!b) return 2;
    if (argc > 1) {
        int a;
        for (a = 1; a < argc; a++) {
            FILE* f = fopen(argv[a], "rb"); size_t n;
            if (!f) return 2;
            n = fread(b, 1, N, f); fclose(f);
            printf("%016llx\n", (unsigned long long)axfast_xxh3(b, n));
        }
        free(b); return 0;
    }
    for (i = 0; i < N; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b[i] = (unsigned char)(x >> 56); }
    for (i = 0; i <= 5000; i++) printf("%zu %016llx\n", i, (unsigned long long)axfast_xxh3(b + (i & 7), i));
    for (i = 0; i < sizeof big / sizeof big[0]; i++) printf("%zu %016llx\n", big[i], (unsigned long long)axfast_xxh3(b + 1, big[i]));
    free(b); return 0;
}
