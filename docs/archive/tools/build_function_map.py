"""Merge match_candidates.json into function_map.json.

Usage: python build_function_map.py <mapping dir>

Entries whose evidence is "manual" or "graph-checked", or whose status is past
"mapped", are never touched: those were checked by hand or by check_matches.py. Everything else is (re)written from the
latest candidates, so the matcher can be re-run as Ghidra names improve.
"""
import json
import os
import sys

ACCEPT = 3.0
CHECKED = ("manual", "graph-checked")  # evidence values that the matcher never overwrites


def key(rec):
    """function_map key: exe functions by address, overlay functions prefixed by their file."""
    a = rec["psx"].split("::")[-1]
    return a if rec["block"] == "EXE" else f'{rec["block"]}:{a}'


def evidence(rec):
    why = rec["why"]
    if rec["seed"]:
        return "strings"
    if why[:1] in (["order"], ["leftover"]):
        return why[0]
    kinds = [w.split()[0] for w in why]
    return "+".join(k for k in ("strings", "graph", "const", "sdk") if k in kinds) or "size"


def main():
    mapping = sys.argv[1]
    path = os.path.join(mapping, "function_map.json")
    fmap = json.load(open(path)) if os.path.exists(path) else {}
    cands = json.load(open(os.path.join(mapping, "match_candidates.json")))
    # Functions that no longer exist in Ghidra (deleted as bogus) leave the map.
    current = {key(rec) for rec in cands}
    for k in [k for k in fmap if k not in current]:
        if fmap[k].get("evidence") in CHECKED:
            print(f"warning: hand-checked {k} ({fmap[k]['name']}) is no longer a function in Ghidra")
        else:
            del fmap[k]
    kept = updated = 0
    for rec in cands:
        k = key(rec)
        old = fmap.get(k)
        if old and (old.get("evidence") in CHECKED or old.get("status") != "mapped"):
            kept += 1
            continue
        if "pc" not in rec:
            fmap.pop(k, None)
            continue
        high = rec["seed"] or (rec["score"] >= ACCEPT and rec["why"][:1] not in (["order"], ["leftover"]))
        fmap[k] = {
            "name": rec["pc"],
            "pc": rec["pc_address"],
            "file": "src/" + rec["file"].rsplit(".", 1)[0] + ".c",
            "status": "mapped",
            "evidence": evidence(rec),
            "confidence": "high" if high else "low",
            "score": rec["score"],
            "notes": "",
        }
        updated += 1
    ordered = dict(sorted(fmap.items(), key=lambda kv: (":" in kv[0], kv[0].split(":")[0], int(kv[0].split(":")[-1], 16))))
    with open(path, "w") as f:
        json.dump(ordered, f, indent=1)
    conf = {c: sum(1 for v in ordered.values() if v.get("confidence") == c) for c in ("high", "low")}
    print(f"{len(ordered)} entries ({kept} kept as checked, {updated} from candidates), confidence {conf}")


if __name__ == "__main__":
    main()
