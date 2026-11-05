# Contributing

## Build and test

No build system: `axfast.c` + `axfast.h`. The tests are the CI steps in `.github/workflows/ci.yml`, run them as written:

    gcc -std=c99 -pedantic -Wall -Wextra -Werror -O2 -c axfast.c
    gcc -std=c99 -O2 -I. tests/gen.c -o gen && mkdir in && ./gen in
    gcc -std=c99 -O2 -I. tests/rt.c axfast.c -o rt && for f in in/*; do ./rt $f; done

A change is accepted with every CI job green: gcc and clang, C99 pedantic with no warnings, round trips, SIMD XXH3 ==
scalar, AArch64 and s390x under qemu, 32-bit, ASan+UBSan fuzzing with negative controls, bytes == libaceapex 2.6.1,
Python tests and wheels; ClusterFuzzLite on pull requests.

## Bit-perfect

Every decode is compared byte for byte with the original. Archives are byte for byte those of libaceapex at levels
11-18 (CI job `bytes`); a change that alters them is not accepted here.

## The format is frozen

The format is ACEAPEX's fast profile (`docs/FORMAT_FAST.md` in yasha1971-coder/aceapex). It does not change in a pull
request. A format change starts as an issue in ACEAPEX and ships only as a new header version there.

## Pull requests

- one topic per pull request; CI green before review;
- no force-push to a branch under review: add commits, merges are not squashed;
- commit subject: `area: what changed (numbers)`; the body says how it was measured and where the log is (`results/`).

## License

Contributions are accepted under the MIT license of this repository (`LICENSE`). Code taken from elsewhere keeps its
license and is listed in `THIRD_PARTY_LICENSES`.
