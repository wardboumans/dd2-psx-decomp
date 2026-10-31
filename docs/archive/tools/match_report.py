"""Show the unmatched stretches of match_candidates.json for manual review.

Usage: python match_report.py <mapping dir> [block]

For every run of unmatched PSX functions, prints the matched neighbours and the
PC functions that the alignment skipped at that spot (with their line counts).
"""
import json
import os
import sys


def main():
    mapping = sys.argv[1]
    only = sys.argv[2] if len(sys.argv) > 2 else None
    recs = json.load(open(os.path.join(mapping, "match_candidates.json")))
    pc = json.load(open(os.path.join(mapping, "pc_functions.json")))
    index = {q["name"]: i for i, q in enumerate(pc)}
    blocks = []
    for r in recs:
        if (only is None or r["block"] == only) and r["block"] not in blocks:
            blocks.append(r["block"])
    for b in blocks:
        seq = [r for r in recs if r["block"] == b]
        print(f"== {b}: {sum('pc' in r for r in seq)}/{len(seq)} matched")
        k = 0
        while k < len(seq):
            if "pc" in seq[k]:
                k += 1
                continue
            start = k
            while k < len(seq) and "pc" not in seq[k]:
                k += 1
            before = seq[start - 1] if start > 0 else None
            after = seq[k] if k < len(seq) else None
            lo = index[before["pc"]] + 1 if before else 0
            hi = index[after["pc"]] if after else len(pc)
            run = " ".join(f'{r["psx"]}({r["instructions"]})' for r in seq[start:k])
            skipped = " ".join(f'{pc[j]["name"]}({pc[j]["lines"]})' for j in range(lo, hi))
            print(f'  after {before["pc"] if before else "-"} / before {after["pc"] if after else "-"}')
            print(f"    PSX: {run}")
            print(f"    PC : {skipped[:400] or '-'}")


if __name__ == "__main__":
    main()
