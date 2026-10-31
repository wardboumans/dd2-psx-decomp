"""Unpack DD2's DIRINFO archive.

Usage: python extract_dirinfo.py <DIRINFO> <out_dir>

Layout (from the loader at 0x80051c58 / 0x80051ff4 in SCUS_943.50):
the first 5 sectors are a table of 24-byte entries
    char   name[18]   e.g. "LEV0\\FONT.BNK", NUL-terminated
    u16    sector     offset in 2048-byte sectors from the start of DIRINFO
    u32    size       in bytes
"""
import os
import struct
import sys

SECTOR = 2048
TABLE_SIZE = 5 * SECTOR
ENTRY = 24


def main():
    src, out_dir = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = f.read()
    listing = []
    # the table ends mid-entry; the trailing partial slot is junk
    for pos in range(0, TABLE_SIZE - ENTRY + 1, ENTRY):
        raw_name = data[pos:pos + 18].split(b"\0")[0]
        if not raw_name:
            continue
        sector, size = struct.unpack_from("<HI", data, pos + 18)
        name = raw_name.decode("ascii")
        start = sector * SECTOR
        if start + size > len(data):
            sys.exit(f"{name}: extends past end of archive")
        path = os.path.join(out_dir, *name.split("\\"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as o:
            o.write(data[start:start + size])
        listing.append(f"{sector:6d}  {size:9d}  {name}")
    print("sector       size  name")
    print("\n".join(listing))
    print(f"{len(listing)} files")


if __name__ == "__main__":
    main()
