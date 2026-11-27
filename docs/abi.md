# API and ABI

Use C17 and include `<surco/surco.h>`. The public header is C++ compatible.
The shared library uses ELF SONAME `libsurco.so.0`. Releases in the 0.x
series are pre-1.0, so build consumers against the headers for the installed
release. Archive compatibility is governed by the separate format version.

Create encoder and decoder contexts through the library. Their streams belong
to the caller, start at byte zero and must remain open. Indexed writing
requires an empty seekable update stream. Decoding and recovery require
seekable inputs. The library copies the encoder schema and input record data
and writes decoded records into the caller's array. The array needs at least
the channel count. Independent contexts can run in parallel on separate
streams. Each context and its stream are single-threaded.

Finish an encoder to flush the last block and write the footer, then destroy
it and close the stream. Destroy does not implicitly finish. API argument
errors can be corrected and a rejected backwards timestamp does not add a
record. An I/O or block failure can invalidate a context. Recovery writes a
fresh archive to a distinct empty stream. The caller verifies the output
through `surco_verify` before promoting it. Decode returns `SURCO_END` when
the selected interval is exhausted. It does not consume a row above the
upper bound.

The internal NASM routines use the [System V AMD64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI).
They are stackless leaf functions and use caller-saved general and XMM
registers. Transform pointers can be disjoint or exactly aliased. Their
16-byte vector alignment is unrestricted, and C callers use naturally
aligned `uint32_t` arrays. Partial overlap is outside the contract. A
count of zero does not access either pointer. The Assembly objects declare
a non-executable GNU stack.
