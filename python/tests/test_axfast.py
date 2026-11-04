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
