"""rvcls.py DIS... - RV-DIAG-1: instruction classes of the fast-profile block decoder (axf_dec_block with its inlined
per-min-match bodies; local .L labels belong to the function) in riscv64 objdump -dr output."""
import sys, re, os
P = {"ld/sd data": r'\s(ld|sd)\s+\w+,\s*-?\d+\((?!sp\))\w+\)', "ld/sd sp": r'\s(ld|sd)\s+\w+,\s*-?\d+\(sp\)',
     "lbu": r'\slbu\s', "sb": r'\ssb\s', "lw/lwu/sw": r'\s(lw|lwu|sw)\s', "lh/sh": r'\s(lhu?|sh)\s',
     "shifts/or (byte assembly)": r'\s(slli|srli|or)\s'}
for f in sys.argv[1:]:
    fn = None; body = {}
    for l in open(f).read().split("\n"):
        m = re.match(r'^[0-9a-f]+ <(.*)>:$', l)
        if m and not m.group(1).startswith(".L"): fn = m.group(1); body[fn] = []; continue
        if fn: body[fn].append(l)
    for name in [k for k in body if "axf_dec_block" in k]:
        B = body[name]; ins = [l for l in B if re.match(r'^\s+[0-9a-f]+:\s', l)]
        memcpy = sum(1 for l in B if re.search(r'R_RISCV_CALL(_PLT)?\s+memcpy', l))
        print("%s %s: insns %d | memcpy calls %d | %s" % (os.path.basename(f), name[:22], len(ins), memcpy,
              " | ".join("%s %d" % (k, sum(1 for l in ins if re.search(p, l))) for k, p in P.items())))
