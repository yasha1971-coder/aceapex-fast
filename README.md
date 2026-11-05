# aceapex-fast

**What.** The FAST profile of [ACEAPEX](https://github.com/yasha1971-coder/aceapex) as a C99 library in two files
(`axfast.h`, `axfast.c`, ~700 lines, no dependency): an LZ77 codec with fixed 32-bit tokens and no entropy stage, built
for decompression speed and random access to 1 MiB blocks. Archives are byte-identical to libaceapex at levels 11-18
(`aceapex_fast` 1-8 in lzbench), and each library reads the other's.

**For whom.** Programs that want the fast decoder without the C++ library, zstd or threads: embedded and static builds,
other languages through a C ABI, readers that need a byte range of a large archive without decoding the rest.

**Example.**

```c
#include "axfast.h"
#include <stdlib.h>

size_t cap = axfast_bound(n);
unsigned char* z = malloc(cap);
int64_t zs = axfast_compress(src, n, z, cap, 1);                    /* level 1..8 */
int64_t d  = axfast_decompress(z, (size_t)zs, out, n);              /* == n */
int64_t r  = axfast_decompress_range(z, (size_t)zs, off, len, buf, len);  /* bytes [off, off + len) */
/* negative results: AXFAST_ERR_BUFFER, AXFAST_ERR_DATA, AXFAST_ERR_MEMORY */
```

Build: `cc -std=c99 -O2 -c axfast.c` (C99, `-pedantic` clean; GCC and Clang get an AVX2 clone of the block decoder
picked at load time on x86-64 Linux).

**Numbers.**

| what | result | log |
|---|---|---|
| bytes vs libaceapex 2.6.1 (levels 1-8 = 11-18, 1 thread) | 72/72 identical: 6 conformance inputs, silesia.tar, enwik8, chr1 | [results/fastlib-bytes-2026-11-04.log](results/fastlib-bytes-2026-11-04.log) |
| decode vs libaceapex (silesia.tar, level 1, AMD EPYC 4344P, 1 core, no `-march`, median of 5) | 11 723 vs 11 909 MB/s (0.985 x) | [results/fastlib-speed-2026-11-04.log](results/fastlib-speed-2026-11-04.log) |
| encode level 1 vs libaceapex (same run) | 1 026.6 vs 1 069.0 MB/s (0.961 x), ratio 61.57 % | same |
| the decoder in lzbench (libaceapex, same block decoder; Zen 5 C4D EPYC 9B45, silesia.tar, 1 core, median of 5) | `aceapex_fast -1` 15 950 MB/s vs `zxc 0.15.0 -1` 11 027 MB/s | [aceapex results/zen5-lzbench2-2026-10-28.log](https://github.com/yasha1971-coder/aceapex/blob/v2.5.0/results/zen5-lzbench2-2026-10-28.log) |
| big-endian (s390x under qemu) and 32-bit x86 | round trips ok, archive bytes == x86-64 (8 inputs x 8 levels) | [results/fastlib-portability-2026-11-04.txt](results/fastlib-portability-2026-11-04.txt) |
| corrupted archives (ASan + UBSan, 180 s) | 10 500 mutants: 3 904 decoded to the declared size, 6 596 rejected, 0 wrong results; negative control caught | [results/fastlib-fuzz-2026-11-04.log](results/fastlib-fuzz-2026-11-04.log) |

CI runs the same gates on every push: C99 `-pedantic -Werror` with GCC and Clang, round trips, 32-bit, s390x under
qemu, ASan + UBSan with 60 s of fuzzing, and byte-for-byte comparison with libaceapex 2.6.1.

**Python.** `python/`: a CPython extension over the same two files, no runtime dependency
(`pip install ./python`; CI builds wheels for manylinux x86_64/aarch64 and macOS arm64/x86_64, not published).
`compress(data, level=1)`, `decompress(data, verify=True)`, `decompress_into(data, out, verify=True)` (into a writable
buffer, no new object), `decompress_range(data, off, length)`, `xxh3(data)`. `verify` checks the XXH3-64 of the output
against the header, so a damaged archive raises `AxfastError` instead of returning wrong bytes. Archives are
byte-identical to the C library (pytest, 9 inputs x 8 levels). XXH3 uses AVX2 (x86-64, picked at run time) or NEON
(AArch64), scalar otherwise; all three give the same hashes. Speed on one core, median of 5
([results/fastlib-py-speed2-2026-11-05.log](results/fastlib-py-speed2-2026-11-05.log)):

| MB/s | C | Python verify=False | Python verify=True |
|---|---|---|---|
| silesia.tar, 1 MiB block archives (`decompress`) | 14 646 | 14 603 | 11 840 |
| enwik8, 1 MiB block archives (`decompress`) | 15 439 | 15 112 | 12 057 |
| silesia.tar, whole file (`decompress_into`, reused buffer) | 11 215 | 11 208 | 8 897 |
| enwik8, whole file (`decompress_into`) | 12 666 | 12 673 | 10 089 |
| silesia.tar, whole file (`decompress`: a new 212 MB `bytes` each call) | 2 244 (C, fresh buffer) | 2 200 | 2 191 |

A new `bytes` object of 212 MB costs the page faults of fresh memory (C with a fresh buffer pays the same);
`decompress_into` avoids them.

**Limits.**

- CPU encoder only, one thread per call (the calls are independent: compress blocks of a file on your own threads,
  or use libaceapex for its thread pool). The GPU decoder of the format lives in the research of ACEAPEX, not here.
- Levels 1-8: level 1 is the fast sparse parser (61.57 % on silesia.tar); levels 5-8 are an optimal parse, ~25x slower
  to encode and very slow on short-period data.
- Block size 1 MiB, fixed (the format allows smaller blocks; this library writes 1 MiB, reads any).
- Byte order: little-endian and big-endian hosts both work and write the same bytes.
- `axfast_decompress` does not check the stored XXH3; `axfast_xxh3` and `axfast_stored_xxh3` let the caller do it.
- No streaming API: the archive and the output are whole buffers.

**Links.**

- Format: [docs/FORMAT_FAST.md](https://github.com/yasha1971-coder/aceapex/blob/main/docs/FORMAT_FAST.md) in ACEAPEX.
- ACEAPEX DOI (all versions): [10.5281/zenodo.20440964](https://doi.org/10.5281/zenodo.20440964); 2.6.1:
  [10.5281/zenodo.23930083](https://doi.org/10.5281/zenodo.23930083).
- lzbench: `aceapex_fast` is in [inikep/lzbench](https://github.com/inikep/lzbench) since #355, 2.6.1 since
  [#357](https://github.com/inikep/lzbench/pull/357).

License: MIT; XXH3 secret from xxHash, BSD 2-Clause, see [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).
