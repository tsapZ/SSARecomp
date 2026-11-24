#!/usr/bin/env python3
"""Import the Russian 0.0.0.4 text, fonts and voices into a 0.0.0.2 data folder.

The supported 0.0.0.2 executable maps the game font in ASCII and U+00A1..U+00FF only.
The Russian font files carry the Cyrillic alphabet in 64 extra glyphs.  This tool
copies those glyphs into the Latin-1 slots the executable already knows, and shifts
each Russian character to the Latin-1 codepoint with the same cp1251 byte.  The
game still runs its own 0.0.0.2 XEX; only the data changes.

It writes an overlay folder (text/eng, textures, audio/eng, ui/eng, movies/eng) and,
with --apply, copies that overlay into a copy of the supported data folder.  --apply is
transactional: it keeps a recoverable copy of every file it replaces (and records the
files that did not exist yet) under .russian-import-backup, restores the originals if a
copy fails while keeping the backup for a later --restore, and --restore undoes the
import.  It never writes the XEX, and it refuses to run on a data folder whose default.xex
is not the supported one.

Only the Python standard library is used.  See docs/game/russian-language-import.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import tempfile
from contextlib import contextmanager
from pathlib import Path

SUPPORTED_XEX_SHA256 = "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b"
RUSSIAN_XEX_SHA256 = "0598a9638d92ec0070f4fb701323c132427dd76af3cd5a0ebc1d61f211b84041"
FONTS = ("arial", "bleeker", "lt", "ltnob", "robotic")
LATIN1_ENTRY = 225  # U+00C0: entry code + 33
LATIN1_YO = 201     # U+00A8
LATIN1_YO_SMALL = 217  # U+00B8
LATIN1_RANGE = 64

MARKER = Path("text", "eng", ".russian-import")
BACKUP_DIR = ".russian-import-backup"
BACKUP_FILES = "files"
BACKUP_MANIFEST = "manifest.json"
LOCK_FILE = ".russian-import-lock"


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


def _matches(parent: Path, name: str, kind: str) -> list[Path]:
    if not parent.is_dir():
        return []
    return [
        entry
        for entry in sorted(parent.iterdir())
        if entry.name.lower() == name.lower() and (entry.is_dir() if kind == "dir" else entry.is_file())
    ]


def lookup(parent: Path, name: str, kind: str) -> Path | None:
    """Find `name` under `parent`: the exact spelling first, then one case-insensitive match."""
    exact = parent / name
    if parent.is_dir() and (exact.is_dir() if kind == "dir" else exact.is_file()):
        return exact
    matches = _matches(parent, name, kind)
    return matches[0] if len(matches) == 1 else None


def require(parent: Path, name: str, kind: str, label: str) -> Path:
    """Like lookup, but raises a precise error when the entry is missing or ambiguous."""
    exact = parent / name
    if parent.is_dir() and (exact.is_dir() if kind == "dir" else exact.is_file()):
        return exact
    matches = _matches(parent, name, kind)
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        names = ", ".join(entry.name for entry in matches)
        raise SystemExit(f"{label}: {parent} has more than one {kind} matching {name!r}: {names}")
    raise SystemExit(f"{label}: no {kind} {name!r} under {parent}")


def find_dir(parent: Path, *names: str) -> Path | None:
    for name in names:
        found = lookup(parent, name, "dir")
        if found is not None:
            return found
    return None


def lxb_files(directory: Path) -> list[Path]:
    return sorted(
        entry for entry in directory.iterdir() if entry.is_file() and entry.suffix.lower() == ".lxb"
    )


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
    check_xex(require(supported, "default.xex", "file", "supported"), SUPPORTED_XEX_SHA256, "supported")
    check_xex(require(russian, "default.xex", "file", "Russian"), RUSSIAN_XEX_SHA256, "Russian")

    text_supported = require(supported, "text", "dir", "supported")
    text_russian = require(russian, "text", "dir", "Russian")
    if find_dir(supported, "textures", "Textures") is None:
        raise SystemExit(f"supported: no textures/ folder under {supported}")
    fonts_russian = find_dir(russian, "Textures", "textures")
    if fonts_russian is None:
        raise SystemExit(f"Russian: no Textures/ folder under {russian}")
    eng_supported = require(text_supported, "eng", "dir", "supported text")
    rus_russian = require(text_russian, "rus", "dir", "Russian text")

    out.mkdir(parents=True, exist_ok=True)
    merged = 0
    for source in lxb_files(eng_supported):
        target = lookup(rus_russian, source.name, "file")
        if target is None:
            continue
        merge_lxb(source, target, out / "text" / "eng" / source.name, shift=True)
        merged += 1
    global_base = require(eng_supported, "global.lxb", "file", "supported text")
    global_ru = require(rus_russian, "global.lxb", "file", "Russian text")
    merge_lxb(global_base, global_ru, out / "text" / "eng" / "global.port.lxb", shift=False)

    for font in FONTS:
        remap_font(
            require(fonts_russian, f"{font}.fnt", "file", "Russian fonts"),
            out / "textures" / f"{font}.fnt",
        )
        copy_file(
            require(fonts_russian, f"{font}.apk", "file", "Russian fonts"),
            out / "textures" / f"{font}.apk",
        )

    audio = require(russian, "audio", "dir", "Russian")
    audio_rus = require(audio, "rus", "dir", "Russian audio")
    copy_file(
        require(audio_rus, "voice.bnk", "file", "Russian audio"),
        out / "audio" / "eng" / "voice.bnk",
    )
    ui = find_dir(russian, "ui")
    if ui is not None:
        ui_rus = lookup(ui, "rus", "dir")
        uituning = lookup(ui_rus, "uituning.lxb", "file") if ui_rus is not None else None
        if uituning is not None:
            copy_file(uituning, out / "ui" / "eng" / "uituning.lxb")
    movies = find_dir(russian, "movies")
    if movies is not None:
        movies_rus = lookup(movies, "rus", "dir")
        legal = lookup(movies_rus, "legal.bik", "file") if movies_rus is not None else None
        if legal is not None:
            copy_file(legal, out / "movies" / "eng" / "legal.bik")

    (out / "README.txt").write_text(
        "Russian language import for the supported 0.0.0.2 data.\n"
        f"supported default.xex SHA-256: {SUPPORTED_XEX_SHA256}\n"
        f"Russian default.xex SHA-256:   {RUSSIAN_XEX_SHA256}\n"
        f"merged .lxb files: {merged} (including global.lxb)\n\n"
        "Apply this overlay to a copy of the supported extracted data, then start\n"
        "the normal build with --language=english.  The Russian XEX is not used.\n"
        "global.port.lxb is the unshifted table for the port's own menus; the game\n"
        "reads the shifted global.lxb.  --restore undoes an --apply.\n",
        encoding="utf-8",
    )
    return {"merged": merged, "out": out}


def resolve_case(parent: Path, name: str) -> Path:
    """`parent/name`, but reuse an existing entry that differs only in case."""
    exact = parent / name
    if exact.exists() or exact.is_symlink():
        return exact
    if not parent.is_dir():
        return exact
    matches = [entry for entry in sorted(parent.iterdir()) if entry.name.lower() == name.lower()]
    if len(matches) > 1:
        names = ", ".join(entry.name for entry in matches)
        raise SystemExit(f"{parent} has more than one entry matching {name!r}: {names}")
    return matches[0] if matches else exact


def relative_destination(root: Path, relative: Path) -> Path:
    """Resolve `relative` under `root`, case-awarely, and refuse anything outside or a symlink."""
    if relative.is_absolute() or not relative.parts or any(
        part in ("", ".", "..") for part in relative.parts
    ):
        raise SystemExit(f"refusing a path outside the data folder: {relative}")
    root = root.resolve()
    current = root
    for part in relative.parts[:-1]:
        candidate = resolve_case(current, part)
        if candidate.is_symlink():
            raise SystemExit(f"refusing to write through the symlink {candidate}")
        if candidate.is_dir():
            try:
                candidate.resolve().relative_to(root)
            except ValueError:
                raise SystemExit(f"refusing to write through {candidate} outside {root}") from None
            current = candidate
        else:
            current = current / part
    dest = resolve_case(current, relative.parts[-1])
    if dest.parent.is_symlink():
        raise SystemExit(f"refusing to write through the symlink {dest.parent}")
    try:
        dest.parent.resolve().relative_to(root)
    except ValueError:
        raise SystemExit(f"refusing to write outside {root}: {relative}") from None
    if dest.is_symlink():
        raise SystemExit(f"refusing to replace the symlink {dest}")
    return dest


def atomic_copy(source: Path, dest: Path) -> None:
    """Copy to a sibling temporary file and rename it over dest, so dest is never partial."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=dest.name + ".", suffix=".tmp", dir=dest.parent)
    os.close(fd)
    try:
        shutil.copy2(source, name)
        os.replace(name, dest)
    except BaseException:
        try:
            os.unlink(name)
        except OSError:
            pass
        raise


def reserved(relative: Path) -> bool:
    """True for the import's own bookkeeping, which can never be an overlay input."""
    if relative.as_posix().lower() == MARKER.as_posix().lower():
        return True
    first = relative.parts[0].lower() if relative.parts else ""
    return first in (BACKUP_DIR, BACKUP_DIR + ".tmp", LOCK_FILE)


def overlay_entries(overlay: Path) -> list[Path]:
    entries = []
    for source in sorted(overlay.rglob("*")):
        if not source.is_file() or source == overlay / "README.txt":
            continue
        relative = source.relative_to(overlay)
        if reserved(relative):
            continue
        entries.append(relative)
    return entries


def preflight(target: Path, entries: list[Path]) -> None:
    seen = set()
    for relative in entries:
        dest = relative_destination(target, relative)
        key = dest.as_posix().casefold()
        if key in seen:
            raise SystemExit(f"{dest}: more than one overlay file would write this path")
        seen.add(key)
        if dest.exists() and not dest.is_file():
            raise SystemExit(f"{dest} is not a regular file")


def find_backup(target: Path) -> Path | None:
    """The backup folder, or None; a linked backup is refused rather than read."""
    backup = target / BACKUP_DIR
    if backup.is_symlink():
        raise SystemExit(f"{target}: refusing to use the linked backup {backup}")
    if not backup.exists():
        return None
    try:
        backup.resolve().relative_to(target.resolve())
    except ValueError:
        raise SystemExit(f"{target}: the backup {backup} is outside the data folder") from None
    return backup


def _no_links(root: Path, relative: Path, label: str) -> None:
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise SystemExit(f"{label}: refusing to read through the symlink {current}")


def write_backup(target: Path, entries: list[Path]) -> Path:
    backup = target / BACKUP_DIR
    staging = target / (BACKUP_DIR + ".tmp")
    if backup.is_symlink() or staging.is_symlink():
        raise SystemExit(f"{target}: refusing to use the linked backup {BACKUP_DIR}")
    if staging.exists():
        shutil.rmtree(staging)
    (staging / BACKUP_FILES).mkdir(parents=True)
    files = []
    for relative in entries:
        dest = relative_destination(target, relative)
        if dest.is_file():
            saved = staging / BACKUP_FILES / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dest, saved)
            files.append({"path": relative.as_posix(), "state": "saved", "sha256": sha256(dest)})
        elif dest.exists():
            raise SystemExit(f"{dest} is not a regular file")
        else:
            files.append({"path": relative.as_posix(), "state": "absent"})
    manifest = staging / (BACKUP_MANIFEST + ".tmp")
    manifest.write_text(json.dumps({"version": 1, "files": files}, indent=2), encoding="utf-8")
    os.replace(manifest, staging / BACKUP_MANIFEST)
    os.replace(staging, backup)
    return backup


def _restore_actions(target: Path, backup: Path) -> list[tuple[str, Path, Path | None]]:
    """Validate the whole manifest, including every saved digest, before changing anything."""
    manifest_path = backup / BACKUP_MANIFEST
    _no_links(backup, Path(BACKUP_MANIFEST), str(backup))
    if not manifest_path.is_file():
        raise SystemExit(f"{target}: no import backup at {backup}; nothing to restore")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"{manifest_path}: cannot read the restore manifest: {exc}") from None
    if not isinstance(manifest, dict) or manifest.get("version") != 1:
        raise SystemExit(f"{manifest_path}: unsupported restore manifest")
    entries = manifest.get("files")
    if not isinstance(entries, list):
        raise SystemExit(f"{manifest_path}: the manifest has no files list")
    actions = []
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise SystemExit(f"{manifest_path}: a manifest entry is not an object")
        raw = entry.get("path")
        if not isinstance(raw, str) or not raw:
            raise SystemExit(f"{manifest_path}: a manifest entry has no path")
        relative = Path(raw)
        key = relative.as_posix().lower()
        if key in seen:
            raise SystemExit(f"{manifest_path}: {raw!r} appears more than once")
        seen.add(key)
        if reserved(relative):
            raise SystemExit(f"{manifest_path}: {raw!r} is reserved for the import's bookkeeping")
        dest = relative_destination(target, relative)
        state = entry.get("state")
        if state == "saved":
            digest = entry.get("sha256")
            if not isinstance(digest, str) or not digest:
                raise SystemExit(f"{manifest_path}: {raw!r} has no saved-file digest")
            _no_links(backup, Path(BACKUP_FILES) / relative, str(backup))
            source = backup / BACKUP_FILES / relative
            if not source.is_file():
                raise SystemExit(f"{backup}: the backup of {raw} is missing")
            actual = sha256(source)
            if actual != digest:
                raise SystemExit(f"{backup}: the backup of {raw} is {actual}, expected {digest}")
            actions.append(("saved", dest, source))
        elif state == "absent":
            if dest.exists() and not dest.is_file():
                raise SystemExit(f"{dest} is not a regular file")
            actions.append(("absent", dest, None))
        else:
            raise SystemExit(f"{manifest_path}: unknown state {state!r} for {relative}")
    return actions


@contextmanager
def import_lock(target: Path):
    lock_path = target / LOCK_FILE
    if lock_path.is_symlink():
        raise SystemExit(f"{target}: refusing a linked import lock")
    flags = os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(lock_path, flags, 0o600), "r+b") as lock:
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise SystemExit(f"{target}: could not lock the import folder: {exc}") from None
        yield


def restore_overlay(target: Path) -> None:
    with import_lock(target):
        _restore_overlay(target)


def _restore_overlay(target: Path, quiet: bool = False, keep_backup: bool = False) -> None:
    backup = find_backup(target)
    if backup is None:
        raise SystemExit(f"{target}: no import backup at {target / BACKUP_DIR}; nothing to restore")
    actions = _restore_actions(target, backup)
    for state, dest, source in actions:
        if state == "saved":
            atomic_copy(source, dest)
        elif dest.is_file():
            dest.unlink()
    marker = relative_destination(target, MARKER)
    if marker.is_file():
        marker.unlink()
    if not keep_backup:
        shutil.rmtree(backup)
    if not quiet:
        print(f"restored the original files in {target}")


def preserves_recoverability(target: Path, entries: list[Path]) -> bool:
    """A re-apply stays recoverable only when it overwrites nothing."""
    return all(not relative_destination(target, relative).exists() for relative in entries)


def apply_overlay(overlay: Path, target: Path, force: bool) -> None:
    with import_lock(target):
        _apply_overlay(overlay, target, force)


def _apply_overlay(overlay: Path, target: Path, force: bool) -> None:
    check_xex(require(target, "default.xex", "file", "target"), SUPPORTED_XEX_SHA256, "target")
    entries = overlay_entries(overlay)
    if not entries:
        raise SystemExit(f"{overlay} has no files to apply")
    preflight(target, entries)
    marker = relative_destination(target, MARKER)
    backup = find_backup(target)
    if backup is not None:
        if marker.exists() and not force:
            raise SystemExit(
                f"{target} already has an import; pass --restore to undo it or --force to re-apply"
            )
        # A backup without the marker is an interrupted run: restore it before starting again.
        _restore_overlay(target, quiet=True)
    elif marker.exists():
        if not force:
            raise SystemExit(
                f"{target} already has an import marker but no backup; "
                "pass --force to re-apply without overwriting anything"
            )
        if not preserves_recoverability(target, entries):
            raise SystemExit(
                f"{target} has an import marker but no backup; --force would overwrite files "
                "that cannot be restored, so it is refused"
            )
    write_backup(target, entries)
    try:
        for relative in entries:
            atomic_copy(overlay / relative, relative_destination(target, relative))
        marker = relative_destination(target, MARKER)
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(
            "Russian language import; see docs/game/russian-language-import.md\n", encoding="utf-8"
        )
    except BaseException:
        try:
            _restore_overlay(target, quiet=True, keep_backup=True)
        except BaseException:
            pass
        raise
    print(f"applied the overlay to {target}; --restore undoes it")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--supported", type=Path, help="extracted 0.0.0.2 disc data")
    parser.add_argument("--russian", type=Path, help="extracted 0.0.0.4 disc data")
    parser.add_argument("--out", type=Path, help="overlay folder to write")
    parser.add_argument("--apply", type=Path, help="copy the overlay into this copy of the supported data")
    parser.add_argument("--restore", type=Path, help="undo an earlier --apply from its backup")
    parser.add_argument("--force", action="store_true", help="re-apply over an existing import")
    args = parser.parse_args(argv)
    if args.restore is not None:
        if args.apply is not None:
            parser.error("--restore and --apply cannot be used together")
        restore_overlay(args.restore)
        return 0
    if not (args.supported and args.russian and args.out):
        parser.error("--supported, --russian and --out are required unless --restore is used")
    result = build_overlay(args.supported, args.russian, args.out)
    print(f"wrote {result['merged']} merged text files and the fonts/voice/UI to {args.out}")
    if args.apply:
        apply_overlay(args.out, args.apply, args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
