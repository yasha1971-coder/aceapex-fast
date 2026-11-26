#!/usr/bin/env bash
# TB-3-ESC-1 A/B: OLD = the V48 decoder of aceapex-fast main b9edf87 (bench/old/axfast.c), OLD2 = the same
# source built again (A/A control), NEW = axfast.c of this branch (V48), PC = OLD with the first match copy of the
# fast path done twice (positive control). For each FILE and LEVEL: every build compresses (archives must be equal:
# the encoder is not changed) and decodes; then ROUNDS rounds, the order of the four builds rotating, each run = K
# decodes (median). Linux pins to CPU PIN (taskset); macOS runs unpinned.
# Usage: ab.sh "FLAGS" PIN ROUNDS K "LEVELS" FILE...
set -uo pipefail
FLAGS=$1; PIN=$2; ROUNDS=$3; K=$4; LEVELS=$5; shift 5
D=$(cd "$(dirname "$0")" && pwd); S=$D/..; O=${OUT:-/tmp/v48}; mkdir -p "$O/pc"; CC=${CC:-cc}
h() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -c1-12; else shasum -a 256 "$1" | cut -c1-12; fi; }
pin() { if command -v taskset >/dev/null; then taskset -c "$PIN" "$@"; else "$@"; fi; }
echo "== $(date -u +%FT%TZ) $(uname -sm) $($CC --version | head -1) FLAGS [$FLAGS] rounds $ROUNDS K $K levels [$LEVELS]"
cp "$D/old/axfast.c" "$D/old/axfast.h" "$O/pc/"
python3 - "$O/pc/axfast.c" <<'PY'
import sys
p = sys.argv[1]; t = open(p).read()
a = "{ const uint8_t* sm = d - dist; axf_cp32(d, sm); axf_cp32(d + 32, sm + 32); if (AXF_UNLIKELY(ml > 64u)) axf_cp32(d + 64, sm + 64); }"
assert t.count(a) == 1, "positive control: fast-path line not found"
open(p, "w").write(t.replace(a, a.replace("axf_cp32(d, sm);", "axf_cp32(d, sm); axf_cp32(d, sm);", 1)))
PY
# PREBUILT=dir: use dir/b_OLD, b_OLD2, b_NEW, b_PC built elsewhere (ace-core: gcc 13 static from Docker), no build here
[ -n "${PREBUILT:-}" ] && { for n in OLD OLD2 NEW PC; do cp "$PREBUILT/b_$n" "$O/b_$n" || exit 3; echo "prebuilt $n $(h "$O/b_$n")"; done; }
for v in OLD:$D/old OLD2:$D/old NEW:$S PC:$O/pc; do [ -n "${PREBUILT:-}" ] && break; n=${v%%:*}; src=${v#*:}
  $CC -O3 $FLAGS -I"$src" -c "$src/axfast.c" -o "$O/axf_$n.o" && $CC -O3 $FLAGS -I"$src" -o "$O/b_$n" "$D/tb3bench.c" "$O/axf_$n.o" || { echo "BUILD_FAILED $n"; exit 3; }
  echo "built $n from $src: axfast.c $(h "$src/axfast.c") obj $(h "$O/axf_$n.o")"
done
# PREARC=dir: take the archives from dir (name silesia / enwik8 / chr1 + .ace<level>, written by the 2.6.5 encoder) and
# do not compress here (ace-core: no heavy work outside the lock); otherwise OLD and NEW compress and must agree.
arc() { local s; s=$(basename "$1"); s=${s%.tar}; s=${s%.fa}; if [ -n "${PREARC:-}" ]; then echo "$PREARC/$s.ace$2"; else echo "$O/$(basename "$1").OLD.L$2.axf"; fi; }
for f in "$@"; do b=$(basename "$f")
  for lv in $LEVELS; do
    if [ -n "${PREARC:-}" ]; then echo "$b L$lv: archive $(arc "$f" $lv) ($(wc -c < "$(arc "$f" $lv)") B, 2.6.5 encoder)"; continue; fi
    for n in OLD NEW; do "$O/b_$n" comp "$f" $lv "$O/$b.$n.L$lv.axf" > "$O/c.txt" || { cat "$O/c.txt"; echo "RT_FAIL $n $b L$lv"; exit 4; }; done
    cmp -s "$O/$b.OLD.L$lv.axf" "$O/$b.NEW.L$lv.axf" || { echo "ARCHIVE DIFFERS NEW $b L$lv"; exit 4; }
    echo "$b L$lv: $(sed 's/.*-> //' "$O/c.txt"), archives of OLD and NEW equal, both decode byte for byte (OLD2 and PC are checked by every timed decode)"
  done
done
for f in "$@"; do b=$(basename "$f")
  for lv in $LEVELS; do A=$(arc "$f" $lv)
    for r in $(seq "$ROUNDS"); do
      case $((r % 4)) in 0) o="OLD OLD2 NEW PC";; 1) o="OLD2 NEW PC OLD";; 2) o="NEW PC OLD OLD2";; *) o="PC OLD OLD2 NEW";; esac
      for n in $o; do echo "R $b L$lv $r $n $(pin "$O/b_$n" dec "$A" "$f" "$K")"; done
    done
  done
done
echo "== end $(date -u +%FT%TZ)"
