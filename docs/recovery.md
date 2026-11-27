# Verifying and recovering an archive

`surco verify FILE` validates header and footer CRCs, layout, index,
all payload CRCs and decoding invariants. `surco inspect FILE` reads
structural metadata. It does not perform the payload checks.

```sh
surco recover damaged.surco --output recovered.surco
surco verify recovered.surco
```

Recovery opens the input read-only and requires a valid schema header.
It keeps complete blocks with intact header and payload CRCs, monotonic
sequence/range and a valid decoded payload. If the header is valid and
the payload is bad, recovery skips that block using its validated extent.
When framing is damaged it scans for the next plausible CRC-protected
block header, using a total 16 MiB scan budget per invocation. It uses
the valid footer, when available, to avoid interpreting the index as data.
It can retain a suffix of valid blocks after damage. Recovered rows are
written in a new layout and receive a new index.

The CLI verifies the staged archive before placing it at the requested
path. It reports `recovered`, `lost_at_least`,
`loss_count_known`, `accepted_blocks` and `rejected_payload_blocks`.
An intact footer can establish the original declared sample count.
Otherwise the loss floor counts rejected, bounded payloads with valid
headers and the true loss can be larger. A zero loss floor with an
unknown total makes no completeness claim. Recovery cannot recreate
lost readings. CRCs protect against accidental corruption.
