#!/usr/bin/env python3
"""Generate a C++ byte-array header from i18n/zh_CN.json for compile-time fallback."""
import json
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen_builtin.py INPUT_JSON OUTPUT_HEADER\n")
        return 2
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    raw = src.read_bytes()
    # Validate UTF-8 and JSON syntax, then store compact UTF-8 bytes to keep binaries lean.
    parsed = json.loads(raw.decode("utf-8"))
    compact = json.dumps(parsed, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

    lines = [
        "// Auto-generated from i18n/zh_CN.json by i18n/gen_builtin.py. Do not edit.",
        "#pragma once",
        "#include <cstddef>",
        "",
        "namespace lh::i18n {",
        f"inline constexpr std::size_t kBuiltinZhCnJsonSize = {len(compact)}u;",
        "inline constexpr unsigned char kBuiltinZhCnJson[] = {",
    ]
    step = 20
    for i in range(0, len(compact), step):
        chunk = ", ".join(f"0x{b:02x}" for b in compact[i : i + step])
        lines.append(f"  {chunk},")
    lines.append("  0x00")
    lines.append("};")
    lines.append("}  // namespace lh::i18n")
    lines.append("")
    content = "\n".join(lines)

    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_file() and dst.read_text(encoding="utf-8") == content:
        return 0
    dst.write_text(content, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
