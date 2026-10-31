"""Assign every mapped function to a module-level source file.

Usage: python module_files.py <mapping dir> <PC STRUCTURE.md>

The PC decomp's STRUCTURE.md lists the original modules (link-order address
ranges, file names from the debug info). A function with a PC address goes to
the module whose range holds that address (or, between ranges, the module
with the nearest boundary). Ranges that cover PC-only code (DirectDraw, the
software rasterizer, the CRT) don't apply to the PSX, so functions there keep
the file chosen by hand. PSX-only functions keep their hand-chosen file,
normalised to the module names below.
"""
import json
import os
import re
import sys

PC_ONLY = ("span", "triangle", "line", "ot", "ddraw", "transparency", "libgte_extra")
# hand-chosen fine-grained files -> module files
NORMALISE = {
    "src/system/card_file.c": "src/system/card.c",
    "src/system/buffer_load.c": "src/system/file.c",
    "src/graphics/draw_buffers.c": "src/graphics/object.c",
    "src/system/arcsin.c": "src/system/maths.c",
}
# Where the PSX link order contradicts the PC range. Load_Sample_Bank sits inside file on the
# PSX (the PC moved it to its DirectSound code). Calc_Angle sits after Calc_Arc_Sin and
# before mpe_heap on the PSX, nowhere near debug's Profile_Init (no original name is known).
# Draw_Font_Poly and Decompress sit with the draw-buffer code (Allocate_OT, Draw_All); the prim
# buffer allocator between Update_Object and the sprite code (the PC moved it into font).
PSX_MODULE = {
    "Calc_Angle": "src/system/maths.c",
    "Load_Sample_Bank": "src/system/file.c",
    "Draw_Font_Poly": "src/graphics/object.c",
    "Decompress": "src/graphics/object.c",
    "Init_Primitive_Buffer": "src/graphics/object.c",
    "Alloc_Prim_Buffer": "src/graphics/object.c",
    "Free_Prim_Buffer": "src/graphics/object.c",
}
ROW = re.compile(r"^\| (0x[0-9a-f]+)(?:-(0x[0-9a-f]+)\+?)? \| (`[^|]*`) \|")


def load_rows(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        m = ROW.match(line)
        if not m:
            continue
        files = [f[:-4] + ".c" for f in re.findall(r"`(src/[^`]+\.cpp)`", m.group(3))]
        if files:
            rows.append([int(m.group(1), 16), int(m.group(2), 16) if m.group(2) else None, files])
    rows.sort()
    for i, r in enumerate(rows):
        if r[1] is None:
            r[1] = rows[i + 1][0] if i + 1 < len(rows) else r[0] + 1
    return rows


def pick(files, name, pc_file):
    """Choose between the two files of a shared row."""
    if len(files) == 1:
        return files[0]
    stems = [os.path.basename(f)[:-2] for f in files]
    low = name.lower()
    rules = {
        "cd": "cd_" in low or low.startswith("cd") or "_cd" in low,
        "anim": "animation" in low,
        "lens_flare": "lens" in low,
        "particles": any(k in (pc_file or "") for k in ("particle", "smoke", "fire", "spark", "steam")),
        "pause": "pause" in low,
        "league": any(k in low for k in ("league", "promote", "relegat", "points")),
    }
    for stem, f in zip(stems[1:], files[1:]):
        if rules.get(stem):
            return f
    return files[0]


def split_shared_rows(rows, shared, fmap):
    """A module is contiguous in link order, so a row that holds two files is
    split at one point per code block: the split (and file order) that agrees
    best with the per-function guesses from pick()."""
    changed = 0
    for i, keys in shared.items():
        a, b = rows[i][2]
        by_block = {}
        for k in keys:
            by_block.setdefault(k.split(":")[0] if ":" in k else "EXE", []).append(k)
        for ks in by_block.values():
            guess = [fmap[k]["file"] for k in ks]
            best = None
            for first, second in ((a, b), (b, a)):
                for cut in range(len(ks) + 1):
                    score = sum(g == (first if j < cut else second) for j, g in enumerate(guess))
                    if best is None or score > best[0]:
                        best = (score, first, second, cut)
            _, first, second, cut = best
            for j, k in enumerate(ks):
                f = first if j < cut else second
                if fmap[k]["file"] != f:
                    fmap[k]["file"] = f
                    changed += 1
    return changed


def main():
    mapping, structure = sys.argv[1], sys.argv[2]
    rows = load_rows(structure)
    path = os.path.join(mapping, "function_map.json")
    fmap = json.load(open(path))
    pc_file = {hex(q["pc_address"]): q["file"] for q in json.load(open(os.path.join(mapping, "pc_functions.json")))}
    changed = 0
    shared = {}  # row index -> map keys in PSX order (fmap is sorted by block and address)
    for k, v in fmap.items():
        new = v["file"]
        if v["pc"]:
            a = int(v["pc"], 16)
            # overlapping ranges: the one that starts last is the most specific;
            # between ranges: the module whose boundary is nearest
            inside = [i for i, r in enumerate(rows) if r[0] <= a < r[1]]
            if inside:
                i = max(inside, key=lambda i: rows[i][0])
            else:
                i = min(range(len(rows)), key=lambda i: min(abs(a - rows[i][0]), abs(a - (rows[i][1] - 1))), default=None)
            if i is not None and not any(os.path.basename(f)[:-2] in PC_ONLY for f in rows[i][2]):
                new = pick(rows[i][2], v["name"], pc_file.get(v["pc"]))
                if len(rows[i][2]) > 1 and v["name"] not in PSX_MODULE:
                    shared.setdefault(i, []).append(k)
        new = PSX_MODULE.get(v["name"], NORMALISE.get(new, new))
        if new != v["file"]:
            v["file"] = new
            changed += 1
    changed += split_shared_rows(rows, shared, fmap)
    with open(path, "w") as fh:
        json.dump(fmap, fh, indent=1)
    files = sorted({v["file"] for v in fmap.values()})
    print(f"{changed} entries moved; {len(files)} files:")
    print(" ".join(f.replace("src/", "") for f in files))


if __name__ == "__main__":
    main()
