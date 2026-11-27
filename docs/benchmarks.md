# Measured benchmarks

Five deterministic, 120,000-row datasets use two signed integer channels. Each row in the raw numeric representation is 16 bytes. The datasets cover constants, a random walk, periodic signals, noise and full-range random values with regular or irregular timestamps. The generator seed is 734521.

These numbers were measured on AMD Ryzen Z2 A, Fedora Linux 45 (KDE Plasma Desktop Edition), kernel 7.2.11-300.fc45.x86_64. The compiler was cc (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2), with `-O3 -DNDEBUG -std=c17` and LTO disabled. The assembler was NASM version 3.02 and the benchmarked source commit was `f7ce0a840b0ff5203361d3e582604905a6b8af6d`. Each row summarizes seven repetitions.

## Transform throughput

Each repetition applies a transform 32 times to 262,144 32-bit words, then verifies its round-trip. Values are decimal MB/s.

| Backend | Delta median [min–max] | Prefix median [min–max] |
|---|---:|---:|
| scalar | 6,733 [4,974–7,574] | 10,310 [8,355–10,927] |
| sse2 | 18,754 [13,100–19,469] | 16,034 [9,959–17,972] |

## End-to-end storage

Pack includes CSV parsing and indexed writes. Unpack includes CSV export. Query covers about 2% of each timeline and includes archive open and validation. Time is wall-clock milliseconds, including process startup and the measurement wrapper. Archive sizes include headers and the index.

| Dataset | Raw archive (B) | Surco (B) | Zstd -3 (B) | SSE2 pack (ms) | SSE2 unpack (ms) | SSE2 query (ms) |
|---|---:|---:|---:|---:|---:|---:|
| constant | 1,930,036 | 131,334 | 241,676 | 13.66 | 29.29 | 3.57 |
| walk | 1,930,036 | 333,021 | 718,135 | 22.25 | 41.67 | 4.89 |
| periodic | 1,930,036 | 153,037 | 487,026 | 15.95 | 30.46 | 3.90 |
| noisy | 1,930,036 | 557,593 | 959,688 | 18.62 | 31.33 | 3.50 |
| random | 1,930,036 | 1,201,916 | 1,215,507 | 26.13 | 36.34 | 4.52 |

The scalar and SSE2 encoders emitted equal file sizes in every workload. Faster transforms affect only part of end-to-end latency. Each pack was verified, every unpack was compared to the original CSV, and queries were compared to independent CSV filtering.

Zstandard v1.5.7 at level 3 compressed the complete raw Surco archive, including its metadata and checksums. This preserves the same measurements and schema. Zstandard timings cover byte compression and decompression from the raw archive. The output must be decompressed to use Surco's index, so no Zstandard selective query time is reported.

The [full results](benchmark-results.json) include minima, medians, maxima and peak RSS by operation (GNU time, KiB). The measurements describe this machine and these datasets. They are sensitive to scheduling, I/O cache and CPU frequency.

```sh
cmake --preset release && cmake --build --preset release
python3 tools/bench.py --rows 120000 --repetitions 7
build/release/surco benchmark --backend scalar
build/release/surco benchmark --backend sse2
```
