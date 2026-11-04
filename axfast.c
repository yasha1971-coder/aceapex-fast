/* axfast.c - aceapex-fast: the FAST profile of ACEAPEX as a C99 library (with axfast.h). Format: ACEPX2 header version 3,
 * profile 1 (docs/FORMAT_FAST.md). The decoder is the one of src/ax_fast.h (bounded, fuzzed, endian-safe), copied so that
 * these two files stand alone; the encoder is the reference encoder of src/ax_fast_enc.h in C and writes the same bytes;
 * XXH3-64 is our own scalar code. Little-endian fields and loads on every host. Build: cc -O2 -c axfast.c (C99). */
#include "axfast.h"
#include <stdlib.h>
#include <string.h>

/* ---- format and decoder (src/ax_fast.h, 2.6.1) ---- */
#define AXF_VERSION       3u
#define AXF_PROFILE_FAST  1u
#define AXF_MIN_DIST      64u
#define AXF_COPY          64u          /* longest match copied by a direct code; longer matches are split by the encoder */
#define AXF_PAD           64u          /* readable slack after the literals of a block */
#define AXF_BHDR          12u          /* ntok, next, tail */
#define AXF_BLOCK_MAX     (1u << 20)   /* 20-bit distance field + 64 covers a block of 2^20 bytes */
#define AXF_MM_MIN        4u
#define AXF_MM_MAX        32u
#define AXF_ROOM          160u         /* output room the fast path needs for one token */

#if defined(__GNUC__)
#define AXF_INLINE static inline __attribute__((always_inline))
#define AXF_LIKELY(x) __builtin_expect(!!(x), 1)
#define AXF_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define AXF_INLINE static inline
#define AXF_LIKELY(x) (x)
#define AXF_UNLIKELY(x) (x)
#endif

typedef struct {
    uint64_t orig, payload;
    uint32_t bs, nb, profile, mm;
    const uint8_t* tab;    /* u64 boff[nb + 1], unaligned: read through axf_boff */
    const uint8_t* data;   /* payload */
} axf_arc_t;

/* Every multi-byte field is little-endian. On a big-endian host the reads (and the encoder's writes and 8-byte compares)
 * swap bytes, so archives are the same bytes on every host (2.6.1, lzbench #357: until 2.6.0 the fast profile was
 * little-endian only). On little-endian hosts the swap compiles away. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define AXF_BIG_ENDIAN 1
AXF_INLINE uint32_t axf_le32(uint32_t v) { return __builtin_bswap32(v); }
AXF_INLINE uint64_t axf_le64(uint64_t v) { return __builtin_bswap64(v); }
#else
AXF_INLINE uint32_t axf_le32(uint32_t v) { return v; }
AXF_INLINE uint64_t axf_le64(uint64_t v) { return v; }
#endif
AXF_INLINE uint32_t axf_rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return axf_le32(v); }
AXF_INLINE uint64_t axf_rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return axf_le64(v); }
AXF_INLINE uint64_t axf_boff(const axf_arc_t* a, uint32_t i) { return axf_rd64(a->tab + 8ull * i); }

/* 1 if the 68 bytes at p are a version-3 header (magic + version only; axf_open validates the rest) */
AXF_INLINE int axf_is_v3(const uint8_t* p, size_t n) {
    return n >= 68 && memcmp(p, "ACEPX2\0\0", 8) == 0 && axf_rd32(p + 8) == AXF_VERSION;
}

/* The 68-byte header alone (the 68 bytes at p): version, reserved fields, profile, min_match, block size, block count
 * consistent with orig_size, payload large enough for nb minimal blocks. Lets a streaming reader check the header
 * before it allocates the block table (2.6.1, lzbench #357). Returns 0, or -2 (ACEAPEX_ERR_DATA). */
static int axf_open_header(const uint8_t* p, axf_arc_t* a) {
    if (!axf_is_v3(p, 68)) return -2;
    a->orig = axf_rd64(p + 12); a->bs = axf_rd32(p + 20); a->nb = axf_rd32(p + 24);
    a->profile = axf_rd32(p + 36); a->mm = axf_rd32(p + 40); a->payload = axf_rd64(p + 44);
    if (axf_rd64(p + 52) != 0 || axf_rd64(p + 60) != 0) return -2;                 /* reserved: must be zero */
    if (a->profile != AXF_PROFILE_FAST || a->mm < AXF_MM_MIN || a->mm > AXF_MM_MAX) return -2;
    if (a->bs == 0 || a->bs > AXF_BLOCK_MAX || a->nb == 0 || a->orig == 0) return -2;    /* the empty input is a version-2 archive */
    if ((uint64_t)a->nb * a->bs < a->orig || (uint64_t)(a->nb - 1) * a->bs >= a->orig) return -2;
    if (a->payload / (AXF_BHDR + AXF_PAD) < a->nb) return -2;                        /* every block >= header + pad */
    return 0;
}
/* Parse and validate a version-3 archive of n bytes whose header and block table are at p (n may exceed what is in
 * memory: a streaming reader passes the archive size and has only header + table): header fields, the block table
 * (monotonic, inside the payload, every block at least a block header plus the pad), payload inside n.
 * Returns 0, or -2 (ACEAPEX_ERR_DATA). */
static int axf_open64(const uint8_t* p, uint64_t n, axf_arc_t* a) {
    uint64_t need, prev, i;
    if (n < 68 || axf_open_header(p, a) != 0) return -2;
    need = 68 + 8ull * ((uint64_t)a->nb + 1);
    if (need > n || a->payload > n - need) return -2;
    a->tab = p + 68; a->data = p + (size_t)need;
    if (axf_boff(a, 0) != 0 || axf_boff(a, a->nb) != a->payload) return -2;
    prev = 0;
    for (i = 1; i <= a->nb; i++) {
        const uint64_t o = axf_boff(a, (uint32_t)i);
        if (o < prev || o - prev < AXF_BHDR + AXF_PAD) return -2;
        prev = o;
    }
    return 0;
}
/* the whole archive in memory: n bytes at p */
static int axf_open(const uint8_t* p, size_t n, axf_arc_t* a) { return axf_open64(p, (uint64_t)n, a); }

/* original bytes held by block b */
AXF_INLINE uint32_t axf_block_len(const axf_arc_t* a, uint32_t b) {
    const uint64_t st = (uint64_t)b * a->bs;
    return (uint32_t)(a->orig - st < a->bs ? a->orig - st : a->bs);
}

/* 32-byte copy; memcpy of a constant size compiles to one vector move (AVX2), two (SSE2/NEON) */
AXF_INLINE void axf_cp32(uint8_t* d, const uint8_t* s) { memcpy(d, s, 32); }
/* up to AXF_COPY + 30 bytes in 32-byte steps; exact when the source lies >= 64 bytes before the destination or is disjoint */
AXF_INLINE void axf_cpw(uint8_t* d, const uint8_t* s, uint32_t l) {
    uint32_t k;
    axf_cp32(d, s);
    for (k = 32; k < l; k += 32) axf_cp32(d + k, s + k);
}
/* a length extension: bytes of 255 then one byte below 255; bad = 1 when the stream ends or the value overflows */
AXF_INLINE uint32_t axf_ext(const uint8_t** ext, const uint8_t* eend, int* bad) {
    uint32_t r = 0;
    for (;;) {
        uint8_t b;
        if (*ext >= eend) { *bad = 1; return 0; }
        b = *(*ext)++; r += b;
        if (b < 255) return r;
        if (r > (1u << 30)) { *bad = 1; return 0; }
    }
}

/* One block. src: ssz bytes (the block as stored, pad included). dst: n bytes. mm: min_match from the header.
 * Returns n, or 0 for a malformed block. See the contract above. */
AXF_INLINE uint32_t axf_dec_block_mm(const uint8_t* src, size_t ssz, uint8_t* dst, uint32_t n, const uint32_t mm) {
    uint32_t ntok, next, tail, i;
    const uint8_t *tp, *ext, *lit, *lend, *eend;
    uint8_t *d, *dend;
    if (ssz < AXF_BHDR + AXF_PAD) return 0;
    ntok = axf_rd32(src); next = axf_rd32(src + 4); tail = axf_rd32(src + 8);
    if ((uint64_t)AXF_BHDR + 4ull * ntok + next + AXF_PAD > ssz) return 0;
    tp = src + AXF_BHDR; ext = tp + 4ull * ntok; lit = ext + next; eend = lit;
    lend = src + ssz - AXF_PAD;                     /* literals end here; the pad [lend, lend + 64) is readable */
    d = dst; dend = dst + n;
    for (i = 0; i < ntok; ) {
        /* Guarded run: k tokens with no bound check per token. A fast token reads at most 64 literal bytes from lit and
         * advances lit by at most 62, and writes at most 62 + 96 bytes from d. With lit <= lend at the start (kept by
         * every path) and k <= (lend - lit) / 62 + 1, every token of the run starts with lit <= lend, so its 64-byte
         * read ends inside the pad; with k <= (dend - d) / 160 every write stays in dst. Only an escape (ll or mc = 63)
         * or a source before the block leave the run. After the run lit may lie past lend: checked once. */
        uint32_t k = ntok - i;
        {   const size_t kd = (size_t)(dend - d) / AXF_ROOM, kl = (size_t)(lend - lit) / 62u + 1u;
            if (kd < k) k = (uint32_t)kd;
            if (kl < k) k = (uint32_t)kl; }
        for (; k; k--, i++) {
            const uint32_t t = axf_rd32(tp + 4ull * i);
            const uint32_t ll = t & 63u, mc = (t >> 6) & 63u, dist = (t >> 12) + AXF_MIN_DIST;
            if (AXF_UNLIKELY(ll == 63u || mc == 63u)) break;
            axf_cp32(d, lit); if (ll > 32u) axf_cp32(d + 32, lit + 32);
            d += ll; lit += ll;
            if (AXF_UNLIKELY((size_t)(d - dst) < dist)) return 0;    /* source before the block */
            axf_cpw(d, d - dist, mc + mm); d += mc + mm;             /* <= 94 bytes: at most 3 steps */
        }
        if (AXF_UNLIKELY(lit > lend)) return 0;                      /* the run consumed literals that are not there */
        if (i >= ntok) break;
        {   /* exact path, one token: block end, long literal run, long match */
            const uint32_t t = axf_rd32(tp + 4ull * i);
            uint32_t ll = t & 63u, mc = (t >> 6) & 63u, ml;
            const uint32_t dist = (t >> 12) + AXF_MIN_DIST;
            i++;
            int bad = 0; uint32_t c0;
            if (ll == 63u) { ll += axf_ext(&ext, eend, &bad); if (bad) return 0; }
            if ((size_t)(lend - lit) < ll || (size_t)(dend - d) < ll) return 0;
            /* lit + ll <= lend, so 32-byte steps read at most 31 bytes into the pad; with 32 bytes of room after the run
               the steps may write past it (the next token overwrites those bytes) - no call, no size-dependent branches */
            if ((size_t)(dend - d) >= (size_t)ll + 32u) axf_cpw(d, lit, ll); else memcpy(d, lit, ll);
            d += ll; lit += ll;
            if ((size_t)(d - dst) < dist) return 0;
            ml = mc + mm;
            if (mc == 63u) { ml = AXF_COPY + 1 + axf_ext(&ext, eend, &bad); if (bad) return 0; }
            if ((size_t)(dend - d) < ml) return 0;
            if ((size_t)(dend - d) >= (size_t)ml + 32u) axf_cpw(d, d - dist, ml);   /* dist >= 64 > 32: forward 32-byte steps are exact */
            else for (c0 = 0; c0 < ml; ) { uint32_t c = ml - c0 < dist ? ml - c0 : dist; memcpy(d + c0, d + c0 - dist, c); c0 += c; }
            d += ml;
        }
    }
    if ((size_t)(lend - lit) != tail || (size_t)(dend - d) != tail) return 0;
    memcpy(d, lit, tail);
    return n;
}
/* Variant with a bound check on every token (2.4.0 first cut, 9f8a4ab). Measured on ace-core (lzbench -t8,8, median of 7,
 * results/fast-bench-2026-10-27-0d3a065.md): with few tokens per KiB (min match 16 and 12, most tokens escape to the
 * exact path) it is 1.7-1.8 % faster than the guarded runs; with many tokens (min match 8 and 5) the guarded runs are
 * 4.5-15 % faster. Both keep the same contract; the dispatcher below picks per min_match. */
AXF_INLINE uint32_t axf_dec_block_tok_mm(const uint8_t* src, size_t ssz, uint8_t* dst, uint32_t n, const uint32_t mm) {
    uint32_t ntok, next, tail, i;
    const uint8_t *tp, *ext, *lit, *lend, *eend;
    uint8_t *d, *dend, *dfast;
    if (ssz < AXF_BHDR + AXF_PAD) return 0;
    ntok = axf_rd32(src); next = axf_rd32(src + 4); tail = axf_rd32(src + 8);
    if ((uint64_t)AXF_BHDR + 4ull * ntok + next + AXF_PAD > ssz) return 0;
    tp = src + AXF_BHDR; ext = tp + 4ull * ntok; lit = ext + next; eend = lit;
    lend = src + ssz - AXF_PAD;                     /* literals end here; the pad [lend, lend + 64) is readable */
    d = dst; dend = dst + n;
    dfast = n >= AXF_ROOM ? dend - (AXF_ROOM - 1) : dst;   /* fast path while d < dfast, i.e. >= 160 bytes of room */
    for (i = 0; i < ntok; i++) {
        const uint32_t t = axf_rd32(tp + 4ull * i);
        uint32_t ll = t & 63u, mc = (t >> 6) & 63u, ml;
        const uint32_t dist = (t >> 12) + AXF_MIN_DIST;
        if (AXF_LIKELY(d < dfast && ll != 63u && mc != 63u)) {
            /* literal copy reads lit..lit+63: lit <= lend holds here, so the read ends inside the pad */
            /* branch-free on lengths: always 64 literal bytes and 96 match bytes (ll <= 62, ml <= 94); the room check
               (>= 160 bytes before the copies, <= 62 literal bytes in front of the match: 98 left) covers both */
            axf_cp32(d, lit); axf_cp32(d + 32, lit + 32);
            d += ll; lit += ll;
            if (AXF_UNLIKELY((size_t)(d - dst) < dist)) return 0;    /* source before the block */
            ml = mc + mm;
            { const uint8_t* sm = d - dist; axf_cp32(d, sm); axf_cp32(d + 32, sm + 32); axf_cp32(d + 64, sm + 64); }
            d += ml;
            if (AXF_UNLIKELY(lit > lend)) return 0;                  /* the literals of this token were not all there */
            continue;
        }
        {   /* exact path: block end, long literal run, long match */
            int bad = 0; uint32_t k;
            if (ll == 63u) { ll += axf_ext(&ext, eend, &bad); if (bad) return 0; }
            if ((size_t)(lend - lit) < ll || (size_t)(dend - d) < ll) return 0;
            /* lit + ll <= lend, so 32-byte steps read at most 31 bytes into the pad; with 32 bytes of room after the run
               the steps may write past it (the next token overwrites those bytes) - no call, no size-dependent branches */
            if ((size_t)(dend - d) >= (size_t)ll + 32u) axf_cpw(d, lit, ll); else memcpy(d, lit, ll);
            d += ll; lit += ll;
            if ((size_t)(d - dst) < dist) return 0;
            ml = mc + mm;
            if (mc == 63u) { ml = AXF_COPY + 1 + axf_ext(&ext, eend, &bad); if (bad) return 0; }
            if ((size_t)(dend - d) < ml) return 0;
            if ((size_t)(dend - d) >= (size_t)ml + 32u) axf_cpw(d, d - dist, ml);   /* dist >= 64 > 32: forward 32-byte steps are exact */
            else for (k = 0; k < ml; ) { uint32_t c = ml - k < dist ? ml - k : dist; memcpy(d + k, d + k - dist, c); k += c; }
            d += ml;
        }
    }
    if ((size_t)(lend - lit) != tail || (size_t)(dend - d) != tail) return 0;
    memcpy(d, lit, tail);
    return n;
}
/* Built without -march (lzbench, distribution packages: baseline x86-64, SSE2) the 32-byte copies are two 16-byte moves.
 * On x86-64 Linux with GCC 6+ / Clang 14+ the block decoder is cloned for AVX2 and picked at load time (ifunc), so a
 * generic binary runs the 32-byte moves on CPUs that have them. Other targets build the one generic function. */
#if defined(__x86_64__) && defined(__linux__) && defined(__GLIBC__) && ((defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 6) || (defined(__clang__) && __clang_major__ >= 14)) && !defined(AXF_NO_CLONES)
#define AXF_CLONES __attribute__((target_clones("avx2", "default")))
#else
#define AXF_CLONES
#endif
AXF_CLONES static uint32_t axf_dec_block(const uint8_t* src, size_t ssz, uint8_t* dst, uint32_t n, uint32_t mm) {
    switch (mm) {   /* the common min_match values as compile-time constants; per-token checks for 16 and 12, guarded runs below */
    case 16: return axf_dec_block_tok_mm(src, ssz, dst, n, 16);
    case 12: return axf_dec_block_tok_mm(src, ssz, dst, n, 12);
    case 8:  return axf_dec_block_mm(src, ssz, dst, n, 8);
    case 5:  return axf_dec_block_mm(src, ssz, dst, n, 5);
    default: return axf_dec_block_mm(src, ssz, dst, n, mm);
    }
}
/* block b of an opened archive into dst (axf_block_len(a, b) bytes); returns that length or 0 */
AXF_INLINE uint32_t axf_dec_block_of(const axf_arc_t* a, uint32_t b, uint8_t* dst) {
    const uint64_t o = axf_boff(a, b), e = axf_boff(a, b + 1);
    return axf_dec_block(a->data + o, (size_t)(e - o), dst, axf_block_len(a, b), a->mm);
}

/* ---- XXH3-64, seed 0, default secret (the header checksum). Scalar, written from the XXH3 specification; the secret is
 * the reference implementation's (xxHash, BSD 2-Clause, Yann Collet). Same values as XXH3_64bits on every host. ---- */
static const uint8_t axh_secret[192] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};
#define AXH_P32_1 0x9E3779B1u
#define AXH_P32_2 0x85EBCA77u
#define AXH_P32_3 0xC2B2AE3Du
#define AXH_P64_1 0x9E3779B185EBCA87ull
#define AXH_P64_2 0xC2B2AE3D27D4EB4Full
#define AXH_P64_3 0x165667B19E3779F9ull
#define AXH_P64_4 0x85EBCA77C2B2AE63ull
#define AXH_P64_5 0x27D4EB2F165667C5ull
static uint64_t axh_rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static uint64_t axh_swap64(uint64_t x) {
    return ((x << 56) & 0xff00000000000000ull) | ((x << 40) & 0x00ff000000000000ull) | ((x << 24) & 0x0000ff0000000000ull) |
           ((x << 8) & 0x000000ff00000000ull) | ((x >> 8) & 0x00000000ff000000ull) | ((x >> 24) & 0x0000000000ff0000ull) |
           ((x >> 40) & 0x000000000000ff00ull) | ((x >> 56) & 0x00000000000000ffull);
}
static uint64_t axh_fold(uint64_t a, uint64_t b) {   /* low ^ high of the 128-bit product */
#if defined(__SIZEOF_INT128__) && defined(__GNUC__)
    __extension__ typedef unsigned __int128 u128;
    const u128 p = (u128)a * b; return (uint64_t)p ^ (uint64_t)(p >> 64);
#else
    const uint64_t al = a & 0xffffffffu, ah = a >> 32, bl = b & 0xffffffffu, bh = b >> 32;
    const uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const uint64_t cross = (ll >> 32) + (hl & 0xffffffffu) + lh;
    const uint64_t hi = hh + (hl >> 32) + (cross >> 32), lo = (cross << 32) | (ll & 0xffffffffu);
    return lo ^ hi;
#endif
}
static uint64_t axh_xxh64_aval(uint64_t h) { h ^= h >> 33; h *= AXH_P64_2; h ^= h >> 29; h *= AXH_P64_3; h ^= h >> 32; return h; }
static uint64_t axh_aval(uint64_t h) { h ^= h >> 37; h *= 0x165667919E3779F9ull; h ^= h >> 32; return h; }
static uint64_t axh_rrmxmx(uint64_t h, uint64_t len) {
    h ^= axh_rotl(h, 49) ^ axh_rotl(h, 24); h *= 0x9FB21C651E98DF25ull; h ^= (h >> 35) + len; h *= 0x9FB21C651E98DF25ull; return h ^ (h >> 28);
}
static uint64_t axh_mix16(const uint8_t* p, const uint8_t* s) { return axh_fold(axf_rd64(p) ^ axf_rd64(s), axf_rd64(p + 8) ^ axf_rd64(s + 8)); }
static void axh_round(uint64_t* acc, const uint8_t* p, const uint8_t* s) {
    int i;
    for (i = 0; i < 8; i++) { const uint64_t v = axf_rd64(p + 8 * i), k = v ^ axf_rd64(s + 8 * i); acc[i ^ 1] += v; acc[i] += (k & 0xffffffffu) * (k >> 32); }
}
static void axh_scramble(uint64_t* acc, const uint8_t* s) {
    int i;
    for (i = 0; i < 8; i++) { uint64_t a = acc[i]; a ^= a >> 47; a ^= axf_rd64(s + 8 * i); a *= AXH_P32_1; acc[i] = a; }
}
uint64_t axfast_xxh3(const void* in, size_t len) {
    const uint8_t* p = (const uint8_t*)in; const uint8_t* s = axh_secret;
    if (len <= 16) {
        if (len > 8) {
            const uint64_t lo = axf_rd64(p) ^ (axf_rd64(s + 24) ^ axf_rd64(s + 32)), hi = axf_rd64(p + len - 8) ^ (axf_rd64(s + 40) ^ axf_rd64(s + 48));
            return axh_aval((uint64_t)len + axh_swap64(lo) + hi + axh_fold(lo, hi));
        }
        if (len >= 4) {
            const uint64_t in64 = (uint64_t)axf_rd32(p + len - 4) + ((uint64_t)axf_rd32(p) << 32);
            return axh_rrmxmx(in64 ^ (axf_rd64(s + 8) ^ axf_rd64(s + 16)), len);
        }
        if (len) {
            const uint32_t c = ((uint32_t)p[0] << 16) | ((uint32_t)p[len >> 1] << 24) | (uint32_t)p[len - 1] | ((uint32_t)len << 8);
            return axh_xxh64_aval((uint64_t)c ^ (uint64_t)(axf_rd32(s) ^ axf_rd32(s + 4)));
        }
        return axh_xxh64_aval(axf_rd64(s + 56) ^ axf_rd64(s + 64));
    }
    if (len <= 128) {
        uint64_t acc = (uint64_t)len * AXH_P64_1;
        if (len > 32) {
            if (len > 64) {
                if (len > 96) { acc += axh_mix16(p + 48, s + 96); acc += axh_mix16(p + len - 64, s + 112); }
                acc += axh_mix16(p + 32, s + 64); acc += axh_mix16(p + len - 48, s + 80);
            }
            acc += axh_mix16(p + 16, s + 32); acc += axh_mix16(p + len - 32, s + 48);
        }
        acc += axh_mix16(p, s); acc += axh_mix16(p + len - 16, s + 16);
        return axh_aval(acc);
    }
    if (len <= 240) {
        uint64_t acc = (uint64_t)len * AXH_P64_1; const size_t rounds = len / 16; size_t i;
        for (i = 0; i < 8; i++) acc += axh_mix16(p + 16 * i, s + 16 * i);
        acc = axh_aval(acc);
        for (i = 8; i < rounds; i++) acc += axh_mix16(p + 16 * i, s + 16 * (i - 8) + 3);
        acc += axh_mix16(p + len - 16, s + 136 - 17);
        return axh_aval(acc);
    }
    {   /* long input: blocks of 16 stripes of 64 bytes, a scramble after each block */
        uint64_t acc[8] = { AXH_P32_3, AXH_P64_1, AXH_P64_2, AXH_P64_3, AXH_P64_4, AXH_P32_2, AXH_P64_5, AXH_P32_1 };
        const size_t block = 64 * 16, nb = (len - 1) / block; size_t b, k, i; uint64_t r;
        for (b = 0; b < nb; b++) { for (k = 0; k < 16; k++) axh_round(acc, p + b * block + 64 * k, s + 8 * k); axh_scramble(acc, s + 192 - 64); }
        { const size_t ns = ((len - 1) - block * nb) / 64; for (k = 0; k < ns; k++) axh_round(acc, p + nb * block + 64 * k, s + 8 * k); }
        axh_round(acc, p + len - 64, s + 192 - 64 - 7);
        r = (uint64_t)len * AXH_P64_1;
        for (i = 0; i < 4; i++) r += axh_fold(acc[2 * i] ^ axf_rd64(s + 11 + 16 * i), acc[2 * i + 1] ^ axf_rd64(s + 11 + 16 * i + 8));
        return axh_aval(r);
    }
}

/* ---- encoder: the reference encoder of the fast profile (src/ax_fast_enc.h) in C, the same parses and the same bytes ---- */
#define AXE_LL_EXT 63u
#define AXE_ML_EXT 63u
#define AXE_DMAX ((1u << 20) - 1)
#define AXE_WIN AXF_BLOCK_MAX
#define AXE_NONE 0xffffffffu
static const uint32_t axe_mm[8] = { 16, 12, 8, 5, 16, 12, 8, 5 };
typedef struct { uint32_t mm; int depth, lazy, optimal; } axe_params;
static int axe_level(int level, axe_params* p) {
    if (level < 1 || level > 8) return 0;
    p->mm = axe_mm[level - 1]; p->optimal = level >= 5; p->lazy = 1; p->depth = p->optimal ? 512 : 64;
    return 1;
}
/* bytes a block of n input bytes can take in the worst case (see ax_fast_enc.h: n/32 covers the extensions) */
static size_t axe_bound(size_t n) { return AXF_BHDR + AXF_PAD + n + n / 32 + 16; }

static void axe_wr32(uint8_t* p, uint32_t v) { v = axf_le32(v); memcpy(p, &v, 4); }
static void axe_wr64(uint8_t* p, uint64_t v) { v = axf_le64(v); memcpy(p, &v, 8); }
static uint32_t axe_ctz(uint64_t x) {
#if defined(__GNUC__)
    return (uint32_t)__builtin_ctzll(x);
#else
    uint32_t n = 0; while (!(x & 1)) { x >>= 1; n++; } return n;
#endif
}
static uint32_t axe_clz(uint64_t x) {
#if defined(__GNUC__)
    return (uint32_t)__builtin_clzll(x);
#else
    uint32_t n = 0; while (!(x >> 63)) { x <<= 1; n++; } return n;
#endif
}

/* scratch of one call, sized for one block of up to bs bytes; arrays a parse does not need stay NULL */
typedef struct {
    uint32_t *tok, *head, *chain, *bl, *bd, *from, *mlen, *mdist, *bt;
    double* price; uint8_t *ext, *lit; uint32_t nt, ne; size_t nl;
} axe_ws;
static void axe_free(axe_ws* w) {
    free(w->tok); free(w->head); free(w->chain); free(w->bl); free(w->bd); free(w->from); free(w->mlen); free(w->mdist); free(w->bt);
    free(w->price); free(w->ext); free(w->lit); memset(w, 0, sizeof *w);
}
static int axe_alloc(axe_ws* w, const axe_params* P, size_t bs) {
    memset(w, 0, sizeof *w);
    w->tok = (uint32_t*)malloc(sizeof(uint32_t) * (bs / 4 + 16)); w->ext = (uint8_t*)malloc(bs / 16 + 64); w->lit = (uint8_t*)malloc(bs + 64);
    if (!w->tok || !w->ext || !w->lit) return 0;
    if (P->mm == 16 && !P->optimal) { w->head = (uint32_t*)malloc(sizeof(uint32_t) * ((1u << 17) + 1)); return w->head != NULL; }
    w->head = (uint32_t*)malloc(sizeof(uint32_t) << 16); w->chain = (uint32_t*)malloc(sizeof(uint32_t) * (bs + 1));
    if (!w->head || !w->chain) return 0;
    if (!P->optimal) return 1;
    w->bl = (uint32_t*)malloc(sizeof(uint32_t) * (bs + 1)); w->bd = (uint32_t*)malloc(sizeof(uint32_t) * (bs + 1));
    w->from = (uint32_t*)malloc(sizeof(uint32_t) * (bs + 1)); w->mlen = (uint32_t*)malloc(sizeof(uint32_t) * (bs + 1));
    w->mdist = (uint32_t*)malloc(sizeof(uint32_t) * (bs + 1)); w->price = (double*)malloc(sizeof(double) * (bs + 1));
    w->bt = (uint32_t*)malloc(sizeof(uint32_t) * 3 * (bs / 4 + 16));
    return w->bl && w->bd && w->from && w->mlen && w->mdist && w->price && w->bt;
}
static void axe_leb(axe_ws* w, uint32_t r) { while (r >= 255) { w->ext[w->ne++] = 255; r -= 255; } w->ext[w->ne++] = (uint8_t)r; }
/* one token of the greedy / optimal parses: ll literals then ml bytes at distance d; matches above AXF_COPY escape */
static void axe_put(axe_ws* w, uint32_t ll, uint32_t ml, uint32_t d, uint32_t mm) {
    uint32_t llf = ll, mlf;
    if (ll >= AXE_LL_EXT) { llf = AXE_LL_EXT; axe_leb(w, ll - AXE_LL_EXT); }
    if (ml <= AXF_COPY) mlf = ml - mm; else { mlf = AXE_ML_EXT; axe_leb(w, ml - AXF_COPY - 1); }
    w->tok[w->nt++] = llf | mlf << 6 | (d - AXF_MIN_DIST) << 12;
}
static void axe_lits(axe_ws* w, const uint8_t* p, size_t k) { if (k) memcpy(w->lit + w->nl, p, k); w->nl += k; }
/* the block's bytes from the scratch arrays into out (room bytes); size, or 0 when it does not fit */
static size_t axe_emit(const axe_ws* w, uint32_t tail, uint8_t* out, size_t room) {
    const size_t need = AXF_BHDR + 4 * (size_t)w->nt + w->ne + w->nl + AXF_PAD; uint8_t* p = out; uint32_t i;
    if (need > room) return 0;
    axe_wr32(p, w->nt); axe_wr32(p + 4, w->ne); axe_wr32(p + 8, tail); p += AXF_BHDR;
    for (i = 0; i < w->nt; i++) axe_wr32(p + 4 * (size_t)i, w->tok[i]);
    p += 4 * (size_t)w->nt;
    if (w->ne) memcpy(p, w->ext, w->ne);
    p += w->ne;
    if (w->nl) memcpy(p, w->lit, w->nl);
    p += w->nl; memset(p, 0, AXF_PAD); p += AXF_PAD;
    return (size_t)(p - out);
}
static uint32_t axe_match_len(const uint8_t* in, uint32_t a, uint32_t b, uint32_t lim) {
    uint32_t l = 4;
    while (l + 8 <= lim) { const uint64_t x = axf_rd64(in + a + l) ^ axf_rd64(in + b + l); if (x) { l += axe_ctz(x) >> 3; break; } l += 8; }
    return l > lim ? lim : l;
}
#define AXE_H4(in, p, hb) ((uint32_t)(axf_rd32((in) + (p)) * 0x9E3779B1u) >> (32 - (hb)))

/* greedy / lazy parse (levels 2..4): hash head + chain, the longest of `depth` candidates, a longer match one byte later wins */
static uint32_t axe_longest(const uint8_t* in, uint32_t n, uint32_t pos, const axe_ws* w, int depth, uint32_t* bd) {
    uint32_t cand = w->head[AXE_H4(in, pos, 15)], best = 0; int dep = depth; const uint32_t lim = n - pos - 8;
    while (cand != AXE_NONE && dep-- > 0) {
        const uint32_t d = pos - cand;
        if (d > AXE_DMAX + AXF_MIN_DIST || d > AXE_WIN) break;
        if (d >= AXF_MIN_DIST && axf_rd32(in + cand) == axf_rd32(in + pos)) {
            const uint32_t l = axe_match_len(in, pos, cand, lim);
            if (l > best) { best = l; *bd = d; if (l >= 512) break; }
        }
        cand = w->chain[cand];
    }
    return best;
}
static void axe_greedy(const uint8_t* in, uint32_t n, const axe_params* P, axe_ws* w, uint32_t* tail) {
    const uint32_t mm = P->mm; uint32_t pos = 0, anchor = 0, i;
    for (i = 0; i < (1u << 15); i++) w->head[i] = AXE_NONE;
    for (i = 0; i <= n; i++) w->chain[i] = AXE_NONE;
    while (pos + 12 < n) {
        const uint32_t h = AXE_H4(in, pos, 15); uint32_t bd = 0, best = axe_longest(in, n, pos, w, P->depth, &bd);
        if (best >= mm && P->lazy && pos + 13 < n) {
            uint32_t b2d = 0; const uint32_t b2 = axe_longest(in, n, pos + 1, w, P->depth, &b2d);
            if (b2 > best + 1) { w->chain[pos] = w->head[h]; w->head[h] = pos; pos++; continue; }
        }
        if (best >= mm) {
            uint32_t q;
            axe_lits(w, in + anchor, pos - anchor); axe_put(w, pos - anchor, best, bd, mm);
            for (q = pos; q < pos + best && q + 12 < n; q++) { const uint32_t hh = AXE_H4(in, q, 15); w->chain[q] = w->head[hh]; w->head[hh] = q; }
            pos += best; anchor = pos;
        } else { w->chain[pos] = w->head[h]; w->head[h] = pos; pos += 1 + ((pos - anchor) >> 6); }
    }
    axe_lits(w, in + anchor, n - anchor); *tail = n - anchor;
}
/* optimal parse (levels 5..8): the longest match at every position (depth 512), then a forward DP over the block that
 * prices literal bytes + 4 bytes per token + LAMBDA = 2 per token */
static void axe_optimal(const uint8_t* in, uint32_t n, const axe_params* P, axe_ws* w, uint32_t* tail) {
    const uint32_t mm = P->mm, lastp = n > 12 ? n - 12 : 0, LONG = 4096; uint32_t pos, i, j, anchor = 0, nm = 0, k;
    for (i = 0; i < (1u << 16); i++) w->head[i] = AXE_NONE;
    for (i = 0; i <= n; i++) { w->chain[i] = AXE_NONE; w->bl[i] = 0; w->bd[i] = 0; }
    for (pos = 0; pos < lastp; pos++) {
        const uint32_t h = AXE_H4(in, pos, 16); uint32_t cand = w->head[h], best = 0, bdist = 0; int dep = P->depth; const uint32_t lim = n - pos - 8;
        while (cand != AXE_NONE && dep-- > 0) {
            const uint32_t d = pos - cand; if (d > AXE_DMAX + AXF_MIN_DIST || d > AXE_WIN) break;
            if (d >= AXF_MIN_DIST && axf_rd32(in + cand) == axf_rd32(in + pos)) {
                const uint32_t l = axe_match_len(in, pos, cand, lim);
                if (l > best) { best = l; bdist = d; if (l >= LONG) break; }
            }
            cand = w->chain[cand];
        }
        w->chain[pos] = w->head[h]; w->head[h] = pos;
        if (best >= mm) { w->bl[pos] = best; w->bd[pos] = bdist; }
        if (best >= LONG) {   /* a long run: the positions inside it match the same source with the remaining length */
            const uint32_t skip = best - LONG;
            for (k = 1; k <= skip && pos + k < lastp; k++) { w->bl[pos + k] = best - k; w->bd[pos + k] = bdist; }
            pos += skip;
        }
    }
    for (i = 0; i <= n; i++) { w->price[i] = 1e300; w->from[i] = 0; w->mlen[i] = 0; w->mdist[i] = 0; }
    w->price[0] = 0;
    for (i = 0; i < n; i++) {
        const double pi = w->price[i]; uint32_t L, top, l; double c;
        if (pi + 1.0 < w->price[i + 1]) { w->price[i + 1] = pi + 1.0; w->from[i + 1] = i; w->mlen[i + 1] = 0; }
        L = w->bl[i]; if (!L) continue;
        c = pi + 4.0 + 2.0;
        top = L < AXF_COPY ? L : AXF_COPY;
        for (l = mm; l <= top; l++) { j = i + l; if (c < w->price[j]) { w->price[j] = c; w->from[j] = i; w->mlen[j] = l; w->mdist[j] = w->bd[i]; } }
        if (L > AXF_COPY) { j = i + L; if (c < w->price[j]) { w->price[j] = c; w->from[j] = i; w->mlen[j] = L; w->mdist[j] = w->bd[i]; } }
    }
    for (j = n; j > 0; ) { i = w->from[j]; if (w->mlen[j]) { w->bt[3 * nm] = i; w->bt[3 * nm + 1] = w->mlen[j]; w->bt[3 * nm + 2] = w->mdist[j]; nm++; } j = i; }
    while (nm-- > 0) {
        const uint32_t st = w->bt[3 * nm], l = w->bt[3 * nm + 1], d = w->bt[3 * nm + 2];
        axe_lits(w, in + anchor, st - anchor); axe_put(w, st - anchor, l, d, mm); anchor = st + l;
    }
    axe_lits(w, in + anchor, n - anchor); *tail = n - anchor;
}
/* sparse parse (level 1, min match 16; HW-ENC2 + ENC3 + SKIP10 of the reference encoder): an 8-byte hash key plus 2 more
 * key bytes, one slot per hash; table positions every 2nd byte, probes every 3rd; backward extension; a key hit rejected
 * for length steps like a miss; matches of 65..128 bytes as pieces of <= 64 */
static void axe_sparse(const uint8_t* in, uint32_t n, const axe_params* P, axe_ws* w, uint32_t* tail) {
    enum { HB = 17, SA = 3, SB = 2, INS = 8, SKIP = 8, SPLIT = 128 };
    const uint32_t mm = P->mm, DUMMY = 1u << HB; uint32_t* T = w->head; uint32_t pos = 0, anchor = 0, ip = 0, i;
    for (i = 0; i <= DUMMY; i++) T[i] = 0;
#define AXE_H8(p) ((uint32_t)((axf_rd64(p) * 0x9E3779B185EBCA87ull) >> (64 - HB)))
    if (n >= 80) {
        const uint32_t last = n - 16;
        while (pos < last) {
            uint32_t c, len = 0, d = 0, probe, ll, llf;
            {   const uint32_t a = ip, b = ip + SB;
                T[a < pos ? AXE_H8(in + a) : DUMMY] = a; T[b < pos ? AXE_H8(in + b) : DUMMY] = b;
                ip += a < pos ? (b < pos ? 2 * SB : SB) : 0;
                for (; ip < pos; ip += SB) T[AXE_H8(in + ip)] = ip; }
            c = T[AXE_H8(in + pos)];
            {   const uint64_t k = axf_rd64(in + pos);
                if (((axf_rd64(in + c) ^ k) | ((axf_rd64(in + c + 8) ^ axf_rd64(in + pos + 8)) & 0xffffull)) == 0 && c < pos) {
                    d = pos - c;
                    if (d < AXF_MIN_DIST) { d *= (AXF_MIN_DIST + d - 1) / d; if (d <= pos && axf_rd64(in + pos - d) == k) c = pos - d; else d = 0; }
                    if (d) {
                        const uint32_t lim = n - pos; uint32_t l = 8;
                        for (;;) { uint64_t x;
                                   if (l + 8 > lim) { while (l < lim && in[c + l] == in[pos + l]) l++; break; }
                                   x = axf_rd64(in + c + l) ^ axf_rd64(in + pos + l); if (x) { l += axe_ctz(x) >> 3; break; } l += 8; }
                        len = l;
                    }
                } }
            if (!len) { pos += SA + ((pos - anchor) >> SKIP); continue; }
            probe = pos;
            {   uint32_t room = pos - anchor; if (room > pos - d) room = pos - d;
                while (room >= 8) { const uint64_t x = axf_rd64(in + pos - 8) ^ axf_rd64(in + pos - 8 - d);
                    if (x) { const uint32_t k = axe_clz(x) >> 3; pos -= k; len += k; room = 0; break; } pos -= 8; len += 8; room -= 8; }
                if (room && pos >= d + 8) {
                    const uint64_t x = axf_rd64(in + pos - 8) ^ axf_rd64(in + pos - 8 - d); uint32_t k = x ? axe_clz(x) >> 3 : 8; if (k > room) k = room;
                    pos -= k; len += k;
                } else while (room && in[pos - 1] == in[pos - 1 - d]) { pos--; len++; room--; } }
            if (len < mm) { pos = probe + SA + ((probe - anchor) >> SKIP); continue; }
            ll = pos - anchor; axe_lits(w, in + anchor, ll);
            llf = ll; if (ll >= AXE_LL_EXT) { llf = AXE_LL_EXT; axe_leb(w, ll - AXE_LL_EXT); }
            {   uint32_t rest = len, first = 1, mlf;
                if (rest > AXF_COPY && rest <= SPLIT)
                    while (rest > AXF_COPY) { const uint32_t piece = rest - AXF_COPY < mm ? rest - mm : AXF_COPY;
                        w->tok[w->nt++] = (first ? llf : 0u) | (piece - mm) << 6 | (d - AXF_MIN_DIST) << 12; first = 0; rest -= piece; }
                if (rest <= AXF_COPY) mlf = rest - mm; else { mlf = AXE_ML_EXT; axe_leb(w, rest - AXF_COPY - 1); }
                w->tok[w->nt++] = (first ? llf : 0u) | mlf << 6 | (d - AXF_MIN_DIST) << 12; }
            {   const uint32_t end = pos + len;
                if (ip < end) { for (; ip + 8 < end && ip < last; ip += SB * INS) T[AXE_H8(in + ip)] = ip; ip = end + ((SB - end % SB) % SB); }
                if (end - 2 < last) T[AXE_H8(in + end - 2)] = end - 2;
                pos = end; anchor = end; }
        }
    }
#undef AXE_H8
    axe_lits(w, in + anchor, n - anchor); *tail = n - anchor;
}

/* ---- API ---- */
static void axfast_empty_archive(uint8_t* p) {   /* libaceapex's empty archive: header version 2, block size 65536, no blocks */
    memset(p, 0, 68); memcpy(p, "ACEPX2\0\0", 8); axe_wr32(p + 8, 2); axe_wr32(p + 20, 65536); axe_wr64(p + 28, axfast_xxh3(p, 0));
}
static int axfast_is_empty(const uint8_t* p, size_t n) {
    int i;
    if (n < 68 || memcmp(p, "ACEPX2\0\0", 8) != 0 || axf_rd32(p + 8) != 2 || axf_rd64(p + 12) != 0 || axf_rd32(p + 20) == 0 || axf_rd32(p + 24) != 0) return 0;
    for (i = 36; i < 68; i++) if (p[i]) return 0;
    return 1;
}
size_t axfast_bound(size_t n) {
    const size_t nb = (n + AXF_BLOCK_MAX - 1) / AXF_BLOCK_MAX;
    return n ? 68 + 8 * (nb + 1) + nb * axe_bound(AXF_BLOCK_MAX) + 64 : 68;
}
int64_t axfast_compress(const void* src, size_t n, void* dst, size_t cap, int level) {
    const uint8_t* in = (const uint8_t*)src; uint8_t* out = (uint8_t*)dst; axe_params P; axe_ws w;
    uint64_t nb64, off = 0; uint32_t nb, b; size_t head; const uint32_t bs = AXF_BLOCK_MAX;
    if (!axe_level(level, &P) || !dst) return AXFAST_ERR_DATA;
    if (n == 0) { if (cap < 68) return AXFAST_ERR_BUFFER; axfast_empty_archive(out); return 68; }
    if (!src) return AXFAST_ERR_DATA;
    nb64 = ((uint64_t)n + bs - 1) / bs; if (nb64 > 0xffffffffu) return AXFAST_ERR_DATA;
    nb = (uint32_t)nb64; head = 68 + 8 * ((size_t)nb + 1);
    if (cap < head) return AXFAST_ERR_BUFFER;
    if (!axe_alloc(&w, &P, n < bs ? n : bs)) { axe_free(&w); return AXFAST_ERR_MEMORY; }
    for (b = 0; b < nb; b++) {
        const size_t o = (size_t)b * bs; const uint32_t k = (uint32_t)(n - o < bs ? n - o : bs); uint32_t tail = 0; size_t c;
        axe_wr64(out + 68 + 8 * (size_t)b, off);
        w.nt = w.ne = 0; w.nl = 0;
        if (P.optimal) axe_optimal(in + o, k, &P, &w, &tail);
        else if (P.mm == 16) axe_sparse(in + o, k, &P, &w, &tail);
        else axe_greedy(in + o, k, &P, &w, &tail);
        c = axe_emit(&w, tail, out + head + off, cap - head - (size_t)off);
        if (!c) { axe_free(&w); return AXFAST_ERR_BUFFER; }
        off += c;
    }
    axe_free(&w);
    axe_wr64(out + 68 + 8 * (size_t)nb, off);
    memset(out, 0, 68); memcpy(out, "ACEPX2\0\0", 8);
    axe_wr32(out + 8, AXF_VERSION); axe_wr64(out + 12, (uint64_t)n); axe_wr32(out + 20, bs); axe_wr32(out + 24, nb);
    axe_wr64(out + 28, axfast_xxh3(in, n)); axe_wr32(out + 36, AXF_PROFILE_FAST); axe_wr32(out + 40, P.mm); axe_wr64(out + 44, off);
    return (int64_t)(head + off);
}
int64_t axfast_original_size(const void* src, size_t n) {
    axf_arc_t a; const uint8_t* p = (const uint8_t*)src;
    if (!p) return AXFAST_ERR_DATA;
    if (axfast_is_empty(p, n)) return 0;
    if (axf_open(p, n, &a) != 0 || a.orig > (uint64_t)INT64_MAX) return AXFAST_ERR_DATA;
    return (int64_t)a.orig;
}
uint64_t axfast_stored_xxh3(const void* src, size_t n) { return n >= 68 ? axf_rd64((const uint8_t*)src + 28) : 0; }
int64_t axfast_decompress(const void* src, size_t n, void* dst, size_t cap) {
    axf_arc_t a; const uint8_t* p = (const uint8_t*)src; uint8_t* out = (uint8_t*)dst; uint32_t b;
    if (!p) return AXFAST_ERR_DATA;
    if (axfast_is_empty(p, n)) return 0;
    if (axf_open(p, n, &a) != 0) return AXFAST_ERR_DATA;
    if (a.orig > cap) return AXFAST_ERR_BUFFER;
    if (!out) return AXFAST_ERR_DATA;
    for (b = 0; b < a.nb; b++) if (!axf_dec_block_of(&a, b, out + (size_t)b * a.bs)) return AXFAST_ERR_DATA;
    return (int64_t)a.orig;
}
int64_t axfast_decompress_range(const void* src, size_t n, uint64_t off, uint64_t len, void* dst, size_t cap) {
    axf_arc_t a; const uint8_t* p = (const uint8_t*)src; uint8_t* out = (uint8_t*)dst; uint8_t* tmp = NULL; uint64_t pos, end;
    if (!p) return AXFAST_ERR_DATA;
    if (axfast_is_empty(p, n)) return (off == 0 && len == 0) ? 0 : AXFAST_ERR_DATA;
    if (axf_open(p, n, &a) != 0) return AXFAST_ERR_DATA;
    if (off > a.orig || len > a.orig - off) return AXFAST_ERR_DATA;
    if (len == 0) return 0;
    if (len > cap) return AXFAST_ERR_BUFFER;
    if (!out) return AXFAST_ERR_DATA;
    for (pos = off, end = off + len; pos < end; ) {
        const uint32_t b = (uint32_t)(pos / a.bs); const uint64_t bst = (uint64_t)b * a.bs; const uint32_t bl = axf_block_len(&a, b);
        uint64_t take = bst + bl - pos; if (take > end - pos) take = end - pos;
        if (pos == bst && take == bl) {   /* the whole block lies in the range: straight into dst */
            if (!axf_dec_block_of(&a, b, out + (pos - off))) { free(tmp); return AXFAST_ERR_DATA; }
        } else {
            if (!tmp && !(tmp = (uint8_t*)malloc(a.bs))) return AXFAST_ERR_MEMORY;
            if (!axf_dec_block_of(&a, b, tmp)) { free(tmp); return AXFAST_ERR_DATA; }
            memcpy(out + (pos - off), tmp + (pos - bst), (size_t)take);
        }
        pos += take;
    }
    free(tmp);
    return (int64_t)len;
}
