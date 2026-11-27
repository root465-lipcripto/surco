# Assembly and dispatch

`surco_delta_sse2` subtracts the preceding word in four lanes with
`psubd`. Shifting the input register and retaining the last input lane
supplies history between vectors. `surco_prefix_sse2` makes an inclusive
four-lane scan with two shifts and `paddd`, then broadcasts the final
prefix to carry across vectors. Both finish residual words with scalar
instructions and use unaligned vector moves. All arithmetic wraps modulo
2³².

`surco_crc_sse42` uses the hardware CRC32C instruction for eight-byte
groups then individual bytes. It updates an uncomplemented state. The
portable nibble-table backend and accelerated backend produce identical
checksums.

The runtime checks CPUID leaf 1 for SSE2 and SSE4.2 separately. A forced
`scalar` uses C for transforms and checksums. `sse2` uses NASM transforms
and uses the accelerated checksum when SSE4.2 is available. `auto` selects
the available transform backend. The build targets baseline x86-64.
There is no AVX2 backend.

The binary contains the three NASM functions. Guard-page and register
preservation tests run on the SSE2 target when available. The file byte
comparisons exercise dispatch through both the library and command line.
