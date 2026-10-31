"""Print initialized data from the PSX executable at a RAM address.

Usage: python exe_data.py <SCUS_943.50> <address> <count> [type]

type is one of s8 u8 s16 u16 s32 u32 (default u32). Values are printed as
C initializers, one row of up to 8 per line, so they can be pasted into source.
"""
import struct
import sys

HEADER = 0x800
TYPES = {"s8": "b", "u8": "B", "s16": "h", "u16": "H", "s32": "i", "u32": "I"}


def main():
    path, address, count = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 0)
    kind = sys.argv[4] if len(sys.argv) > 4 else "u32"
    data = open(path, "rb").read()
    load = struct.unpack_from("<I", data, 0x18)[0]
    size = struct.unpack_from("<I", data, 0x1C)[0]
    fmt = TYPES[kind]
    width = struct.calcsize(fmt)
    if not load <= address and address + count * width <= load + size:
        sys.exit(f"{address:#x} is outside the initialized image {load:#x}-{load + size:#x}")
    off = HEADER + address - load
    values = struct.unpack_from("<" + fmt * count, data, off)
    hexed = kind.startswith("u") and width > 1
    cells = [f"0x{v:0{width * 2}X}" if hexed else str(v) for v in values]
    for i in range(0, len(cells), 8):
        print("    " + ", ".join(cells[i:i + 8]) + ",")


if __name__ == "__main__":
    main()
