"""summary.py LOG - per (file, level): median over rounds of each build's run median; NEW / OLD, OLD2 / OLD (A/A),
PC / OLD (positive control). Flags: AA when |OLD2/OLD - 1| >= 3 %, PC_NOT_SEEN when PC is not below OLD, LOSS when
NEW / OLD < 0.99 (the stop condition of the V48 gate)."""
import sys, statistics
runs = {}
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) >= 7 and p[0] == "R" and p[5] == "median":
        runs.setdefault((p[1], p[2]), {}).setdefault(p[4], []).append(float(p[6]))
bad = 0
print("file level | OLD | OLD2 (A/A) | NEW (V48) | PC (control) | flags")
for (f, lv), d in sorted(runs.items()):
    m = {k: statistics.median(v) for k, v in d.items()}
    o = m["OLD"]; aa = m["OLD2"] / o - 1; new = m["NEW"] / o - 1; pc = m["PC"] / o - 1
    flags = ([] if abs(aa) < 0.03 else ["AA"]) + ([] if pc < 0 else ["PC_NOT_SEEN"]) + ([] if new >= -0.01 else ["LOSS"])
    bad += "LOSS" in flags
    print(f"{f} {lv} | {o:.1f} | {m['OLD2']:.1f} ({aa:+.2%}) | {m['NEW']:.1f} ({new:+.2%}) | {m['PC']:.1f} ({pc:+.2%}) | {' '.join(flags) or 'ok'} | n={len(d['OLD'])}")
print("LOSS cells:", bad)
