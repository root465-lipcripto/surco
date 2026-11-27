# USB serial and WSila

The capture protocol is ASCII `timestamp_us,channel_id,value\n`.
A trailing CR before LF is accepted. Timestamps are unsigned 64-bit
microseconds. Channel IDs are zero-based decimal, and values are signed
32-bit decimals. With multiple channels, emit channel IDs in ascending
order with the same timestamp, starting at zero for every sample. For
example, with two channels:

```text
1000000,0,20
1000000,1,100
1100000,0,21
1100000,1,101
```

Capture opens only the named TTY for reading. It configures raw
8N1 and restores the previous terminal settings on normal cleanup.
Supported baud rates are 9600, 19200, 38400, 57600 and 115200.
The default is 115200. The receiver limits each frame to 127 bytes and
reports bad or incomplete frame counts on stderr. The idle deadline
defaults to 3000 ms and renews only when a complete sample is accepted.
An idle timeout, Ctrl-C, SIGTERM, or a sample limit flushes the archive.
A hard I/O failure discards a staged destination. Surco sends no serial
payload to the device.

```sh
surco capture --device /dev/ttyACM0 --samples 3 -o demo.surco
surco verify demo.surco
```

Use the device path you have independently identified. The
[WSila synthetic example](../examples/wsila/synthetic.wsila) emits three
fixed records using documented `serial.println` and `time.sleep`.  Its
source passes `wsilac check` at WSila commit
`746cc23ec0ac4679dad45734a630951e08ed6e81`. The automated capture tests use
pseudo-terminals for repeatability.
[WSila language reference](https://github.com/root465-lipcripto/WSila/blob/746cc23ec0ac4679dad45734a630951e08ed6e81/docs/language.md)

## Physical capture on a Pico 2 W

On 2026-11-27, a Raspberry Pi Pico 2 W running the WSila synthetic
example was connected over USB to Fedora 45. The example was
built for the RP2350 with Pico SDK 2.3.1 using the WSila commit above.
The host ran Surco 0.1.0 at commit
`9528b69d5c29b32f4782f5019f036105a1ce3b15`.

The example emits three records after a two-second startup delay.
Open its CDC port promptly after firmware boot. Use the device path
assigned by your host.

```sh
surco capture --device /dev/ttyACM0 --channels 1 --baud 115200 \
  --timeout-ms 9000 --samples 3 -o synthetic.surco
surco verify synthetic.surco
surco unpack synthetic.surco
```

Surco received three samples with zero malformed or incomplete frames.
Verification passed for the indexed 187-byte archive, which contained one
compressed block. The decoded CSV matched the example byte for byte.

```csv
timestamp_us,channel_0
0,20
100000,23
200000,21
```
