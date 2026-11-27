# Research and format choices

Surco is a sequential block codec for a fixed integer schema with a
separately documented container. An encoder processes a bounded buffer.
Raw fallback protects against block payload expansion. The archive index
trades a header pass at open for selective payload reads.

- [Gorilla](https://www.vldb.org/pvldb/vol8/p1816-teller.pdf) compresses
  timestamps with delta-of-delta and floating-point values with XOR and
  leading/trailing zero reuse. Surco shares the timing insight and uses
  varints and integer-channel deltas with a different wire format.
- [Sprintz](https://arxiv.org/abs/1808.02515) applies integer prediction,
  small-block bitpacking and zero-run handling. Surco uses modular
  first-order prediction with bit widths covering each channel in a
  block and optionally encodes runs of equal deltas. It targets archival
  files and its predictors reset at every block.
- [TSZ for integral time series](https://github.com/qsib-cbie/tsz)
  is another small integer time-series compression approach. Its delta
  modeling and fixed-width packing have separate encoding rules from Surco.
- [Protocol Buffers encoding](https://protobuf.dev/programming-guides/encoding/)
  documents base-128 varints and ZigZag. Surco uses the same integer
  mappings while requiring minimal encodings and imposing its own bounds.
- [LZ4](https://github.com/lz4/lz4) and [Zstandard](https://github.com/meta/zstd)
  encode generic byte sequences. Zstandard is a comparison baseline and
  has no role in Surco's encoder or decoder. Their frame formats do not
  define Surco schemas, timestamp indexes or recovery.
- [CRC32C in RFC 3720](https://www.rfc-editor.org/rfc/rfc3720#appendix-B)
  provides Castagnoli parameters and vectors. Surco applies checksums
  to structural sections, and validates index entries against headers.
- [NASM](https://www.nasm.us/doc/), the
  [System V AMD64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI),
  and the [Intel instruction manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
  guide the ELF implementation and CPUID dispatch. SSE2 supplies integer
  lanes, SSE4.2 supplies CRC32C, and a future AVX2 backend would require
  CPUID plus OSXSAVE and XCR0 checks for XMM/YMM state.
- [CMake](https://cmake.org/documentation/) and
  [Clang](https://clang.llvm.org/docs/) document the native build and
  instrumentation. The C reference protects portability and gives the
  Assembly tests a differential oracle.

Timestamps are already normalized to a schema-declared integer unit.
Surco records that unit and performs no conversion while encoding.
Replaying values can convert differences to time delays, without
altering stored integers. Temporal bounds describe an ordering and
allow a search for overlapping blocks.

GitHub returned no repositories named Surco matching both telemetry
and compression in the prepublication search for this project.
Other projects use the name Surco. The GitHub owner and repository
pair identifies this project.
