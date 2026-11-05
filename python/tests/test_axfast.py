"""Gates of the Python binding: archives == the C library byte for byte, round trips, ranges, corrupt archives raise."""
import random
import pytest
import aceapex_fast as af
from conftest import INPUTS, reference

LEVELS = range(1, 9)


@pytest.mark.parametrize("name", INPUTS)
@pytest.mark.parametrize("level", LEVELS)
def test_bytes_equal_c_and_round_trip(tools, inputs, name, level):
    data = inputs[name]
    z = af.compress(data, level)
    assert z == reference(tools, name, level)
    assert af.decompress(z) == data
    assert af.decompress(z, verify=False) == data
    rnd = random.Random(level * 1000 + len(data))
    for _ in range(32):
        off = rnd.randrange(len(data) + 1); n = rnd.randrange(len(data) - off + 1)
        assert af.decompress_range(z, off, n) == data[off:off + n]


@pytest.mark.parametrize("name", ["text", "dna", "mixed", "small", "rand"])
def test_corrupt_archive_raises_never_wrong_bytes(inputs, name):
    data = inputs[name]
    rnd = random.Random(len(data))
    for level in (1, 4, 8):
        z = af.compress(data, level)
        for _ in range(200):
            b = bytearray(z)
            for _ in range(rnd.randrange(1, 5)):
                b[rnd.randrange(len(b))] ^= 1 << rnd.randrange(8)
            try:
                out = af.decompress(bytes(b))
            except af.AxfastError:
                continue
            assert out == data          # a flip in the pad or in an unused field may decode correctly, never wrongly
        for cut in (0, 1, 67, 68, len(z) // 2, len(z) - 1):
            with pytest.raises(af.AxfastError):
                af.decompress(z[:cut])
            if len(data):
                with pytest.raises(af.AxfastError):
                    af.decompress_range(z[:cut], 0, 1)


def test_errors_and_types(inputs):
    data = inputs["text"]
    with pytest.raises(af.AxfastError):
        af.compress(data, 0)
    with pytest.raises(af.AxfastError):
        af.compress(data, 9)
    z = af.compress(data)
    with pytest.raises(af.AxfastError):
        af.decompress_range(z, len(data), 1)
    with pytest.raises(af.AxfastError):
        af.decompress_range(z, 1, len(data))
    assert af.decompress_range(z, len(data), 0) == b""
    assert af.decompress(memoryview(z)) == data and af.compress(bytearray(data)) == z
    assert af.decompress(af.compress(b"")) == b""
    assert af.xxh3(b"") == 0x2D06800538D394C2
    with pytest.raises(TypeError):
        af.compress("text")


@pytest.mark.parametrize("name", ["text", "mixed", "empty", "one"])
def test_decompress_into(inputs, name):
    data = inputs[name]
    for level in (1, 5):
        z = af.compress(data, level)
        out = bytearray(len(data) + 7)
        assert af.decompress_into(z, out) == len(data) and bytes(out[:len(data)]) == data
        mv = memoryview(bytearray(len(data)))
        assert af.decompress_into(z, mv, verify=False) == len(data) and mv.tobytes() == data
        if data:
            with pytest.raises(af.AxfastError):
                af.decompress_into(z, bytearray(len(data) - 1))
            b = bytearray(z); b[len(b) // 2] ^= 0x10
            try:
                af.decompress_into(bytes(b), out)
            except af.AxfastError:
                pass
            else:
                assert bytes(out[:len(data)]) == data
        with pytest.raises(TypeError):
            af.decompress_into(z, bytes(len(data)))     # not writable


def test_xxh3_simd_equals_scalar(tools, inputs):
    """af.xxh3 (AVX2 / NEON / scalar, whatever this host picks) == the C scalar build on every CI input and on slices"""
    import subprocess
    names = list(inputs)
    want = subprocess.run([str(tools / "xxh3_scalar"), *[str(tools / "in" / n) for n in names]], check=True, capture_output=True, text=True).stdout.split()
    assert [f"{af.xxh3(inputs[n]):016x}" for n in names] == want
    data = inputs["mixed"]
    for n in list(range(0, 2000, 7)) + [65536, 65537, 1048575, len(data)]:
        sl = tools / "slice"; sl.write_bytes(data[:n])
        got = subprocess.run([str(tools / "xxh3_scalar"), str(sl)], check=True, capture_output=True, text=True).stdout.strip()
        assert f"{af.xxh3(data[:n]):016x}" == got
