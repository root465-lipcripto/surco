# Current limits

- The supported deployment target is Linux x86-64. The baseline package
  requires the glibc symbol floor recorded in its release notes. Build-time
  sanitizers and optional fuzzing are development tools.
- v1 stores 1–32 named integer channels with one fixed schema and
  nondecreasing 64-bit unsigned timestamps. Floating point, dynamic
  schemas, arbitrary metadata and cross-language native bindings are
  outside this version.
- The decoder requires seekable storage. Binary stdout is supported for
  unindexed encoding. Pipe it to a file before decoding.
- Index validation reads all block headers once per open. The index
  reduces payload work, and does not make opening a new file logarithmic.
- CRC32C detects accidental errors. Choose a separate mechanism when
  provenance or cryptographic integrity matters.
- An archive can be larger than the original measurements after envelope
  and index overhead. Version 1 has no general-purpose entropy codec.
- Text CSV is canonicalized. Recovery needs an intact schema and has a
  bounded resynchronization scan. Some kinds of corruption make loss
  counts uncertain.
- The named output is placed only after success. The output directory
  must support hard links and permit temporary file creation. Abrupt
  termination can leave a temporary file, and the CLI does not promise
  crash durability.
- Timed replay delays between CSV rows, capped at 60 seconds per
  scaled gap. Terminal/output overhead adds to elapsed time.
- Serial ingestion was verified with pseudo-terminals. A physical Pico
  2 W session remains pending.
