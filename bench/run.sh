#!/usr/bin/env bash
# TB-3-ARM-1 A/B: binaries A and A2 (AXF_FP=0, built twice: the A/A control), V48 (1), W16 (2), PC (9, positive control:
# one extra 32-byte copy per fast token) from the same axfast.c with the same flags. Round trips of every FILE by every
# binary first (archives must be equal across the builds: same encoder), then ROUNDS rounds on the first FILE, the order
# of the five binaries rotating each round, each run = K decodes (median and best).
# Usage: run.sh FLAGS PIN ROUNDS K FILE...
set -uo pipefail
FLAGS=$1; PIN=$2; ROUNDS=$3; K=$4; shift 4
D=$(cd "$(dirname "$0")" && pwd); S=$D/..; O=${OUT:-/tmp/tb3}; mkdir -p "$O"; CC=${CC:-gcc}
echo "== $(date -u +%FT%TZ) $(uname -m) $($CC --version | head -1) FLAGS [$FLAGS] pin $PIN rounds $ROUNDS K $K"
for v in A:0 A2:0 V48:1 W16:2 PC:9; do
  n=${v%%:*}; f=${v#*:}
  $CC -O3 $FLAGS -DAXF_FP=$f -I"$S" -c "$S/axfast.c" -o "$O/axf_$n.o" || { echo "BUILD_FAILED $n"; exit 3; }
  $CC -O3 $FLAGS -I"$S" -o "$O/b_$n" "$D/tb3bench.c" "$O/axf_$n.o" || { echo "BUILD_FAILED $n"; exit 3; }
  echo "built $n (AXF_FP=$f) obj $(sha256sum "$O/axf_$n.o" | cut -c1-12)"
done
for fpath in "$@"; do
  b=$(basename "$fpath")
  for lv in 1 2; do   # levels 1 and 2 (min match 16 and 12) take axf_dec_block_tok_mm, the function the variants change
    for n in A A2 V48 W16 PC; do "$O/b_$n" comp "$fpath" $lv "$O/$b.$n.L$lv.axf" | sed "s/^/[$n] /"; [ "${PIPESTATUS[0]}" = 0 ] || { echo "RT_FAIL $n $b L$lv"; exit 4; }; done
    for n in A2 V48 W16 PC; do cmp -s "$O/$b.A.L$lv.axf" "$O/$b.$n.L$lv.axf" || { echo "ARCHIVE DIFFERS $n $b L$lv"; exit 4; }; done
    echo "archives of $b level $lv equal across the five builds"
  done
done
T=$1; A="$O/$(basename "$T").A.L1.axf"
for r in $(seq "$ROUNDS"); do
  case $((r % 5)) in 0) o="A A2 V48 W16 PC";; 1) o="A2 V48 W16 PC A";; 2) o="V48 W16 PC A A2";; 3) o="W16 PC A A2 V48";; *) o="PC A A2 V48 W16";; esac
  for n in $o; do echo "R $r $n $(taskset -c "$PIN" "$O/b_$n" dec "$A" "$T" "$K")"; done
done
echo "== end $(date -u +%FT%TZ)"
