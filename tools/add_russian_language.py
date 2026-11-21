#!/usr/bin/env python3
"""Import the Russian 0.0.0.4 text, fonts and voices into a 0.0.0.2 data folder.

The supported 0.0.0.2 executable maps the game font in ASCII and U+00A1..U+00FF only.
The Russian font files carry the Cyrillic alphabet in 64 extra glyphs.  This tool
copies those glyphs into the Latin-1 slots the executable already knows, and shifts
each Russian character to the Latin-1 codepoint with the same cp1251 byte.  The
game still runs its own 0.0.0.2 XEX; only the data changes.

It writes an overlay folder (text/eng, textures, audio/eng, ui/eng, movies/eng) and,
with --apply, copies that overlay into a copy of the supported data folder.  It never
writes the XEX, and it refuses to run on a data folder whose default.xex is not the
supported one.

Only the Python standard library is used.  See docs/game/russian-language-import.md.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import sys
from pathlib import Path

SUPPORTED_XEX_SHA256 = "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b"
RUSSIAN_XEX_SHA256 = "0598a9638d92ec0070f4fb701323c132427dd76af3cd5a0ebc1d61f211b84041"
FONTS = ("arial", "bleeker", "lt", "ltnob", "robotic")
LATIN1_ENTRY = 225  # U+00C0: entry code + 33
LATIN1_YO = 201     # U+00A8
LATIN1_YO_SMALL = 217  # U+00B8
LATIN1_RANGE = 64


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_xex(path: Path, expected: str, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"{label}: no default.xex at {path}")
    actual = sha256(path)
    if actual != expected:
        raise SystemExit(
            f"{label}: default.xex is {actual}, expected {expected}; "
            "this is not the data folder the import was made for"
        )


def find_dir(parent: Path, *names: str) -> Path | None:
    for name in names:
        candidate = parent / name
        if candidate.is_dir():
            return candidate
    return None


def be32(data: bytes, at: int) -> int:
    return int.from_bytes(data[at:at + 4], "big")


def parse_lxb(path: Path):
    data = path.read_bytes()
    if len(data) < 0x80:
        raise SystemExit(f"{path}: too short for a .lxb file")
    count = be32(data, 0x7C)
    if 0x80 + 8 * count > len(data):
        raise SystemExit(f"{path}: unexpected .lxb layout")
    entries = []
    for i in range(count):
        at = 0x80 + 8 * i
        key = be32(data, at)
        start = at + 4 + be32(data, at + 4)
        if start >= len(data):
            raise SystemExit(f"{path}: string {i} is outside the file")
        end = data.find(b"\0", start)
        if end < 0:
            raise SystemExit(f"{path}: string {i} is not terminated")
        entries.append((key, data[start:end]))
    name_start = be32(data, 4)
    return data, entries, name_start


def shift_text(text: str) -> str:
    """Move Russian codepoints into the Latin-1 cells the 0.0.0.2 mapper knows."""
    out = []
    for ch in text:
        cp = ord(ch)
        if 0x0410 <= cp <= 0x044F:
            out.append(chr(cp - 0x0410 + 0x00C0))
        elif cp == 0x0401:
            out.append("\u00a8")
        elif cp == 0x0451:
            out.append("\u00b8")
        elif cp < 0x80 or 0xA1 <= cp <= 0xFF:
            out.append(ch)
        elif cp in (0x2013, 0x2014):
            out.append("-")
        elif cp == 0x2026:
            out.append("...")
        elif cp == 0x2116:
            out.append("N")
        elif cp == 0x00A0:
            out.append(" ")
        else:
            try:
                raw = ch.encode("cp1251")
            except UnicodeEncodeError:
                out.append("?")
                continue
            out.append(chr(raw[0]) if 0xA1 <= raw[0] <= 0xFF else "?")
    return "".join(out)


def merge_lxb(base_path: Path, russian_path: Path, out_path: Path, shift: bool) -> int:
    base, base_entries, base_name_start = parse_lxb(base_path)
    _, russian_entries, _ = parse_lxb(russian_path)
    russian = dict(russian_entries)
    header = bytearray(base[:0x80])
    table_end = 0x80 + 8 * len(base_entries)
    strings = bytearray()
    pairs = []
    for i, (key, raw) in enumerate(base_entries):
        value = russian.get(key, raw)
        try:
            text = value.decode("utf-8")
        except UnicodeDecodeError:
            text = value.decode("cp1252", "replace")
        if shift:
            text = shift_text(text)
        encoded = text.encode("utf-8")
        start = table_end + len(strings)
        pairs.append((key, start - (0x80 + 8 * i + 4)))
        strings += encoded + b"\0"
    new_name_start = table_end + len(strings)
    if base_name_start:
        struct.pack_into(">I", header, 4, new_name_start)
        tail = base[base_name_start:]
    else:
        tail = b""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(header)
        for key, offset in pairs:
            f.write(struct.pack(">II", key, offset))
        f.write(strings)
        f.write(tail)
    return len(base_entries)


def read_fnt(path: Path):
    data = path.read_bytes()
    entries = []
    at = 0x0C
    while at + 8 <= len(data):
        x, y = struct.unpack_from(">HH", data, at)
        entries.append([x, y, data[at + 4], data[at + 5]])
        at += 8
    return bytearray(data), entries


def font_layout(entries) -> tuple[int, int, int]:
    """Return (start of the 64 Cyrillic glyphs, entry of Ё, entry of ё)."""
    for start in range(330, len(entries) - LATIN1_RANGE + 1):
        if all(entries[start + i][3] for i in range(LATIN1_RANGE)):
            if (start == 0 or not entries[start - 1][3]) and (
                start + LATIN1_RANGE == len(entries) or not entries[start + LATIN1_RANGE][3]
            ):
                before = [i for i in range(330, start) if entries[i][3]]
                after = [
                    i
                    for i in range(start + LATIN1_RANGE, len(entries))
                    if entries[i][3]
                ]
                if not before or not after:
                    break
                return start, before[-1], after[0]
    raise SystemExit("font: the 64-glyph Cyrillic block was not found")


def remap_font(source: Path, out_path: Path) -> None:
    data, entries = read_fnt(source)
    start, yo, yo_small = font_layout(entries)
    if max(LATIN1_ENTRY, LATIN1_YO, LATIN1_YO_SMALL) + LATIN1_RANGE > len(entries):
        raise SystemExit(f"{source}: the table is too short for the Latin-1 slots")
    for i in range(LATIN1_RANGE):
        src = 0x0C + 8 * (start + i)
        dst = 0x0C + 8 * (LATIN1_ENTRY + i)
        data[dst:dst + 8] = data[src:src + 8]
    for src, dst in ((yo, LATIN1_YO), (yo_small, LATIN1_YO_SMALL)):
        s = 0x0C + 8 * src
        d = 0x0C + 8 * dst
        data[d:d + 8] = data[s:s + 8]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)


def copy_file(source: Path, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, out_path)


def build_overlay(supported: Path, russian: Path, out: Path) -> dict:
    check_xex(supported / "default.xex", SUPPORTED_XEX_SHA256, "supported")
    check_xex(russian / "default.xex", RUSSIAN_XEX_SHA256, "Russian")

    text_supported = find_dir(supported, "text")
    text_russian = find_dir(russian, "text")
    fonts_supported = find_dir(supported, "textures", "Textures")
    fonts_russian = find_dir(russian, "Textures", "textures")
    if not text_supported or not text_russian or not fonts_supported or not fonts_russian:
        raise SystemExit("a data folder has no text/ or textures/ folder")

    out.mkdir(parents=True, exist_ok=True)
    merged = 0
    for source in sorted((text_supported / "eng").glob("*.lxb")):
        target = text_russian / "rus" / source.name
        if not target.is_file():
            continue
        merge_lxb(source, target, out / "text" / "eng" / source.name, shift=True)
        merged += 1
    global_base = text_supported / "eng" / "global.lxb"
    global_ru = text_russian / "rus" / "global.lxb"
    if not global_base.is_file() or not global_ru.is_file():
        raise SystemExit("text/eng/global.lxb or text/rus/global.lxb is missing")
    merge_lxb(global_base, global_ru, out / "text" / "eng" / "global.port.lxb", shift=False)

    for font in FONTS:
        remap_font(fonts_russian / f"{font}.fnt", out / "textures" / f"{font}.fnt")
        copy_file(fonts_russian / f"{font}.apk", out / "textures" / f"{font}.apk")

    copy_file(russian / "audio" / "rus" / "voice.bnk", out / "audio" / "eng" / "voice.bnk")
    uituning = russian / "ui" / "rus" / "uituning.lxb"
    if uituning.is_file():
        copy_file(uituning, out / "ui" / "eng" / "uituning.lxb")
    legal = russian / "movies" / "rus" / "legal.bik"
    if legal.is_file():
        copy_file(legal, out / "movies" / "eng" / "legal.bik")

    (out / "README.txt").write_text(
        "Russian language import for the supported 0.0.0.2 data.\n"
        f"supported default.xex SHA-256: {SUPPORTED_XEX_SHA256}\n"
        f"Russian default.xex SHA-256:   {RUSSIAN_XEX_SHA256}\n"
        f"merged .lxb files: {merged} (including global.lxb)\n\n"
        "Apply this overlay to a copy of the supported extracted data, then start\n"
        "the normal build with --language=english.  The Russian XEX is not used.\n"
        "global.port.lxb is the unshifted table for the port's own menus; the game\n"
        "reads the shifted global.lxb.\n",
        encoding="utf-8",
    )
    return {"merged": merged, "out": out}


def apply_overlay(overlay: Path, target: Path, force: bool) -> None:
    check_xex(target / "default.xex", SUPPORTED_XEX_SHA256, "target")
    marker = target / "text" / "eng" / ".russian-import"
    if marker.exists() and not force:
        raise SystemExit(f"{target} already has an import marker; pass --force to overwrite")
    for source in sorted(overlay.rglob("*")):
        if not source.is_file() or source.name == "README.txt":
            continue
        dest = target / source.relative_to(overlay)
        dest.parent.mkdir(parents=True, exist_ok=True)
        # A target built as a symlink farm must not be written through.
        if dest.is_symlink() or dest.exists():
            dest.unlink()
        shutil.copyfile(source, dest)
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text("Russian language import; see docs/game/russian-language-import.md\n", encoding="utf-8")
    print(f"applied the overlay to {target}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--supported", required=True, type=Path, help="extracted 0.0.0.2 disc data")
    parser.add_argument("--russian", required=True, type=Path, help="extracted 0.0.0.4 disc data")
    parser.add_argument("--out", required=True, type=Path, help="overlay folder to write")
    parser.add_argument("--apply", type=Path, help="copy the overlay into this copy of the supported data")
    parser.add_argument("--force", action="store_true", help="overwrite an existing import in --apply")
    args = parser.parse_args(argv)
    result = build_overlay(args.supported, args.russian, args.out)
    print(f"wrote {result['merged']} merged text files and the fonts/voice/UI to {args.out}")
    if args.apply:
        apply_overlay(args.out, args.apply, args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
