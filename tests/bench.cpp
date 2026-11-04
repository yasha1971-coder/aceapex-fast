// bench.cpp - fastlib step 1 speed against the current library, one thread, the same build flags (no -march):
//   enc: axfast_compress(1) vs aceapex_compress(11, 1 thread); dec: axfast_decompress vs aceapex_decompress_mt(1 thread)
//   on the same archive. MB/s of the original, best of R per call; one line per round. Usage: bench FILE R
#include "aceapex.h"
#include "axfast.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); const size_t n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> in(n); if (fread(in.data(), 1, n, f) != n) return 2; fclose(f);
    const int R = argc > 2 ? atoi(argv[2]) : 7;
    std::vector<uint8_t> za(aceapex_compress_bound(n)), zb(axfast_bound(n)), o(n);
    double ea = 1e9, eb = 1e9, da = 1e9, db = 1e9; int64_t sa = 0, sb = 0;
    for (int r = 0; r < R; r++) {
        double t = now(); sa = aceapex_compress(in.data(), n, za.data(), za.size(), 11, 1); ea = std::min(ea, now() - t);
        t = now(); sb = axfast_compress(in.data(), n, zb.data(), zb.size(), 1); eb = std::min(eb, now() - t);
    }
    if (sa != sb || memcmp(za.data(), zb.data(), (size_t)sa)) { printf("archives differ\n"); return 1; }
    for (int r = 0; r < R; r++) {
        double t = now(); int64_t d = aceapex_decompress_mt(za.data(), (size_t)sa, o.data(), n, 1); da = std::min(da, now() - t); if (d != (int64_t)n) return 1;
        t = now(); d = axfast_decompress(za.data(), (size_t)sa, o.data(), n); db = std::min(db, now() - t); if (d != (int64_t)n || memcmp(o.data(), in.data(), n)) return 1;
    }
    printf("enc lib %.1f axfast %.1f (%.3f x) | dec lib %.0f axfast %.0f (%.3f x) | %lld B\n", n / ea / 1e6, n / eb / 1e6, ea / eb, n / da / 1e6, n / db / 1e6, da / db, (long long)sa);
    return 0;
}
