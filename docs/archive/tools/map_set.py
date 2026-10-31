"""Record hand-checked mappings in function_map.json.

Usage: python map_set.py <mapping dir> <batch.json>

batch.json: {"<key>": {"name": ..., "file": "src/...c", "notes": ...}, ...}
Keys are function_map keys (exe address, or FRONTEND:/GENERAL:/RESULTS: prefix).
Entries get evidence "manual" and confidence "high"; the PC address is filled in
when the name exists in the PC decomp, otherwise it is null (PSX-only function).
"""
import json
import os
import sys


def main():
    mapping, batch_path = sys.argv[1], sys.argv[2]
    path = os.path.join(mapping, "function_map.json")
    fmap = json.load(open(path))
    pc_by_name = {q["name"]: q for q in json.load(open(os.path.join(mapping, "pc_functions.json")))}
    batch = json.load(open(batch_path))
    for key, entry in batch.items():
        q = pc_by_name.get(entry["name"])
        old = fmap.get(key, {})
        fmap[key] = {
            "name": entry["name"],
            "pc": hex(q["pc_address"]) if q else None,
            "file": entry["file"],
            "status": old.get("status") if old.get("status") not in (None, "mapped") else "mapped",
            "evidence": "manual",
            "confidence": "high",
            "score": None,
            "notes": entry.get("notes", ""),
        }
    ordered = dict(sorted(fmap.items(), key=lambda kv: (":" in kv[0], kv[0].split(":")[0], int(kv[0].split(":")[-1], 16))))
    with open(path, "w") as f:
        json.dump(ordered, f, indent=1)
    print(f"{len(batch)} entries set; {sum(v['evidence'] == 'manual' for v in ordered.values())} manual in total")


if __name__ == "__main__":
    main()
