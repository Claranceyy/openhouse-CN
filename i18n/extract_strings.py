#!/usr/bin/env python3
"""Scan openhouse GUI C++ sources to extract translatable strings and audit i18n/zh_CN.json.

Usage:
  python i18n/extract_strings.py                 # Audit coverage and validate zh_CN.json
  python i18n/extract_strings.py --dump OUT.json # Dump all extracted candidate strings
"""
import argparse
import json
from pathlib import Path
import re
import sys

UI_FILES = [
    "apps/openhouse-installer/ui.cpp",
    "apps/openhouse-installer/driver_install.cpp",
    "apps/openhouse-installer/radio.cpp",
    "apps/openhouse-installer/self_update.cpp",
    "apps/openhouse-installer/system.cpp",
    "apps/openhouse-installer/device_type.cpp",
    "apps/openhouse-installer/drives.cpp",
    "apps/openhouse-overlay/ui.cpp",
    "apps/openhouse-overlay/views.cpp",
    "apps/openhouse-overlay/log_view.cpp",
    "apps/openhouse-overlay/driver_check.cpp",
    "apps/openhouse-overlay/base_channels.cpp",
    "apps/openhouse-overlay/update_check.cpp",
    "apps/doorman-dongle-manager/ui.cpp",
    "apps/doorman-dongle-manager/dongle.cpp",
    "apps/openhouse-viewer/main.cpp",
    "apps/openhouse-viewer/views.cpp",
    "apps/openhouse-viewer/log_view.cpp",
    "apps/lhviz/main.cpp",
]

PRINTF_SPEC_RE = re.compile(
    r"%(%|[-+ #0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn])"
)


def printf_signature(fmt: str):
    sig = []
    for m in PRINTF_SPEC_RE.finditer(fmt):
        tok = m.group(1)
        if tok == "%":
            continue
        spec = tok[-1]
        if spec == "n":
            return None
        rest = tok[:-1]
        if "*" in rest:
            # Count width/precision stars explicitly
            dot = rest.find(".")
            wpart = rest if dot == -1 else rest[:dot]
            ppart = "" if dot == -1 else rest[dot + 1 :]
            if "*" in wpart:
                sig.append("W")
            if "*" in ppart:
                sig.append("P")
        len_mod = " "
        for lm in ("hh", "ll", "h", "l", "j", "z", "t", "L"):
            if rest.endswith(lm):
                len_mod = "H" if lm == "hh" else ("L" if lm == "ll" else lm)
                break
        if spec in ("i", "d"):
            kind = "d"
        elif spec in ("u", "o", "x", "X"):
            kind = "u"
        elif spec in ("f", "F", "e", "E", "g", "G", "a", "A"):
            kind = "f"
        else:
            kind = spec
        sig.append(f"{len_mod}{kind}")
    return ";".join(sig)


def strip_comments(src: str) -> str:
    out = []
    i, n = 0, len(src)
    in_str = False
    while i < n:
        if in_str:
            out.append(src[i])
            if src[i] == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if src[i] == '"':
                in_str = False
            i += 1
            continue
        if src[i] == '"':
            in_str = True
            out.append(src[i])
            i += 1
        elif src[i : i + 2] == "//":
            while i < n and src[i] != "\n":
                i += 1
        elif src[i : i + 2] == "/*":
            i += 2
            while i + 1 < n and src[i : i + 2] != "*/":
                if src[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def unescape_cpp_string(raw: str) -> str:
    out = []
    i, n = 0, len(raw)
    while i < n:
        if raw[i] == "\\" and i + 1 < n:
            c = raw[i + 1]
            if c == "n":
                out.append("\n")
            elif c == "t":
                out.append("\t")
            elif c == "r":
                out.append("\r")
            elif c == '"':
                out.append('"')
            elif c == "\\":
                out.append("\\")
            elif c == "x":
                j = i + 2
                while j < n and j < i + 4 and raw[j] in "0123456789abcdefABCDEF":
                    j += 1
                out.append(chr(int(raw[i + 2 : j], 16)))
                i = j
                continue
            else:
                out.append(c)
            i += 2
        else:
            out.append(raw[i])
            i += 1
    return "".join(out)


def extract_literals(code: str):
    """Extract adjacent-concatenated C++ string literals with line numbers."""
    code = strip_comments(code)
    results = []
    i, n = 0, len(code)
    line = 1
    while i < n:
        if code[i] == "\n":
            line += 1
            i += 1
            continue
        if code[i] == '"':
            # Skip #include "..."
            line_start = code.rfind("\n", 0, i) + 1
            prefix = code[line_start:i].lstrip()
            if prefix.startswith("#include"):
                i += 1
                while i < n and code[i] != "\n":
                    i += 1
                continue
            start_line = line
            parts = []
            while i < n and code[i] == '"':
                i += 1
                buf = []
                while i < n and code[i] != '"':
                    if code[i] == "\\" and i + 1 < n:
                        buf.append(code[i : i + 2])
                        if code[i + 1] == "\n":
                            line += 1
                        i += 2
                        continue
                    if code[i] == "\n":
                        line += 1
                    buf.append(code[i])
                    i += 1
                if i < n and code[i] == '"':
                    i += 1
                parts.append(unescape_cpp_string("".join(buf)))
                # Check for adjacent concatenated string literal across whitespace
                j = i
                extra_lines = 0
                while j < n and code[j] in " \t\r\n":
                    if code[j] == "\n":
                        extra_lines += 1
                    j += 1
                if j < n and code[j] == '"':
                    line += extra_lines
                    i = j
                else:
                    break
            full = "".join(parts)
            # Strip trailing ImGui ##id for visible display text
            vis = full.split("##", 1)[0] if "##" in full else full
            if vis and any(c.isalpha() for c in vis):
                results.append((start_line, vis, full))
            continue
        i += 1
    return results


def validate_dict(data: dict) -> list[str]:
    errors = []
    for key in ("exact", "format", "patterns"):
        if key not in data:
            errors.append(f"Missing top-level key: {key}")
    for src, dst in data.get("format", {}).items():
        s1 = printf_signature(src)
        s2 = printf_signature(dst)
        if s1 is None or s2 is None or s1 != s2:
            errors.append(f"Printf signature mismatch: {src!r} ({s1}) -> {dst!r} ({s2})")
    for idx, pat in enumerate(data.get("patterns", [])):
        if not isinstance(pat, dict) or "match" not in pat or "replace" not in pat:
            errors.append(f"Invalid pattern entry #{idx}")
            continue
        if not re.search(r"\{@?[1-9]\}", pat["match"]):
            errors.append(f"Pattern #{idx} has no slot placeholder: {pat['match']!r}")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", type=Path, help="Dump extracted literals by file to JSON")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parent.parent
    dict_path = repo / "i18n/zh_CN.json"
    data = json.loads(dict_path.read_text(encoding="utf-8"))

    errors = validate_dict(data)
    if errors:
        print("DICTIONARY VALIDATION ERRORS:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    by_file = {}
    unique_exact = {}
    for rel in UI_FILES:
        p = repo / rel
        if not p.is_file():
            continue
        lits = extract_literals(p.read_text(encoding="utf-8"))
        by_file[rel] = [{"line": ln, "vis": v, "raw": r} for ln, v, r in lits]
        for ln, v, _ in lits:
            unique_exact.setdefault(v, []).append(f"{rel}:{ln}")

    if args.dump:
        args.dump.write_text(json.dumps(by_file, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"Dumped {len(unique_exact)} unique strings across {len(by_file)} files to {args.dump}")
        return 0

    exact = data.get("exact", {})
    fmt = data.get("format", {})
    covered = sum(1 for s in unique_exact if s in exact or s in fmt)
    print(f"zh_CN.json valid: exact={len(exact)}, format={len(fmt)}, patterns={len(data.get('patterns', []))}")
    print(f"Scanned {len(by_file)} files, {len(unique_exact)} unique candidate strings ({covered} exact/format matched)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
