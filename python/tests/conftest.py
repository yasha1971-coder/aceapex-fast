"""Builds the C helpers of the repository (tests/gen.c: the CI inputs, tests/axc.c: the C reference compressor) once
per session with the system C compiler ($CC or cc) and provides the inputs and the reference archives."""
import os, pathlib, subprocess, shutil
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
INPUTS = ["empty", "one", "small", "text", "dna", "zeros", "rand", "period", "mixed"]


@pytest.fixture(scope="session")
def tools(tmp_path_factory):
    d = tmp_path_factory.mktemp("axfast")
    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        pytest.skip("no C compiler for the reference helpers")
    for name, srcs, flags in (("gen", ["tests/gen.c"], []), ("axc", ["tests/axc.c", "axfast.c"], []),
                              ("xxh3_scalar", ["tests/xxh3_dump.c", "axfast.c"], ["-DAXFAST_NO_SIMD"])):
        subprocess.run([cc, "-std=c99", "-O2", *flags, "-I", str(ROOT), *[str(ROOT / s) for s in srcs], "-o", str(d / name)], check=True)
    (d / "in").mkdir()
    subprocess.run([str(d / "gen"), str(d / "in")], check=True)
    return d


@pytest.fixture(scope="session")
def inputs(tools):
    return {n: (tools / "in" / n).read_bytes() for n in INPUTS}


def reference(tools, name, level):
    return subprocess.run([str(tools / "axc"), str(level), str(tools / "in" / name)], check=True, capture_output=True).stdout
