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
`746cc23ec0ac4679dad45734a630951e08ed6e81`. The capture tests use
pseudo-terminals. Physical Pico 2 W capture remains pending.
[WSila language reference](https://github.com/root465-lipcripto/WSila/blob/746cc23ec0ac4679dad45734a630951e08ed6e81/docs/language.md)
