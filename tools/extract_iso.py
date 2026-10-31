"""Extract the ISO9660 filesystem from a raw MODE2/2352 PSX data track.

Usage: python extract_iso.py <track01.bin> <out_dir>

Form 1 files are written as plain 2048-byte-sector data. Form 2 / interleaved
files (XA audio, STR video) are written as raw 2352-byte sectors so that no
subheader or audio data is lost.
"""
import os
import struct
import sys

RAW = 2352
DATA_OFF = 24  # 12 sync + 4 header + 8 subheader (Mode 2 Form 1)
XA_FORM2 = 0x1000
XA_INTERLEAVED = 0x2000


def read_sector(f, lba):
    f.seek(lba * RAW)
    return f.read(RAW)


def read_data(f, lba, size):
    out = bytearray()
    for i in range((size + 2047) // 2048):
        out += read_sector(f, lba + i)[DATA_OFF:DATA_OFF + 2048]
    return bytes(out[:size])


def iter_records(f, lba, size):
    data = read_data(f, lba, size)
    pos = 0
    while pos < len(data):
        length = data[pos]
        if length == 0:
            pos = (pos // 2048 + 1) * 2048  # records never span sectors
            continue
        rec = data[pos:pos + length]
        pos += length
        ext_lba = struct.unpack_from("<I", rec, 2)[0]
        ext_size = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        if name in (b"\x00", b"\x01"):
            continue
        su = 33 + name_len + (1 - name_len % 2)
        xa_attr = 0
        if rec[su + 6:su + 8] == b"XA":
            xa_attr = struct.unpack_from(">H", rec, su + 4)[0]
        yield name.decode("ascii").split(";")[0], ext_lba, ext_size, flags, xa_attr


def extract(f, lba, size, out_dir, listing, prefix=""):
    os.makedirs(out_dir, exist_ok=True)
    for name, ext_lba, ext_size, flags, xa_attr in iter_records(f, lba, size):
        path = os.path.join(out_dir, name)
        rel = prefix + name
        if flags & 0x02:
            listing.append(f"{ext_lba:7d}  {'<DIR>':>10}  {rel}/")
            extract(f, ext_lba, ext_size, path, listing, rel + "/")
            continue
        sectors = (ext_size + 2047) // 2048
        if xa_attr & (XA_FORM2 | XA_INTERLEAVED):
            with open(path, "wb") as o:
                for i in range(sectors):
                    o.write(read_sector(f, ext_lba + i))
            kind = "raw2352"
        else:
            with open(path, "wb") as o:
                o.write(read_data(f, ext_lba, ext_size))
            kind = ""
        listing.append(f"{ext_lba:7d}  {ext_size:10d}  {rel}  {kind}".rstrip())


def main():
    bin_path, out_dir = sys.argv[1], sys.argv[2]
    with open(bin_path, "rb") as f:
        pvd = read_sector(f, 16)[DATA_OFF:DATA_OFF + 2048]
        if pvd[1:6] != b"CD001":
            sys.exit("no ISO9660 primary volume descriptor at LBA 16")
        root = pvd[156:156 + 34]
        root_lba = struct.unpack_from("<I", root, 2)[0]
        root_size = struct.unpack_from("<I", root, 10)[0]
        listing = []
        extract(f, root_lba, root_size, out_dir, listing)
    print("    LBA        size  path")
    print("\n".join(listing))


if __name__ == "__main__":
    main()
