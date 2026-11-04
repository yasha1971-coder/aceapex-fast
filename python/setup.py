# setup.py - the C extension: aceapex_fast/_axfast.c + axfast.c (symlinked from the repository root)
from setuptools import Extension, setup

setup(ext_modules=[Extension(
    "aceapex_fast._axfast",
    sources=["aceapex_fast/_axfast.c", "axfast.c"],
    include_dirs=["."],
    extra_compile_args=["-O2"],
)])
