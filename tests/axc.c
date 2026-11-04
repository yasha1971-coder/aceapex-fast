/* axc.c - the C reference for the Python tests: axc LEVEL FILE writes axfast_compress(FILE, LEVEL) to stdout */
#include "axfast.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
    FILE* f; long n; unsigned char *in, *z; int64_t zs; size_t cap;
    if (argc < 3 || !(f = fopen(argv[2], "rb"))) return 2;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    in = (unsigned char*)malloc((size_t)n + 1); cap = axfast_bound((size_t)n); z = (unsigned char*)malloc(cap);
    if (!in || !z || (n && fread(in, 1, (size_t)n, f) != (size_t)n)) return 2;
    fclose(f);
    zs = axfast_compress(in, (size_t)n, z, cap, atoi(argv[1]));
    if (zs < 0) return 3;
    fwrite(z, 1, (size_t)zs, stdout);
    free(in); free(z); return 0;
}
