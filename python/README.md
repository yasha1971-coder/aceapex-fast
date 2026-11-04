# aceapex-fast for Python

```python
import aceapex_fast as af
z = af.compress(data, level=1)            # bytes, the same archive as the C library
assert af.decompress(z) == data
part = af.decompress_range(z, off, n)     # data[off:off + n], decodes only the covering 1 MiB blocks
```

A corrupt archive raises `aceapex_fast.AxfastError` (a `ValueError`). `decompress` checks the block structure and, by
default (`verify=True`), the XXH3-64 of the whole output against the header, so damaged literals raise too.
`decompress_range` cannot use that checksum (it covers the whole original): it does the structural checks only, and
a damaged literal inside the range comes back as wrong bytes. `xxh3(data)` returns the checksum for your own checks.
Build: `pip install ./python` (needs a C compiler). No runtime dependency. License: MIT (see ../LICENSE).
