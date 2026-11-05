"""Python side of the overhead measurement (same work as tests/bench_blocks.c): FILE at level 1 as one archive per
1 MiB block and as one archive of the whole file; best of R of decompress(verify=False) and decompress(verify=True)
over all block archives and of the whole archive. Prints MB/s. Usage: python bench.py FILE R"""
import sys, time
import aceapex_fast as af
data = open(sys.argv[1], "rb").read(); R = int(sys.argv[2]); n = len(data); B = 1 << 20
blocks = [af.compress(data[o:o + B], 1) for o in range(0, n, B)]
z = af.compress(data, 1)
best = {}
def run(key, fn):
    t = time.perf_counter(); fn(); t = time.perf_counter() - t
    best[key] = min(best.get(key, 1e9), t)
def each(verify):   # every result dropped before the next call (as free() in the C side)
    for b in blocks:
        af.decompress(b, verify=verify)
out = bytearray(n)
for _ in range(R):
    run("whole_into_noverify", lambda: af.decompress_into(z, out, verify=False))
    run("whole_into_verify", lambda: af.decompress_into(z, out, verify=True))
    run("block_noverify", lambda: each(False))
    run("block_verify", lambda: each(True))
    run("whole_noverify", lambda: af.decompress(z, verify=False))
    run("whole_verify", lambda: af.decompress(z, verify=True))
print(" ".join(f"{k} {n / v / 1e6:.1f}" for k, v in best.items()))
