# Compressed block payload

The byte stream contains the timestamp stream followed by a channel stream
for each schema column. It carries no seek state from earlier blocks.

## Timestamps

The first timestamp is the block header value in all modes. The next
payload byte selects how to encode the remaining N−1 values.

| Mode | Encoding |
|---:|---|
| 0 | Absolute u64 timestamps, eight little-endian bytes each |
| 1 | Nonnegative timestamp deltas in canonical unsigned LEB128 |
| 2 | First delta as unsigned LEB128, then ZigZag of gap changes as unsigned LEB128 |

For mode 2, subtract the previous gap modulo 2⁶⁴, apply 64-bit ZigZag,
and reverse with modular addition. A reconstructed gap must not overflow
when added to the previous timestamp. Each LEB128 byte contributes seven
low-order bits with bit 7 set if another byte follows. A 64-bit value takes
one to ten bytes. The tenth byte can only contain bit zero and encodings
must use the minimum number of bytes.

## Channels

A channel starts with a mode byte and the initial value as a 32-bit word.
Subsequent values are deltas modulo 2³². ZigZag for an unsigned bit pattern
`u` of W bits is `(u << 1) XOR (0 - (u >> (W - 1)))`, truncated to W bits.
The inverse is `(z >> 1) XOR (0 - (z & 1))`.

- Modes 0–32 specify the number of low bits per ZigZag delta. Concatenate
  bits least-significant first, with the earliest bits in the least
  significant positions of each byte. The high bits of the final byte
  are zero padding. Mode 0 encodes N−1 zero deltas in zero bytes.
- Mode 33 stores runs of identical ZigZag deltas as canonical LEB128 pairs
  `(run length, delta)`. Each run has positive length, the delta fits in
  32 bits and runs sum to exactly N−1.
- Other values are reserved.

Sum the inverse ZigZag deltas modulo 2³² starting from the initial value.
Interpret the final words as signed two's complement.

## Encoder choice

Timestamp encoding chooses the smallest measured stream length, with ties
favoring delta over delta-of-delta and varints over fixed-width timestamps.
Per channel, run-length encoding wins only when shorter than the exact
bitpacked length. Surco then compares the entire compressed candidate
against the raw payload. The same block envelope, index extent and
per-archive overhead apply to either codec. A tie uses raw. This comparison
does not imply that a complete archive will always be smaller than its
binary records or input CSV.
