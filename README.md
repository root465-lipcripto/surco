<picture><img src="assets/surco.svg" alt="Surco" width="190"></picture>

**Small records. Long histories.**

Surco is a native telemetry compression, storage and replay engine written
in C and x86-64 assembly, designed for bounded-memory processing of
time-series data. It stores exact integer samples locally, verifies archive
integrity and reads narrow time ranges from archived recordings.

## Install

Download the Linux x86-64 tarball and `SHA256SUMS` from the
[release page](https://github.com/root465-lipcripto/surco/releases).
Check the release notes for the package's glibc requirement.

```sh
sha256sum -c SHA256SUMS
tar -xzf surco-0.1.0-linux-x86_64.tar.gz
./surco-0.1.0-linux-x86_64/bin/surco doctor
```

The archive includes the CLI, static and shared `libsurco`, public header,
Python tools and reference documentation.

## Build from source

Use a C17 compiler, CMake 3.23+, Ninja, NASM on Linux x86-64, and Python 3
for tests and tools. Use `--preset scalar` for a build without NASM.

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset release && cmake --build --preset release && ctest --preset release
cmake --install build/release --prefix "$PWD/build/install"
```

## Record, verify and replay

```sh
surco=./build/release/surco
python3 tools/datasets.py periodic --rows 5000 > samples.csv
"$surco" pack samples.csv -o samples.surco
"$surco" inspect samples.surco --json
"$surco" verify samples.surco
"$surco" unpack samples.surco -o restored.csv
python3 tools/compare.py samples.csv restored.csv
"$surco" query samples.surco --from 120000 --to 220000
"$surco" replay samples.surco --speed 20
"$surco" recover samples.surco --output recovered.surco
python3 tools/visualize.py samples.surco demo.html --surco "$surco" --synthetic
```

Surco CSV begins with `timestamp_us`, `timestamp_ns`, `timestamp_ms`
or `timestamp_s` and one to 32 unique ASCII channel names. Data rows contain
a decimal unsigned timestamp and one signed 32-bit decimal per channel.
Timestamps may repeat and must be nondecreasing. CSV uses unquoted
comma-separated fields, no spaces or blank lines, and LF or CRLF if a
newline is present. A line is at most 2047 bytes before LF. The header may
be the only row. Less significant text choices such as leading zeroes
are canonicalized on export. A named output is created exclusively and
is published after successful completion.

Use `-` for standard streams. CSV stdin may be a pipe. Binary input must
be seekable and binary stdout omits the index. Use `pack --raw` to store
all blocks raw and `--no-index` to omit the index from a named output.
`query` includes both endpoints and verifies only the payloads of
overlapping blocks after validating the archive layout and index. `inspect`
reports structural metadata. `verify` checks the entire archive.
`replay --fast` emits CSV with no delays. Exit 0 means success, 1 means
processing failure and 2 means invalid arguments or unavailable backend.

Use [Serial capture](docs/embedded.md) for the USB protocol and WSila example.
See [Recovery](docs/recovery.md) for how to interpret incomplete archives.

## Native library and format

Include `<surco/surco.h>`. The library borrows your `FILE *`. The caller
owns the streams and record buffers. See the
[C reader](examples/c/read.c), [API and ABI](docs/abi.md),
[architecture](docs/architecture.md) and [format specification](docs/format-v1.md).

Surco compares each complete compressed payload against its raw form.
Independent blocks use timestamp deltas or delta-of-delta and bitpacked
or run-length-encoded channel deltas. Two NASM SSE2 routines perform the
channel delta and inverse transform. An additional NASM routine accelerates
CRC32C when the CPU supports SSE4.2. The C backend can be built and forced
independently. Use `surco doctor` to check dispatch and use
`--backend auto|scalar|sse2` on archive operations.

The [benchmark report](docs/benchmarks.md) contains measured file sizes,
transforms, command latencies and a Zstandard baseline. See
[testing](docs/testing.md), [limitations](docs/limitations.md),
[security](SECURITY.md), [roadmap](ROADMAP.md) and [MIT license](LICENSE).
