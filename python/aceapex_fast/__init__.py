"""aceapex-fast: the FAST profile of ACEAPEX (C99 library axfast) for Python.

compress(data, level=1) -> bytes           level 1..8 (= libaceapex levels 11..18), same archive bytes as the C library
decompress(archive, verify=True) -> bytes    verify: XXH3 of the output must equal the header (corrupt -> AxfastError)
decompress_into(archive, out, verify=True) -> int    into a writable buffer (bytearray, memoryview, numpy), no new object
decompress_range(archive, off, length) -> bytes   decodes only the 1 MiB blocks that cover [off, off + length);
                                           no checksum (it covers the whole original): structural checks only
xxh3(data) -> int                          XXH3-64 (seed 0), the checksum stored in the archive header
AxfastError (a ValueError) on a corrupt archive, a bad level or a range outside the original.
"""
from ._axfast import AxfastError, C_VERSION, compress, decompress, decompress_into, decompress_range, xxh3

__version__ = "0.1.0"
__all__ = ["AxfastError", "compress", "decompress", "decompress_into", "decompress_range", "xxh3", "__version__", "C_VERSION"]
