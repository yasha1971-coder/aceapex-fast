#!/bin/bash -eu
# ClusterFuzzLite build: axfast.c with the sanitizer flags, fuzz/fuzz_axfast.c linked with libFuzzer, and a seed corpus
# from fuzz/make_seeds.c.
$CC $CFLAGS -std=c99 -I. -c axfast.c -o axfast.o
$CC $CFLAGS -std=c99 -I. -c fuzz/fuzz_axfast.c -o fuzz_axfast.o
$CXX $CXXFLAGS fuzz_axfast.o axfast.o $LIB_FUZZING_ENGINE -o "$OUT/fuzz_axfast"
$CC $CFLAGS -std=c99 -I. fuzz/make_seeds.c axfast.o -o make_seeds
mkdir -p seeds && ./make_seeds seeds && (cd seeds && zip -q "$OUT/fuzz_axfast_seed_corpus.zip" ./*)
printf '[libfuzzer]\nmax_len = 300000\nrss_limit_mb = 2560\n' > "$OUT/fuzz_axfast.options"
