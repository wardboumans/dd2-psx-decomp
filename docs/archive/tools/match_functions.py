"""Match PSX game functions to PC decomp functions.

Usage: python match_functions.py <mapping dir>

Reads pc_functions.json and psx_functions.json, writes match_candidates.json:
one record per PSX game function with the best PC match, its score and the
evidence. A human (or Claude) reviews these before they go into function_map.json.

Both exes link their modules in the same order, so within each PSX code block
(exe, FRONTEND, GENERAL, RESULTS) matches must keep the PC address order.
Seeds come from shared strings; an order-preserving alignment between seeds
pairs the rest using strings, SDK calls, size and already-matched callers/callees.
"""
import json
import math
import os
import sys
import warnings
from collections import Counter, defaultdict

GAME_TEXT_END = 0x8005E5EC  # Psy-Q libraries follow the game code in .text
ACCEPT = 3.0                # score needed for a pair to become a seed
REPORT = 0.5                # score needed to report a pair at all


def norm_string(s):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        return s.encode("latin-1", "backslashreplace").decode("unicode_escape").strip()


def block_of(address):
    """Game code block of a PSX address, or None for SDK and non-code addresses."""
    if address.startswith("ov1::"):
        return "RESULTS"
    v = int(address, 16)
    if 0x800116EC <= v < GAME_TEXT_END:
        return "EXE"
    if 0x801C4000 <= v < 0x801E0000:
        return "FRONTEND"
    if 0x801E0000 <= v < 0x801F3000:
        return "GENERAL"
    return None


def load(mapping):
    pc = json.load(open(os.path.join(mapping, "pc_functions.json")))
    px = json.load(open(os.path.join(mapping, "psx_functions.json")))
    for q in pc:
        q["strset"] = {norm_string(s) for s in q["strings"]} - {""}
        q["callset"] = set(q["calls"])
        q["constset"] = set(q["constants"])
    blocks = defaultdict(list)
    for f in px:
        b = block_of(f["address"])
        if b is None:
            continue
        f["blk"] = b
        f["strset"] = {norm_string(s) for s in f["strings"]} - {""}
        f["constset"] = set(f.get("constants", []))
        f["sdk"] = {n for a, n in f["calls"] if block_of(a) is None}
        f["game_callees"] = {a for a, n in f["calls"] if block_of(a) is not None}
        blocks[b].append(f)
    for b in blocks:
        blocks[b].sort(key=lambda f: int(f["address"].split("::")[-1], 16))
    return pc, blocks


class Scorer:
    def __init__(self, pc, psx_all):
        self.pc_by_name = {q["name"]: q for q in pc}
        self.psx = {f["address"]: f for f in psx_all}
        self.map = {}  # psx address -> pc name
        self.ratio = 3.0
        # PC callers per name, for the caller term
        self.pc_callers = defaultdict(set)
        for q in pc:
            for c in q["callset"]:
                self.pc_callers[c].add(q["name"])
        # Rare constants are strong evidence, common ones (32, 4096) weak.
        df = Counter(c for q in pc for c in q["constset"])
        self.idf = {c: math.log(len(pc) / n) for c, n in df.items()}

    def score(self, f, q):
        s = -1.0
        why = []
        if f["strset"] or q["strset"]:
            inter = len(f["strset"] & q["strset"])
            union = len(f["strset"] | q["strset"])
            if inter:
                s += 8.0 * inter / union + 1.0 * min(inter, 4)
                why.append(f"strings {inter}/{union}")
            elif f["strset"] and q["strset"]:
                s -= 3.0
        shared_const = f["constset"] & q["constset"]
        if shared_const:
            s += min(0.5 * sum(self.idf[c] for c in shared_const), 8.0)
            why.append("const " + ",".join(str(c) for c in sorted(shared_const)[:6]))
        elif len(q["constset"]) >= 3 and len(f["constset"]) >= 3:
            s -= 1.5
        shared_sdk = f["sdk"] & q["callset"]
        if shared_sdk:
            s += min(1.5 * len(shared_sdk), 6.0)
            why.append("sdk " + ",".join(sorted(shared_sdk)))
        s -= 0.75 * len(f["sdk"] - q["callset"] - {"__main"}) ** 0.5
        lr = math.log((f["instructions"] + 4) / (self.ratio * (q["lines"] + 1)))
        s -= 1.2 * abs(lr)
        good = bad = 0
        for c in f["game_callees"]:
            m = self.map.get(c)
            if m is None:
                continue
            if m in q["callset"]:
                good += 1
            else:
                bad += 1
        for c in f["callers"]:
            m = self.map.get(c)
            if m is None:
                continue
            if q["name"] in self.pc_by_name[m]["callset"]:
                good += 1
            else:
                bad += 0.5
        if good:
            why.append(f"graph +{good}")
        s += 3.0 * good - 1.0 * bad
        return s, why


def lis_filter(pairs):
    """Keep the longest subsequence of (psx index, pc index) pairs increasing in both."""
    pairs = sorted(pairs)
    best = []
    prev = [-1] * len(pairs)
    length = [1] * len(pairs)
    for i in range(len(pairs)):
        for j in range(i):
            if pairs[j][1] < pairs[i][1] and pairs[j][0] < pairs[i][0] and length[j] + 1 > length[i]:
                length[i], prev[i] = length[j] + 1, j
    if not pairs:
        return []
    i = max(range(len(pairs)), key=lambda k: length[k])
    while i != -1:
        best.append(pairs[i])
        i = prev[i]
    return best[::-1]


def align(seq, pc, scorer, anchors):
    """Order-preserving alignment of seq (PSX) to pc between anchor pairs."""
    result = {}
    bounds = [(-1, -1)] + anchors + [(len(seq), len(pc))]
    for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
        if 0 <= i0 < len(seq):
            result[i0] = j0
        A = list(range(i0 + 1, i1))
        B = list(range(j0 + 1, j1))
        if not A or not B:
            continue
        n, m = len(A), len(B)
        S = [[scorer.score(seq[A[x]], pc[B[y]])[0] for y in range(m)] for x in range(n)]
        dp = [[0.0] * (m + 1) for _ in range(n + 1)]
        for x in range(n - 1, -1, -1):
            for y in range(m - 1, -1, -1):
                dp[x][y] = max(dp[x + 1][y], dp[x][y + 1], S[x][y] + dp[x + 1][y + 1])
        x = y = 0
        while x < n and y < m:
            if S[x][y] > 0 and dp[x][y] == S[x][y] + dp[x + 1][y + 1]:
                result[A[x]] = B[y]
                x += 1
                y += 1
            elif dp[x][y] == dp[x + 1][y]:
                x += 1
            else:
                y += 1
    return result


def fill_gaps(recs, scorer, pc):
    """Between two matches, pair unmatched PSX and skipped PC functions in order
    when both sides have the same count: link order makes that the likely pairing."""
    matched = [k for k, r in enumerate(recs) if "pc_index" in r]
    for k0, k1 in zip(matched, matched[1:]):
        psx_gap = list(range(k0 + 1, k1))
        pc_gap = list(range(recs[k0]["pc_index"] + 1, recs[k1]["pc_index"]))
        if not psx_gap or len(psx_gap) != len(pc_gap):
            continue
        for k, j in zip(psx_gap, pc_gap):
            s, why = scorer.score(scorer.psx[recs[k]["psx"]], pc[j])
            recs[k].update(pc_index=j, score=round(s, 2), seed=False, why=["order"] + why)


def main():
    mapping = sys.argv[1]
    pc, blocks = load(mapping)
    psx_all = [f for b in blocks.values() for f in b]
    scorer = Scorer(pc, psx_all)
    pc_index = {q["name"]: i for i, q in enumerate(pc)}

    # Seeds: functions whose strings point at exactly one PC function.
    by_string = defaultdict(set)
    for q in pc:
        for s in q["strset"]:
            by_string[s].add(q["name"])
    seeds = {}
    for f in psx_all:
        cand = Counter()
        for s in f["strset"]:
            for n in by_string.get(s, ()):
                cand[n] += 1
        if cand:
            top = cand.most_common(2)
            if len(top) == 1 or top[0][1] > top[1][1]:
                seeds[f["address"]] = top[0][0]

    # Calibrate MIPS instructions per PC source line on the seeds.
    ratios = sorted(scorer.psx[a]["instructions"] / max(scorer.pc_by_name[n]["lines"], 1) for a, n in seeds.items())
    scorer.ratio = ratios[len(ratios) // 2]
    print(f"{len(seeds)} string seeds, {scorer.ratio:.2f} MIPS instructions per PC line")

    accepted = dict(seeds)
    for rnd in range(6):
        scorer.map = dict(accepted)
        found = {}
        for name, seq in blocks.items():
            pairs = [(i, pc_index[accepted[f["address"]]]) for i, f in enumerate(seq) if f["address"] in accepted]
            anchors = lis_filter(pairs)
            for i, j in align(seq, pc, scorer, anchors).items():
                found[seq[i]["address"]] = j
        new = {}
        for a, j in found.items():
            s, _ = scorer.score(scorer.psx[a], pc[j])
            if a in seeds or s >= ACCEPT:
                new[a] = pc[j]["name"]
        print(f"round {rnd}: {len(new)} accepted, {len(found)} aligned")
        if new == accepted:
            break
        accepted = new

    # Second pass: the PC exe links its system modules (file, sound, sprite, font,
    # heap) first and the PSX exe last, so align the leftovers of both sides on their own.
    # Its call-graph evidence can feed on its own mistakes, so these stay low confidence.
    leftover = set()
    for rnd in range(4):
        scorer.map = dict(accepted)
        used = set(accepted.values())
        rest_pc = [j for j, q in enumerate(pc) if q["name"] not in used]
        added = 0
        for name, seq in blocks.items():
            rest = [f for f in seq if f["address"] not in accepted]
            sub_pc = [pc[j] for j in rest_pc]
            for i, j in align(rest, sub_pc, scorer, []).items():
                a = rest[i]["address"]
                q = sub_pc[j]
                if q["name"] in used:
                    continue
                if scorer.score(rest[i], q)[0] >= ACCEPT:
                    accepted[a] = q["name"]
                    found[a] = rest_pc[j]
                    used.add(q["name"])
                    leftover.add(a)
                    added += 1
        print(f"leftover round {rnd}: {added} added")
        if not added:
            break

    scorer.map = dict(accepted)
    out = []
    for name, seq in blocks.items():
        recs = []
        for f in seq:
            a = f["address"]
            j = found.get(a)
            rec = {"psx": a, "block": name, "instructions": f["instructions"]}
            if j is not None:
                s, why = scorer.score(f, pc[j])
                if a in leftover:
                    rec.update(pc_index=j, score=round(s, 2), seed=False, why=["leftover"] + why)
                elif s >= REPORT or a in seeds:
                    rec.update(pc_index=j, score=round(s, 2), seed=a in seeds, why=why)
            recs.append(rec)
        fill_gaps(recs, scorer, pc)
        for rec in recs:
            j = rec.pop("pc_index", None)
            if j is not None:
                rec.update(pc=pc[j]["name"], pc_address=hex(pc[j]["pc_address"]), file=pc[j]["file"])
        out += recs
    with open(os.path.join(mapping, "match_candidates.json"), "w") as fh:
        json.dump(out, fh, indent=1)
    matched = [r for r in out if "pc" in r]
    conf = Counter("seed" if r["seed"] else r["why"][0] if r["why"][:1] in (["order"], ["leftover"])
                   else "high" if r["score"] >= ACCEPT else "low" for r in matched)
    print(f"{len(matched)}/{len(out)} matched: {dict(conf)}")
    for b in blocks:
        rs = [r for r in out if r["block"] == b]
        print(f"  {b:9} {sum('pc' in r for r in rs):4}/{len(rs)}")


if __name__ == "__main__":
    main()
