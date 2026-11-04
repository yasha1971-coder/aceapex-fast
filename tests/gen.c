/* gen.c - seeded test inputs for CI (no corpus needed): gen DIR writes
 *   empty, one (1 B), small (100 B), text (3 MiB of words), dna (2 MiB of A/C/G/T lines), zeros (1.5 MB),
 *   rand (300 000 B), period (64 KiB of short-period runs), mixed (1 MiB + 5: text, dna, random, zeros quarters) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long long s = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 32); }
static void put(const char* dir, const char* name, const unsigned char* p, size_t n) {
    char path[1024]; FILE* f; snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "wb"))) { perror(path); exit(1); }
    if (n) fwrite(p, 1, n, f);
    fclose(f);
}
static void text(unsigned char* p, size_t n) {
    static const char* w[] = {"the ", "of ", "and ", "archive ", "block ", "decoder ", "genome ", "random ", "access ", "fast\n"};
    size_t i = 0; while (i < n) { const char* x = w[rnd() % 10]; while (*x && i < n) p[i++] = (unsigned char)*x++; }
}
static void dna(unsigned char* p, size_t n) { size_t i; for (i = 0; i < n; i++) p[i] = (i % 61 == 60) ? '\n' : (unsigned char)"ACGT"[rnd() & 3]; }
int main(int argc, char** argv) {
    const char* d = argc > 1 ? argv[1] : "."; unsigned char* b = (unsigned char*)calloc(4u << 20, 1); size_t i, q;
    if (!b) return 1;
    put(d, "empty", b, 0);
    b[0] = 'x'; put(d, "one", b, 1);
    text(b, 100); put(d, "small", b, 100);
    text(b, 3u << 20); put(d, "text", b, 3u << 20);
    dna(b, 2u << 20); put(d, "dna", b, 2u << 20);
    memset(b, 0, 1500000); put(d, "zeros", b, 1500000);
    for (i = 0; i < 300000; i++) b[i] = (unsigned char)rnd();
    put(d, "rand", b, 300000);
    for (i = 0; i < 65536; ) { const size_t per = 1 + rnd() % 13; size_t k; for (k = 0; k < 4000 && i < 65536; k++, i++) b[i] = (unsigned char)('a' + i % per); }
    put(d, "period", b, 65536);
    q = (1048581 + 3) / 4; text(b, q); dna(b + q, q);
    for (i = 2 * q; i < 3 * q; i++) b[i] = (unsigned char)rnd();
    memset(b + 3 * q, 0, 1048581 - 3 * q); put(d, "mixed", b, 1048581);
    free(b); return 0;
}
