# Security policy

## Supported versions

Only `main`. There are no releases; fixes land on `main`.

## Reporting

Use GitHub private vulnerability reporting only: the **Security** tab of this repository -> **Report a vulnerability**.
Direct link: https://github.com/yasha1971-coder/aceapex-fast/security/advisories/new
Do not open a public issue for a vulnerability.

First reply within 7 days. No fix date is promised.

Include: commit, the archive or input that triggers it (or how to make it), the call, what happened.

## What counts

- a read or write outside the buffers given to the library (any function, any input);
- a crash or an abort on any input;
- a hang: a call that does not return on a finite input;
- on a checked path (Python `decompress` / `decompress_into` with `verify=True`, or C `axfast_decompress` followed by
  `axfast_xxh3` == `axfast_stored_xxh3`): success with bytes that differ from the original.

## What does not

- other bytes from `axfast_decompress` or `axfast_decompress_range` on a damaged archive: they do not check the stored
  XXH3 by design (README); verify as above;
- slow compression at levels 5-8 (optimal parse).
