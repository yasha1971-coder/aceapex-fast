/* standalone.c - runs fuzz_axfast's LLVMFuzzerTestOneInput without libFuzzer (gcc builds, CI job sanitize): every file
 * named on the command line, then SECONDS of random mutations of them (bit flips, byte overwrites, the VERIFY path's
 * edit bytes). Usage: standalone SECONDS FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size);
static unsigned long long rs = 88172645463325252ull;
static unsigned long long rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
int main(int argc, char** argv) {
    unsigned char* in[256]; size_t sz[256]; int nf = 0, i, secs = argc > 1 ? atoi(argv[1]) : 10; long runs = 0; time_t t0;
    for (i = 2; i < argc && nf < 256; i++) {
        FILE* f = fopen(argv[i], "rb"); long n;
        if (!f) continue;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        in[nf] = (unsigned char*)malloc(n ? (size_t)n : 1); sz[nf] = fread(in[nf], 1, (size_t)n, f); fclose(f);
        LLVMFuzzerTestOneInput(in[nf], sz[nf]); nf++;
    }
    t0 = time(NULL);
    while (nf && time(NULL) - t0 < secs) {
        const int j = (int)(rnd() % (unsigned)nf); const size_t n = sz[j]; unsigned char* b; int k, op = (int)(rnd() % 3);
        if (n < 2) continue;
        b = (unsigned char*)malloc(n); for (k = 0; k < (int)n; k++) b[k] = in[j][k];
        if (op == 0) for (k = 1 + (int)(rnd() % 8); k > 0; k--) b[1 + rnd() % (n - 1)] ^= (unsigned char)(1u << (rnd() % 8));
        if (op == 1) for (k = 1 + (int)(rnd() % 4); k > 0; k--) b[1 + rnd() % (n - 1)] = (unsigned char)rnd();
        if (op == 2 && (b[0] & 0x80) && n > 26) for (k = 1; k < 26; k++) b[k] = rnd() % 3 ? 0 : (unsigned char)rnd();
        LLVMFuzzerTestOneInput(b, n); free(b); runs++;
    }
    for (i = 0; i < nf; i++) free(in[i]);
    printf("standalone: %d files, %ld mutants in %d s, no abort\n", nf, runs, secs);
    return 0;
}
