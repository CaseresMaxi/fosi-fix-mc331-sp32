Import("env")  # type: ignore  # noqa: F821

from pathlib import Path


def esc(data: bytes) -> str:
    out = []
    for b in data:
        if b == ord("\\"):
            out.append("\\\\")
        elif b == ord('"'):
            out.append('\\"')
        elif b == ord("\n"):
            out.append("\\n")
        elif b == ord("\r"):
            out.append("\\r")
        elif b == ord("\t"):
            out.append("\\t")
        elif 32 <= b <= 126:
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02x}")
    return "".join(out)


def build_assets():
    root = Path(env["PROJECT_DIR"])  # type: ignore  # noqa: F821
    data = root / "data"
    out = root / "src" / "web" / "UiAssets.h"
    files = {
        "INDEX_HTML": data / "index.html",
        "STYLE_CSS": data / "style.css",
        "APP_JS": data / "app.js",
    }
    lines = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        '#define FOSIFIX_UI_BUILD "1.0.0-stable"',
        "",
    ]
    for name, path in files.items():
        raw = path.read_bytes()
        lines.append(f"static const char {name}[] PROGMEM = \"{esc(raw)}\";")
        lines.append("")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {out} ({out.stat().st_size} bytes)")


build_assets()
