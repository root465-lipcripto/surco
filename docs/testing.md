# Verification

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --preset scalar
cmake --build --preset scalar
ctest --preset scalar
CC=clang cmake --preset sanitize -DSURCO_FUZZ=ON
cmake --build --preset sanitize
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --preset sanitize
```

The independent Python fixture generator describes the wire bytes
and calculates its checksums by bit, without linking Surco.
The tests check fixed raw and compressed archives byte for byte.
They also check extreme and repeated timestamps, signed limits,
different patterns, 32 channels, index mutations with recomputed CRC,
truncation, raw fallback, differential decode, query equivalence,
recovery, CSV errors and output collision handling.

The Assembly tests check scalar equivalence over random deterministic
inputs, aliasing, tails, register preservation, guard pages and
hardware/portable checksum equivalence. Compiler sanitizers instrument C.
The ordinary-build memory test processes 1.2 million records in a
~48 MB archive under a 32 MiB address-space limit. The limit test runs
outside sanitizer builds to accommodate their virtual shadow memory.
A scalar-only build reports the SSE2 suite as skipped.

The optional libFuzzer harness passes bounded buffers to the real decoder.
Keep the mutable corpus in the build directory.

```sh
mkdir -p build/sanitize/corpus
cp tests/fixtures/*.surco build/sanitize/corpus/
build/sanitize/surco_fuzz -max_total_time=15 -timeout=3 -max_len=16384 build/sanitize/corpus
```
