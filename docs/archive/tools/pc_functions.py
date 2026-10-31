"""List the functions of the PC decomp (reference) as JSON.

Usage: python pc_functions.py <pc source-code/src> <out.json>

Each function definition preceded by an `// orig: 0x...` comment gives one record:
name, PC address, file, code line count, the string literals and the identifiers it calls.
"""
import json
import os
import re
import sys

ORIG = re.compile(r"^//\s*orig:\s*(0x[0-9a-fA-F]+)")
DEF = re.compile(r"^[A-Za-z_][\w\s\*&:<>,]*?\b([A-Za-z_]\w*)\s*\(([^;]*)$")
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "reinterpret_cast",
            "const_cast", "case", "do", "else"}
NUMBER = re.compile(r"(?<![\w.])(-?)(0[xX][0-9a-fA-F]+|\d+)[uUlL]*(?![\w.])")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
NAMED = [
    re.compile(r"constexpr\s+[\w:\s]+?\b([A-Za-z_]\w*)\s*(?:=|\{)\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))"),
    re.compile(r"#define\s+([A-Za-z_]\w*)\s+\(?(-?(?:0[xX][0-9a-fA-F]+|\d+))"),
    re.compile(r"\b([A-Za-z_]\w*)\s*=\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*[,}]"),  # enumerators
]


def named_constants(sources):
    """Map constant names defined anywhere in the PC source to their values."""
    values = {}
    for text in sources:
        for pattern in NAMED:
            for name, value in pattern.findall(COMMENT.sub("", text)):
                values.setdefault(name, int(value, 0))
    return values


def constants(body, named):
    """Numeric values used in a function body, with named constants resolved; small values are skipped."""
    code = STRING.sub('""', COMMENT.sub("", body))
    found = {int(sign + digits, 0) for sign, digits in NUMBER.findall(code)}
    found |= {named[w] for w in re.findall(r"\b[A-Za-z_]\w*\b", code) if w in named}
    return sorted(v for v in found if abs(v) > 16)


def function_body(lines, start):
    """Return the lines from the definition at `start` through its closing brace."""
    depth, seen = 0, False
    for i in range(start, len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        seen = seen or "{" in lines[i]
        if seen and depth <= 0:
            return lines[start:i + 1]
    return lines[start:]


def helper_calls(texts):
    """Calls made by every function definition, tagged or not, keyed by name.

    The PC decomp moved parts of original functions into helpers without an
    `// orig:` tag; following calls through those helpers gives the original
    function's callees."""
    calls = {}
    for text in texts:
        lines = text.splitlines()
        for j, line in enumerate(lines):
            if line[:1].isspace() or line.startswith(("//", "#")):
                continue
            d = DEF.match(line.strip())
            if not d or d.group(1) in KEYWORDS:
                continue
            nxt = line + (lines[j + 1] if j + 1 < len(lines) else "")
            if "{" not in nxt or ";" in line:
                continue
            body = "\n".join(function_body(lines, j))
            calls.setdefault(d.group(1), set()).update(c for c in CALL.findall(body) if c not in KEYWORDS)
    return calls


def expand_calls(direct, tagged, helpers):
    """Follow calls into untagged helpers, transitively; tagged functions stay leaves."""
    out, todo, seen = set(), list(direct), set()
    while todo:
        c = todo.pop()
        if c in seen:
            continue
        seen.add(c)
        out.add(c)
        if c not in tagged and c in helpers:
            todo.extend(helpers[c])
    return out


def file_functions(rel, lines, named):
    """Records for every `// orig:`-tagged function definition in one source file."""
    records = []
    for i, line in enumerate(lines):
        m = ORIG.match(line.strip())
        if not m:
            continue
        j = i + 1
        while j < len(lines) and (not lines[j].strip() or lines[j].strip().startswith("//")):
            j += 1
        d = DEF.match(lines[j].strip()) if j < len(lines) else None
        if not d:
            continue
        body_lines = function_body(lines, j)
        body = "\n".join(body_lines)
        records.append({
            "pc_address": int(m.group(1), 16),
            "name": d.group(1),
            "file": rel,
            "line": j + 1,
            "lines": sum(1 for b in body_lines if b.strip() and not b.strip().startswith("//")),
            "strings": sorted(set(STRING.findall(body))),
            "constants": constants(body, named),
            "calls": sorted({c for c in CALL.findall(body) if c not in KEYWORDS} - {d.group(1)}),
        })
    return records


def main():
    root, out = sys.argv[1], sys.argv[2]
    texts = {}
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if fn.endswith((".cpp", ".h")):
                path = os.path.join(dirpath, fn)
                with open(path, encoding="utf-8", errors="replace") as f:
                    texts[path] = f.read()
    named = named_constants(texts.values())
    records = []
    for path, text in sorted(texts.items()):
        if path.endswith(".cpp"):
            rel = os.path.relpath(path, root).replace("\\", "/")
            records += file_functions(rel, text.splitlines(), named)
    helpers = helper_calls(texts.values())
    tagged = {r["name"] for r in records}
    for r in records:
        r["calls"] = sorted(expand_calls(r["calls"], tagged, helpers) - {r["name"]})
    records.sort(key=lambda r: r["pc_address"])
    with open(out, "w") as f:
        json.dump(records, f, indent=1)
    print(f"{len(records)} functions")


if __name__ == "__main__":
    main()
