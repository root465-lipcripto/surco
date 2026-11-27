# Surco file format v1

All fixed-width integers are little-endian and all offsets are absolute byte
positions. An archive consists of a header, zero or more blocks, an optional
index and a mandatory footer. There is no alignment padding. Encoders emit
version 1. Readers require version 1 and zero in reserved fields.

## Header

| Offset | Type | Field |
|---:|---|---|
| 0 | 8 bytes | `53 55 52 43 4f 0d 0a 1a` |
| 8 | u32 | Version, 1 |
| 12 | u32 | Header bytes, `24 + 32 * channels` |
| 16 | u8 | Index flag, 0 or 1 |
| 17 | u8 | Channel count, 1–32 |
| 18 | u8 | Timestamp unit (0 µs, 1 ns, 2 ms, 3 s) |
| 19 | u8 | Reserved, 0 |
| 20 | 32 bytes × channels | NUL-terminated and zero-padded channel names |
| end − 4 | u32 | CRC32C of all preceding header bytes |

Names are unique and match ASCII `[A-Za-z_][A-Za-z0-9_]{0,30}`. The units
describe the stored timestamps and impose no epoch. The format preserves
unsigned 64-bit timestamps and signed 32-bit values. Duplicate timestamps are
valid and retain input order. v1 has no arbitrary metadata field.

## Block

| Offset | Type | Field |
|---:|---|---|
| 0 | 4 bytes | ASCII `BLK1` |
| 4 | u32 | Codec, 0 raw or 1 compressed |
| 8 | u64 | Block sequence number, starting at zero |
| 16 | u32 | Sample count N, 1–1024 |
| 20 | u32 | Payload length |
| 24 | u32 | Raw length, `N * (8 + 4 * channels)` |
| 28 | u32 | CRC32C of the exact payload |
| 32 | u64 | First timestamp |
| 40 | u64 | Last timestamp |
| 48 | u32 | CRC32C of offset 0 through 47 |
| 52 | bytes | Encoded payload |

Each block covers a contiguous run of records. Ranges must be nondecreasing
within and across blocks. The maximum raw payload is 139,264 bytes. A
compressed payload must be at least `1 + 5 * channels` bytes and strictly
smaller than its corresponding raw payload. Both forms must reconstruct
exactly N rows, match the endpoint timestamps and consume the entire payload.
The raw form stores `u64 timestamp` followed by each channel's `i32` as a
two's-complement word, row by row. See [Codec](codec.md) for compressed payloads.

## Index and footer

When the header flag is 1, a contiguous array follows the last block.
It contains one entry per block in sequence order. The entry at position i
stores these fields and must match its corresponding block header exactly.

| Offset | Type | Field |
|---:|---|---|
| 0 | u64 | Block offset |
| 8 | u64 | First timestamp |
| 16 | u64 | Last timestamp |
| 24 | u32 | Count |
| 28 | u32 | Block extent, 52 plus payload bytes |

The footer occupies the last 36 bytes of all complete archives.

| Offset | Type | Field |
|---:|---|---|
| 0 | 4 bytes | ASCII `END1` |
| 4 | u64 | Byte following the last block |
| 12 | u64 | Number of index entries |
| 20 | u64 | Total record count |
| 28 | u32 | CRC32C of the index bytes, 0 when empty or absent |
| 32 | u32 | CRC32C of offset 0 through 31 |

The footer offset must equal the end of the final block, or the end of the
header for an empty archive. With no index the footer immediately follows
the blocks and index entries must be zero. Otherwise the gap is exactly
32 bytes per entry. Implementations must validate all extents before reads.
Files larger than signed 64-bit seek offsets are outside the implementation.
CRC32C uses the reflected Castagnoli polynomial `0x82f63b78` and all-one
initial and final XOR values. It detects accidental corruption. It provides
no authenticity or tamper resistance.
