#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Flatten an Arduino project (headers + sources) into a single .ino for Wokwi.

Features
- Robust header-guard removal (#pragma once, #ifndef GUARD ... #endif) + stray #endif cleanup
- Topological include order for local headers (best-effort)
- Local header #include "..." is inlined (commented out in sources/headers after inlining)
- External #include <...> are deduped and emitted once at the very top (Arduino.h first)
- ENTRY file (either .ino or .cpp) is emitted once at the bottom (prevents global redefinition)
- NEW: Inserts a forward-declarations block (struct/class Foo;) after includes to satisfy
       Arduino/Wokwi's auto-prototyper hoisting of function prototypes.

Usage
  python3 flatten_arduino.py --root . --out wokwi.ino --entry src/main.cpp
  # or
  python3 flatten_arduino.py --root . --out wokwi.ino --entry src/MySketch.ino

Notes
- C sources (.c) are intentionally excluded to avoid C/C++ mixing issues after flattening.
- This script writes even if some lists are empty; check stdout summary for counts.
"""

import argparse, re, sys
from pathlib import Path
from collections import defaultdict, deque

EXCLUDE_DIRS = {".git", "build", "cmake-build-debug", ".pio", ".vscode", ".idea", "out", "dist"}
HEADER_EXTS = {".h", ".hpp", ".hh", ".hxx"}
SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".ino"}  # .c intentionally excluded
INCLUDE_RE  = re.compile(r'^\s*#\s*include\s*(<[^>]+>|"[^"]+")', re.M)

# --- type detection (for forward decls) ---
# capture "struct Foo {" or "class Bar {" or with inheritance colon
TYPE_DEF_RE = re.compile(r'^\s*(struct|class)\s+([A-Za-z_]\w*)\s*(?::|\{)', re.M)


def is_excluded_dir(p: Path) -> bool:
    return any(d in set(p.parts) for d in EXCLUDE_DIRS)


def scan_files(root: Path):
    headers, sources = [], []
    for p in root.rglob("*"):
        if p.is_dir():
            if p.name in EXCLUDE_DIRS:
                continue
        elif p.is_file():
            if is_excluded_dir(p.parent):
                continue
            if p.suffix in HEADER_EXTS:
                headers.append(p)
            elif p.suffix in SOURCE_EXTS:
                sources.append(p)
    return headers, sources


def read_text(p: Path) -> str:
    try:
        return p.read_text(encoding="utf-8", errors="ignore")
    except Exception as e:
        print(f"[warn] failed to read {p}: {e}", file=sys.stderr)
        return ""


def parse_includes(text: str):
    local_includes, external_includes = [], []
    for m in INCLUDE_RE.finditer(text):
        token = m.group(1).strip()
        if token.startswith("<"):
            external_includes.append(token[1:-1])
        else:
            local_includes.append(token[1:-1])
    return local_includes, external_includes


def build_header_maps(headers):
    by_rel_or_full, by_name = {}, {}
    for p in headers:
        name = p.name
        by_name.setdefault(name, p)
        rel = p.as_posix()
        by_rel_or_full[rel] = p
        by_rel_or_full[name] = p
    return by_rel_or_full, by_name


def resolve_local_include(inc: str, base_dir: Path, hdr_map: dict):
    cand = (base_dir / inc).resolve()
    if cand.exists():
        return cand
    norm = inc.replace("\\", "/")
    if norm in hdr_map:
        return hdr_map[norm]
    return hdr_map.get(Path(inc).name)

# ----- header guard handling -----
PRAGMA_ONCE_RE = re.compile(r'^\s*#\s*pragma\s+once\s*$', re.M)
IFNDEF_RE      = re.compile(r'^\s*#\s*ifndef\s+([A-Za-z_]\w*)\s*$', re.M)
DEFINE_RE_TPL  = r'^\s*#\s*define\s+{guard}\b.*$'
ENDIF_RE       = re.compile(r'^\s*#\s*endif\b.*$', re.M)
IF_ANY_RE      = re.compile(r'^\s*#\s*if(n?def)?\b', re.M)


def strip_header_guards_full(text: str) -> str:
    # Remove BOM and pragma once
    if text.startswith("\ufeff"):
        text = text.lstrip("\ufeff")
    text = PRAGMA_ONCE_RE.sub("", text)

    lines = text.splitlines()
    # find first non-empty/comment line
    i = 0
    def ign(s: str) -> bool:
        st = s.strip()
        return (not st) or st.startswith("//") or st.startswith("/*") or st.startswith("*")
    while i < len(lines) and ign(lines[i]):
        i += 1

    if i < len(lines):
        m_ifndef = IFNDEF_RE.match(lines[i])
        if m_ifndef:
            guard = m_ifndef.group(1)
            start_if = i
            # look for #define GUARD shortly after
            max_probe = min(len(lines), i + 12)
            define_line = None
            define_re = re.compile(DEFINE_RE_TPL.format(guard=re.escape(guard)))
            for j in range(i + 1, max_probe):
                if define_re.match(lines[j] or ""):
                    define_line = j
                    break
            if define_line is not None:
                # find last #endif
                end_idx = None
                for k in range(len(lines) - 1, define_line, -1):
                    if ENDIF_RE.match(lines[k] or ""):
                        end_idx = k
                        break
                if end_idx is not None:
                    lines[start_if] = ""
                    lines[define_line] = ""
                    lines[end_idx] = ""
                    text = "\n".join(lines)
                else:
                    text = "\n".join(lines)
            else:
                text = "\n".join(lines)
        else:
            text = "\n".join(lines)
    else:
        text = "\n".join(lines)

    # Safety: drop extra trailing #endif if more endif than if/ifdef/ifndef
    if_count    = len(IF_ANY_RE.findall(text))
    endif_count = len(ENDIF_RE.findall(text))
    if endif_count > if_count:
        diff = endif_count - if_count
        L = text.splitlines()
        for idx in range(len(L) - 1, -1, -1):
            if diff == 0:
                break
            if ENDIF_RE.match(L[idx] or ""):
                L[idx] = "// [removed stray #endif]"
                diff -= 1
        text = "\n".join(L)

    return text

# ----- header graph & order -----

def graph_headers(headers):
    hdr_lookup_full, _ = build_header_maps(headers)
    deps, nodes = defaultdict(set), set(headers)
    for h in headers:
        text = read_text(h)
        locals_, _ = parse_includes(text)
        for inc in locals_:
            t = resolve_local_include(inc, h.parent, hdr_lookup_full)
            if t and t in nodes:
                deps[h].add(t)
    return nodes, deps


# Topological sort (Kahn)

def topo_sort(nodes, deps):
    """nodes: iterable[Path]; deps: dict[node] -> set(dependency)"""
    indeg = {u: 0 for u in nodes}
    rev = defaultdict(set)  # dep -> {users}
    for u in nodes:
        for v in deps.get(u, ()):  # u depends on v
            indeg[u] += 1
            rev[v].add(u)
    q = deque([u for u in nodes if indeg[u] == 0])
    order = []
    while q:
        v = q.popleft()
        order.append(v)
        for u in rev.get(v, ()):  # decrease indegree of users
            indeg[u] -= 1
            if indeg[u] == 0:
                q.append(u)
    if len(order) < len(nodes):  # cycle fallback
        remaining = [u for u in nodes if u not in order]
        order.extend(remaining)
    return order


def dedup(seq):
    seen, out = set(), []
    for x in seq:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out


def strip_local_includes(text: str, base: Path, hdr_map_full: dict):
    def repl(m):
        token = m.group(1)
        if token.startswith('"'):
            inc = token.strip('"')
            if resolve_local_include(inc, base, hdr_map_full):
                return f"// [inlined: {inc}]"
        return m.group(0)
    return re.sub(r'^\s*#\s*include\s*(<[^>]+>|"[^"]+")', repl, text, flags=re.M)


# --- Collect user-defined types for forward decls ---

def collect_user_types(files):
    kinds = {}  # name -> 'struct' or 'class' (first seen wins)
    for p in files:
        txt = read_text(p)
        for m in TYPE_DEF_RE.finditer(txt):
            kind, name = m.group(1), m.group(2)
            if name in {"String"}:  # skip Arduino String
                continue
            kinds.setdefault(name, kind)
    return kinds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=str, default=".", help="project root")
    ap.add_argument("--out", type=str, default="wokwi.ino", help="output single .ino")
    ap.add_argument("--entry", type=str, default=None, help="optional primary .ino/.cpp to place at bottom")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    out_path = Path(args.out).resolve()

    headers, sources = scan_files(root)
    ino_files = [p for p in sources if p.suffix == ".ino"]
    cpp_files = [p for p in sources if p.suffix != ".ino"]

    # Resolve entry (can be .ino or .cpp)
    entry_file = None
    if args.entry:
        entry_file = (root / args.entry).resolve()
        if not entry_file.exists():
            print(f"[warn] --entry not found: {entry_file}", file=sys.stderr)
            entry_file = None

    # Build header order
    nodes, deps = graph_headers(headers)
    hdr_order = topo_sort(list(nodes), deps)
    hdr_map_full, _ = build_header_maps(headers)

    # Collect external includes from ALL project files
    external_pool = []
    for p in headers + sources:
        _, exts = parse_includes(read_text(p))
        external_pool.extend(exts)
    external_pool = dedup(external_pool)
    if "Arduino.h" in external_pool:
        external_pool = ["Arduino.h"] + [x for x in external_pool if x != "Arduino.h"]
    else:
        external_pool = ["Arduino.h"] + external_pool

    # Collect user-defined types (struct/class) for forward decls
    user_types = collect_user_types(headers + sources)
    priority = ["DisplayState", "PomodoroState", "EncoderState"]
    ordered_types = [n for n in priority if n in user_types] + [n for n in sorted(user_types) if n not in priority]

    chunks = []
    chunks.append("// Auto-generated single-file for Wokwi\n")
    for inc in external_pool:
        chunks.append(f"#include <{inc}>\n")
    chunks.append("\n")

    # Forward declarations (to satisfy auto-prototyper)
    if ordered_types:
        chunks.append("// ---- Forward declarations (for Arduino auto-prototyper) ----\n")
        for name in ordered_types:
            kind = user_types[name]
            chunks.append(f"{kind} {name};\n")
        chunks.append("\n")

    # Inline headers (guard-stripped, local includes commented)
    for h in hdr_order:
        text = strip_header_guards_full(read_text(h))
        text = strip_local_includes(text, h.parent, hdr_map_full)
        chunks.append(f"// ===== HEADER: {h.relative_to(root)} =====\n{text.strip()}\n\n")

    # Exclude entry file from intermediate emissions
    if entry_file:
        ino_files = [p for p in ino_files if p.resolve() != entry_file.resolve()]
        cpp_files = [p for p in cpp_files if p.resolve() != entry_file.resolve()]

    # Non-entry .ino helpers
    for p in ino_files:
        t = read_text(p)
        t = strip_local_includes(t, p.parent, hdr_map_full)
        t = re.sub(r'^\s*#\s*include\s*<Arduino\.h>\s*$', '// [dedup Arduino.h]', t, flags=re.M)
        chunks.append(f"// ===== INO: {p.relative_to(root)} =====\n{t.strip()}\n\n")

    # .cpp sources
    for p in cpp_files:
        t = read_text(p)
        t = strip_local_includes(t, p.parent, hdr_map_full)
        t = re.sub(r'^\s*#\s*include\s*<Arduino\.h>\s*$', '// [dedup Arduino.h]', t, flags=re.M)
        chunks.append(f"// ===== SOURCE: {p.relative_to(root)} =====\n{t.strip()}\n\n")

    # Entry file last (if given)
    if entry_file:
        t = read_text(entry_file)
        t = strip_local_includes(t, entry_file.parent, hdr_map_full)
        t = re.sub(r'^\s*#\s*include\s*<Arduino\.h>\s*$', '// [dedup Arduino.h]', t, flags=re.M)
        chunks.append(f"// ===== ENTRY: {entry_file.relative_to(root)} =====\n{t.strip()}\n\n")

    # Final Arduino.h dedup (safety)
    seen = False
    lines = []
    for line in "".join(chunks).splitlines():
        if re.match(r'\s*#\s*include\s*<Arduino\.h>\s*$', line):
            if seen:
                lines.append("// [dedup Arduino.h]")
            else:
                seen = True
                lines.append(line)
        else:
            lines.append(line)
    joined = "\n".join(lines) + "\n"

    # Ensure parent dir exists
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(joined, encoding="utf-8")

    print(
        f"[ok] wrote {out_path} with "
        f"{len(hdr_order)} header(s), {len(ino_files)} ino(s), {len(cpp_files)} cpp(s), "
        f"entry={'yes' if entry_file else 'no'}, forward_decls={len(ordered_types)}."
    )


if __name__ == "__main__":
    main()
