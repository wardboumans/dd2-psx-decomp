"""Check low-confidence matches against the call graph of trusted matches.

Usage: python check_matches.py <mapping dir> [--promote]

For every low-confidence entry in function_map.json, compares the PSX function's
callers and callees that have trusted names (manual or high confidence) with the
callers and callees of the proposed PC function. With --promote, entries with at
least MIN_AGREE agreeing links and no disagreeing ones become high confidence
(evidence "graph-checked"); the rest are listed for manual review.
"""
import json
import os
import sys
from collections import defaultdict

MIN_AGREE = 2


def psx_key(address):
    """function_map key for a PSX address from psx_functions.json."""
    if address.startswith("ov1::"):
        return "RESULTS:" + address[5:]
    v = int(address, 16)
    if 0x801C4000 <= v < 0x801E0000:
        return f"FRONTEND:{address}"
    if 0x801E0000 <= v < 0x801F3000:
        return f"GENERAL:{address}"
    return address


def main():
    mapping = sys.argv[1]
    promote = "--promote" in sys.argv
    path = os.path.join(mapping, "function_map.json")
    fmap = json.load(open(path))
    psx = {psx_key(f["address"]): f for f in json.load(open(os.path.join(mapping, "psx_functions.json")))}
    pc = {q["name"]: q for q in json.load(open(os.path.join(mapping, "pc_functions.json")))}
    pc_callers = defaultdict(set)
    for q in pc.values():
        for c in q["calls"]:
            pc_callers[c].add(q["name"])

    def trusted(k):
        v = fmap.get(k)
        return v and (v["evidence"] == "manual" or v["confidence"] == "high")

    promoted, review = [], []
    for k, v in fmap.items():
        if v["confidence"] != "low" or k not in psx or v["name"] not in pc:
            continue
        f, q = psx[k], pc[v["name"]]
        agree, disagree = [], []
        for a, _ in f["calls"]:
            ck = psx_key(a)
            if trusted(ck):
                (agree if fmap[ck]["name"] in q["calls"] else disagree).append("calls " + fmap[ck]["name"])
        for a in f["callers"]:
            ck = psx_key(a)
            if trusted(ck):
                (agree if fmap[ck]["name"] in pc_callers[v["name"]] else disagree).append("called by " + fmap[ck]["name"])
        if len(agree) >= MIN_AGREE and not disagree:
            promoted.append(k)
            if promote:
                v["confidence"] = "high"
                v["evidence"] = "graph-checked"
                v["notes"] = (v["notes"] + "; " if v["notes"] else "") + ", ".join(agree[:4])
        else:
            review.append((k, v["name"], v["evidence"], agree, disagree))
    if promote:
        with open(path, "w") as fh:
            json.dump(fmap, fh, indent=1)
    print(f"{len(promoted)} {'promoted' if promote else 'would be promoted'}, {len(review)} need review")
    for k, name, ev, agree, disagree in review:
        print(f"  {k:22} {name:34} {ev:9} +{len(agree)} -{len(disagree)} {'; '.join(disagree[:3])}")


if __name__ == "__main__":
    main()
