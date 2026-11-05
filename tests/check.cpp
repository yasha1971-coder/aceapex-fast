// check.cpp - gate 1 of fastlib step 1 (README): axfast against libaceapex.
//   xxh3   : axfast_xxh3 == XXH3_64bits on every length 0..5000 and on long inputs (random and the given files)
//   bytes  : axfast_compress(level) == aceapex_compress(level + 10, 1 thread) byte for byte, levels 1..8
//   cross  : axfast_decompress reads libaceapex archives and aceapex_decompress_mt reads axfast archives (== input)
//   range  : axfast_decompress_range on 64 seeded ranges == the input slice
// Usage: check [--levels=A-B] FILE... (default 1-8); prints one line per file and level, a summary, exit 1 on any difference.
#include "aceapex.h"
#include "axfast.h"
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
#include <string>
static std::vector<uint8_t> slurp(const char* p) { FILE* f = fopen(p, "rb"); if (!f) return {}; fseek(f, 0, SEEK_END); std::vector<uint8_t> v((size_t)ftell(f)); fseek(f, 0, SEEK_SET); if (!v.empty() && fread(v.data(), 1, v.size(), f) != v.size()) v.clear(); fclose(f); return v; }
int main(int argc, char** argv) {
    int bad = 0, lines = 0;
    { std::mt19937_64 r(7); std::vector<uint8_t> b(1 << 20); for (auto& x : b) x = (uint8_t)r(); int xb = 0;
      for (size_t n = 0; n <= 5000; n++) xb += axfast_xxh3(b.data(), n) != XXH3_64bits(b.data(), n);
      for (size_t n : {65536ul, 65537ul, 1000003ul, (size_t)(1 << 20)}) xb += axfast_xxh3(b.data(), n) != XXH3_64bits(b.data(), n);
      printf("xxh3: %d differences on lengths 0..5000 + 4 long\n", xb); bad += xb; }
    int lo = 1, hi = 8, f0 = 1;
    if (argc > 1 && sscanf(argv[1], "--levels=%d-%d", &lo, &hi) == 2) f0 = 2;
    for (int fi = f0; fi < argc; fi++) {
        std::vector<uint8_t> in = slurp(argv[fi]); const size_t n = in.size();
        if (axfast_xxh3(in.data(), n) != XXH3_64bits(in.data(), n)) { printf("%s: xxh3 DIFF\n", argv[fi]); bad++; }
        for (int lv = lo; lv <= hi; lv++) {
            std::vector<uint8_t> za(aceapex_compress_bound(n) + 4096), zb(axfast_bound(n)), o(n + 1);
            const int64_t sa = aceapex_compress(in.data(), n, za.data(), za.size(), lv + 10, 1);
            const int64_t sb = axfast_compress(in.data(), n, zb.data(), zb.size(), lv);
            const bool same = sa == sb && sa > 0 && !memcmp(za.data(), zb.data(), (size_t)sa);
            const int64_t d1 = axfast_decompress(za.data(), (size_t)sa, o.data(), n);
            const bool x1 = d1 == (int64_t)n && (n == 0 || !memcmp(o.data(), in.data(), n));
            const int64_t d2 = sb > 0 ? aceapex_decompress_mt(zb.data(), (size_t)sb, o.data(), n, 1) : -9;
            const bool x2 = d2 == (int64_t)n && (n == 0 || !memcmp(o.data(), in.data(), n));
            int rb = 0;
            if (n) { std::mt19937_64 r(lv * 1000 + n); std::vector<uint8_t> s(n);
                for (int k = 0; k < 64; k++) { const uint64_t off = r() % n, len = r() % (n - off + 1);
                    const int64_t q = axfast_decompress_range(zb.data(), (size_t)sb, off, len, s.data(), s.size());
                    rb += q != (int64_t)len || (len && memcmp(s.data(), in.data() + off, len)); } }
            const bool ok = same && x1 && x2 && !rb; bad += !ok; lines++;
            printf("%s L%d: libaceapex %lld B, axfast %lld B %s; axfast reads lib %s, lib reads axfast %s, ranges %s\n", argv[fi], lv,
                   (long long)sa, (long long)sb, same ? "bytes==" : "BYTES DIFF", x1 ? "ok" : "FAIL", x2 ? "ok" : "FAIL", rb ? "FAIL" : "64/64");
        }
    }
    printf("# %d file-level lines, %d failures\n", lines, bad);
    return bad ? 1 : 0;
}
