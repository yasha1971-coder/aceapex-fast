/* _axfast.c - CPython extension over axfast.c (the C99 library one directory up). No runtime dependency.
 * compress(data, level=1) -> bytes; decompress(data, verify=True) -> bytes (verify: XXH3 of the output == header); decompress_range(data, off, length) -> bytes;
 * xxh3(data) -> int. Any bytes-like input (buffer protocol). The GIL is released while axfast works.
 * Errors: AxfastError (a ValueError) for a corrupt archive, a bad level or a range outside the original; MemoryError. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "axfast.h"

static PyObject* AxfastError;

static PyObject* raise_rc(int64_t rc, const char* what) {
    if (rc == AXFAST_ERR_MEMORY) return PyErr_NoMemory();
    if (rc == AXFAST_ERR_BUFFER) { PyErr_Format(AxfastError, "%s: output buffer too small (internal error)", what); return NULL; }
    PyErr_Format(AxfastError, "%s: invalid input (corrupt archive, bad level or range)", what);
    return NULL;
}

static PyObject* py_compress(PyObject* self, PyObject* args, PyObject* kw) {
    static char* kwl[] = {"data", "level", NULL};
    Py_buffer in; int level = 1; size_t cap; PyObject* out; int64_t rc;
    (void)self;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "y*|i:compress", kwl, &in, &level)) return NULL;
    if (level < AXFAST_LEVEL_MIN || level > AXFAST_LEVEL_MAX) {
        PyBuffer_Release(&in); PyErr_Format(AxfastError, "level must be %d..%d, not %d", AXFAST_LEVEL_MIN, AXFAST_LEVEL_MAX, level); return NULL;
    }
    cap = axfast_bound((size_t)in.len);
    if (cap > (size_t)PY_SSIZE_T_MAX) { PyBuffer_Release(&in); return PyErr_NoMemory(); }
    out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)cap);
    if (!out) { PyBuffer_Release(&in); return NULL; }
    Py_BEGIN_ALLOW_THREADS
    rc = axfast_compress(in.buf, (size_t)in.len, PyBytes_AS_STRING(out), cap, level);
    Py_END_ALLOW_THREADS
    PyBuffer_Release(&in);
    if (rc < 0) { Py_DECREF(out); return raise_rc(rc, "compress"); }
    if (_PyBytes_Resize(&out, (Py_ssize_t)rc) < 0) return NULL;
    return out;
}

static PyObject* py_decompress(PyObject* self, PyObject* args, PyObject* kw) {
    static char* kwl[] = {"data", "verify", NULL};
    Py_buffer in; int64_t n, rc; PyObject* out; int verify = 1; int ok = 1;
    (void)self;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "y*|p:decompress", kwl, &in, &verify)) return NULL;
    n = axfast_original_size(in.buf, (size_t)in.len);
    if (n < 0) { PyBuffer_Release(&in); return raise_rc(n, "decompress"); }
    if ((uint64_t)n > (uint64_t)PY_SSIZE_T_MAX) { PyBuffer_Release(&in); return PyErr_NoMemory(); }
    out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)n);
    if (!out) { PyBuffer_Release(&in); return NULL; }
    Py_BEGIN_ALLOW_THREADS
    rc = axfast_decompress(in.buf, (size_t)in.len, PyBytes_AS_STRING(out), (size_t)n);
    if (rc == n && verify) ok = axfast_xxh3(PyBytes_AS_STRING(out), (size_t)n) == axfast_stored_xxh3(in.buf, (size_t)in.len);
    Py_END_ALLOW_THREADS
    PyBuffer_Release(&in);
    if (rc != n) { Py_DECREF(out); return raise_rc(rc < 0 ? rc : AXFAST_ERR_DATA, "decompress"); }
    if (!ok) { Py_DECREF(out); PyErr_SetString(AxfastError, "decompress: XXH3 of the output differs from the archive header (corrupt archive)"); return NULL; }
    return out;
}

static PyObject* py_decompress_range(PyObject* self, PyObject* args) {
    Py_buffer in; unsigned long long off, len; int64_t rc; PyObject* out;
    (void)self;
    if (!PyArg_ParseTuple(args, "y*KK:decompress_range", &in, &off, &len)) return NULL;
    if (len > (unsigned long long)PY_SSIZE_T_MAX) { PyBuffer_Release(&in); return PyErr_NoMemory(); }
    {   const int64_t n = axfast_original_size(in.buf, (size_t)in.len);   /* checked before allocating len bytes */
        if (n < 0) { PyBuffer_Release(&in); return raise_rc(n, "decompress_range"); }
        if (off > (uint64_t)n || len > (uint64_t)n - off) { PyBuffer_Release(&in); return raise_rc(AXFAST_ERR_DATA, "decompress_range"); } }
    out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)len);
    if (!out) { PyBuffer_Release(&in); return NULL; }
    Py_BEGIN_ALLOW_THREADS
    rc = axfast_decompress_range(in.buf, (size_t)in.len, (uint64_t)off, (uint64_t)len, PyBytes_AS_STRING(out), (size_t)len);
    Py_END_ALLOW_THREADS
    PyBuffer_Release(&in);
    if (rc != (int64_t)len) { Py_DECREF(out); return raise_rc(rc < 0 ? rc : AXFAST_ERR_DATA, "decompress_range"); }
    return out;
}

static PyObject* py_xxh3(PyObject* self, PyObject* args) {
    Py_buffer in; uint64_t h;
    (void)self;
    if (!PyArg_ParseTuple(args, "y*:xxh3", &in)) return NULL;
    Py_BEGIN_ALLOW_THREADS
    h = axfast_xxh3(in.buf, (size_t)in.len);
    Py_END_ALLOW_THREADS
    PyBuffer_Release(&in);
    return PyLong_FromUnsignedLongLong((unsigned long long)h);
}

static PyMethodDef methods[] = {
    {"compress", (PyCFunction)(void (*)(void))py_compress, METH_VARARGS | METH_KEYWORDS, "compress(data, level=1) -> bytes (level 1..8)"},
    {"decompress", (PyCFunction)(void (*)(void))py_decompress, METH_VARARGS | METH_KEYWORDS, "decompress(archive, verify=True) -> bytes; verify checks the header XXH3"},
    {"decompress_range", py_decompress_range, METH_VARARGS, "decompress_range(archive, off, length) -> bytes [off, off + length)"},
    {"xxh3", py_xxh3, METH_VARARGS, "xxh3(data) -> int: XXH3-64 (seed 0), the archive header's checksum"},
    {NULL, NULL, 0, NULL}
};
static struct PyModuleDef mod = { PyModuleDef_HEAD_INIT, "_axfast", "aceapex-fast C extension", -1, methods, NULL, NULL, NULL, NULL };

PyMODINIT_FUNC PyInit__axfast(void) {
    PyObject* m = PyModule_Create(&mod);
    if (!m) return NULL;
    AxfastError = PyErr_NewException("aceapex_fast.AxfastError", PyExc_ValueError, NULL);
    if (!AxfastError || PyModule_AddObject(m, "AxfastError", AxfastError) < 0) { Py_XDECREF(AxfastError); Py_DECREF(m); return NULL; }
    Py_INCREF(AxfastError);
    if (PyModule_AddStringConstant(m, "C_VERSION", AXFAST_VERSION_STRING) < 0) { Py_DECREF(m); return NULL; }
    return m;
}
