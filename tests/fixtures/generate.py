"""Build format v1 oracle fixtures from independent byte layouts."""
from pathlib import Path
from struct import pack

HERE = Path(__file__).resolve().parent
SAMPLES = [(9, -7), (10, -7), (11, -7)]


def crc(data):
    state = 0xffffffff
    for byte in data:
        state ^= byte
        for _ in range(8):
            state = (state >> 1) ^ (0x82f63b78 if state & 1 else 0)
    return state ^ 0xffffffff


def protect(data):
    return data + pack("<I", crc(data))


def archive(payload, codec):
    header = protect(pack("<8sIIBBBB32s", b"SURCO\r\n\x1a", 1, 56, 1, 1, 0, 0, b"volts"))
    block = protect(pack("<4sIQIIIIQQ", b"BLK1", codec, 0, 3, len(payload), 36,
                         crc(payload), 9, 11)) + payload
    index = pack("<QQQII", len(header), 9, 11, 3, len(block))
    footer = protect(pack("<4sQQQI", b"END1", len(header) + len(block), 1, 3, crc(index)))
    return header + block + index + footer


if __name__ == "__main__":
    (HERE / "records.csv").write_bytes(b"timestamp_us,volts\n9,-7\n10,-7\n11,-7\n")
    (HERE / "raw-v1.surco").write_bytes(archive(b"".join(pack("<Qi", *row) for row in SAMPLES), 0))
    compressed = bytes((1, 1, 1, 0)) + pack("<i", -7)
    (HERE / "compressed-v1.surco").write_bytes(archive(compressed, 1))
