#!/usr/bin/env python3
"""Unit tests for tools/add_russian_language.py (no game data needed)."""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import add_russian_language as tool  # noqa: E402


def make_lxb(entries):
    """A minimal .lxb: header, count, (key, offset) pairs, strings; no name table."""
    count = len(entries)
    table_end = 0x80 + 8 * count
    strings = bytearray()
    pairs = []
    for i, (key, text) in enumerate(entries):
        start = table_end + len(strings)
        pairs.append((key, start - (0x80 + 8 * i + 4)))
        strings += text + b"\0"
    header = bytearray(0x80)
    struct.pack_into(">I", header, 0x7C, count)
    out = bytearray(header)
    for key, offset in pairs:
        out += struct.pack(">II", key, offset)
    out += strings
    return bytes(out)


def make_fnt(block_start=369, yo=354, yo_small=434, total=435):
    data = bytearray(0x0C + 8 * total)
    for i in range(total):
        at = 0x0C + 8 * i
        data[at + 4] = i & 0xFF  # ink: a unique marker for each entry
        data[at + 5] = 0  # cell
    for i in range(block_start, block_start + 64):
        data[0x0C + 8 * i + 5] = 1
    data[0x0C + 8 * yo + 5] = 1
    data[0x0C + 8 * yo_small + 5] = 1
    return bytes(data)


class ShiftTests(unittest.TestCase):
    def test_main_alphabet(self):
        self.assertEqual(tool.shift_text("Нажать"), "Íàæàòü")
        self.assertEqual(tool.shift_text("чтобы начать"), "÷òîáû íà÷àòü")

    def test_yo(self):
        self.assertEqual(tool.shift_text("Ёё"), "\u00a8\u00b8")

    def test_ascii_and_latin1(self):
        self.assertEqual(tool.shift_text("Press A"), "Press A")
        self.assertEqual(tool.shift_text("Grüße"), "Grüße")


class LxbTests(unittest.TestCase):
    def test_merge_and_shift(self):
        base = make_lxb([(1, b"Press"), (2, b"Hello"), (3, b"Keep")])
        russian = make_lxb([(1, "Нажать".encode("utf-8")), (2, "Привет".encode("utf-8"))])
        with tempfile.TemporaryDirectory() as tmp:
            base_path = Path(tmp) / "base.lxb"
            ru_path = Path(tmp) / "ru.lxb"
            out_path = Path(tmp) / "out.lxb"
            base_path.write_bytes(base)
            ru_path.write_bytes(russian)
            count = tool.merge_lxb(base_path, ru_path, out_path, shift=True)
            self.assertEqual(count, 3)
            _, entries, _ = tool.parse_lxb(out_path)
            values = dict(entries)
            self.assertEqual(values[1].decode("utf-8"), "Íàæàòü")
            self.assertEqual(values[2].decode("utf-8"), "Ïðèâåò")
            self.assertEqual(values[3], b"Keep")

    def test_merge_without_shift(self):
        base = make_lxb([(1, b"Press")])
        russian = make_lxb([(1, "Нажать".encode("utf-8"))])
        with tempfile.TemporaryDirectory() as tmp:
            base_path = Path(tmp) / "base.lxb"
            ru_path = Path(tmp) / "ru.lxb"
            out_path = Path(tmp) / "out.lxb"
            base_path.write_bytes(base)
            ru_path.write_bytes(russian)
            tool.merge_lxb(base_path, ru_path, out_path, shift=False)
            _, entries, _ = tool.parse_lxb(out_path)
            self.assertEqual(dict(entries)[1].decode("utf-8"), "Нажать")


class LookupTests(unittest.TestCase):
    def test_lookup_is_case_aware(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Textures").mkdir()
            (root / "global.LXB").write_bytes(b"x")
            self.assertEqual(tool.lookup(root, "textures", "dir"), root / "Textures")
            self.assertEqual(tool.lookup(root, "global.lxb", "file"), root / "global.LXB")
            self.assertIsNone(tool.lookup(root, "missing", "dir"))

    def test_require_reports_the_missing_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as caught:
                tool.require(Path(tmp), "textures", "dir", "supported")
            self.assertIn("textures", str(caught.exception))

    def test_require_rejects_ambiguous_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "TEXT").mkdir()
            (root / "TeXt").mkdir()
            with self.assertRaises(SystemExit) as caught:
                tool.require(root, "text", "dir", "supported")
            self.assertIn("more than one", str(caught.exception))


class ApplyTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.target = self.root / "data"
        self.overlay = self.root / "overlay"
        (self.target / "text" / "eng").mkdir(parents=True)
        (self.overlay / "text" / "eng").mkdir(parents=True)
        (self.target / "default.xex").write_bytes(b"supported")
        (self.overlay / "text" / "eng" / "global.lxb").write_bytes(b"new")
        (self.overlay / "text" / "eng" / "extra.lxb").write_bytes(b"fresh")
        (self.overlay / "README.txt").write_text("not applied")
        self._hash = tool.SUPPORTED_XEX_SHA256
        tool.SUPPORTED_XEX_SHA256 = tool.sha256(self.target / "default.xex")

    def tearDown(self):
        tool.SUPPORTED_XEX_SHA256 = self._hash
        self._tmp.cleanup()

    def test_apply_then_restore_keeps_original_and_absent(self):
        original = self.target / "text" / "eng" / "global.lxb"
        original.write_bytes(b"original")
        tool.apply_overlay(self.overlay, self.target, force=False)
        self.assertEqual(original.read_bytes(), b"new")
        self.assertTrue((self.target / "text" / "eng" / "extra.lxb").is_file())
        self.assertTrue((self.target / tool.MARKER).is_file())

        tool.restore_overlay(self.target)
        self.assertEqual(original.read_bytes(), b"original")
        self.assertFalse((self.target / "text" / "eng" / "extra.lxb").exists())
        self.assertFalse((self.target / tool.MARKER).exists())
        self.assertFalse((self.target / tool.BACKUP_DIR).exists())

    def test_failed_copy_restores_the_originals(self):
        original = self.target / "text" / "eng" / "global.lxb"
        original.write_bytes(b"original")
        real = tool.atomic_copy

        def flaky(source, dest):
            if dest.name == "global.lxb" and self.overlay in source.parents:
                raise OSError("disk full")
            real(source, dest)

        tool.atomic_copy = flaky
        try:
            with self.assertRaises(OSError):
                tool.apply_overlay(self.overlay, self.target, force=False)
        finally:
            tool.atomic_copy = real
        self.assertEqual(original.read_bytes(), b"original")
        self.assertFalse((self.target / "text" / "eng" / "extra.lxb").exists())
        self.assertTrue((self.target / tool.BACKUP_DIR).is_dir())

        tool.restore_overlay(self.target)
        self.assertEqual(original.read_bytes(), b"original")
        self.assertFalse((self.target / tool.BACKUP_DIR).exists())

    def test_apply_reuses_an_existing_case(self):
        (self.target / "Textures").mkdir()
        (self.overlay / "textures").mkdir()
        (self.overlay / "textures" / "arial.fnt").write_bytes(b"font")
        tool.apply_overlay(self.overlay, self.target, force=False)
        self.assertTrue((self.target / "Textures" / "arial.fnt").is_file())
        self.assertFalse((self.target / "textures").exists())

    def test_apply_refuses_a_symlinked_directory(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.target / "textures").symlink_to(outside, target_is_directory=True)
        (self.overlay / "textures").mkdir()
        (self.overlay / "textures" / "arial.fnt").write_bytes(b"font")
        with self.assertRaises(SystemExit):
            tool.apply_overlay(self.overlay, self.target, force=False)
        self.assertFalse((outside / "arial.fnt").exists())

    def test_marker_uses_the_existing_case_variant_parent(self):
        target = self.root / "cased"
        (target / "Text" / "Eng").mkdir(parents=True)
        (target / "default.xex").write_bytes(b"supported")
        tool.apply_overlay(self.overlay, target, force=False)
        self.assertTrue((target / "Text" / "Eng" / "global.lxb").is_file())
        self.assertTrue((target / "Text" / "Eng" / ".russian-import").is_file())
        self.assertFalse((target / "text").exists())
        tool.restore_overlay(target)
        self.assertFalse((target / "Text" / "Eng" / ".russian-import").exists())

    def test_reserved_bookkeeping_is_not_an_overlay_input(self):
        (self.overlay / "text" / "eng" / ".russian-import").write_text("from the overlay")
        evil = self.overlay / tool.BACKUP_DIR / "files" / "evil.bin"
        evil.parent.mkdir(parents=True)
        evil.write_bytes(b"evil")
        tool.apply_overlay(self.overlay, self.target, force=False)
        marker = self.target / "text" / "eng" / ".russian-import"
        self.assertEqual(
            marker.read_text(encoding="utf-8"),
            "Russian language import; see docs/game/russian-language-import.md\n",
        )
        self.assertFalse((self.target / tool.BACKUP_DIR / "files" / "evil.bin").exists())

    def test_restore_refuses_a_linked_backup(self):
        original = self.target / "text" / "eng" / "global.lxb"
        original.write_bytes(b"original")
        outside = self.root / "outside"
        (outside / tool.BACKUP_FILES).mkdir(parents=True)
        (outside / tool.BACKUP_MANIFEST).write_text(
            json.dumps(
                {"version": 1, "files": [{"path": "text/eng/global.lxb", "state": "absent"}]}
            ),
            encoding="utf-8",
        )
        (self.target / tool.BACKUP_DIR).symlink_to(outside, target_is_directory=True)
        with self.assertRaises(SystemExit) as caught:
            tool.restore_overlay(self.target)
        self.assertIn("linked backup", str(caught.exception))
        self.assertEqual(original.read_bytes(), b"original")

    def test_restore_validates_the_manifest_before_changing_anything(self):
        original = self.target / "text" / "eng" / "global.lxb"
        original.write_bytes(b"original")
        tool.apply_overlay(self.overlay, self.target, force=False)
        saved = self.target / tool.BACKUP_DIR / tool.BACKUP_FILES / "text" / "eng" / "global.lxb"
        saved.write_bytes(b"tampered")
        with self.assertRaises(SystemExit) as caught:
            tool.restore_overlay(self.target)
        self.assertIn("expected", str(caught.exception))
        self.assertTrue((self.target / "text" / "eng" / "extra.lxb").is_file())
        self.assertEqual(original.read_bytes(), b"new")
        self.assertTrue((self.target / tool.BACKUP_DIR).is_dir())

    def test_restore_rejects_a_reserved_manifest_path(self):
        tool.apply_overlay(self.overlay, self.target, force=False)
        manifest_path = self.target / tool.BACKUP_DIR / tool.BACKUP_MANIFEST
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["files"].append({"path": ".russian-import-backup/files/evil", "state": "absent"})
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(SystemExit) as caught:
            tool.restore_overlay(self.target)
        self.assertIn("reserved", str(caught.exception))
        self.assertTrue((self.target / "text" / "eng" / "extra.lxb").is_file())
        self.assertTrue((self.target / tool.BACKUP_DIR / tool.BACKUP_FILES).is_dir())

    def test_legacy_marker_without_backup_is_refused_when_it_would_overwrite(self):
        original = self.target / "text" / "eng" / "global.lxb"
        original.write_bytes(b"legacy")
        (self.target / "text" / "eng" / ".russian-import").write_text("old import\n")
        with self.assertRaises(SystemExit) as caught:
            tool.apply_overlay(self.overlay, self.target, force=True)
        self.assertIn("no backup", str(caught.exception))
        self.assertEqual(original.read_bytes(), b"legacy")
        self.assertFalse((self.target / "text" / "eng" / "extra.lxb").exists())

    def test_legacy_marker_without_backup_allows_a_pure_addition(self):
        (self.target / "text" / "eng" / ".russian-import").write_text("old import\n")
        tool.apply_overlay(self.overlay, self.target, force=True)
        self.assertTrue((self.target / "text" / "eng" / "global.lxb").is_file())
        self.assertTrue((self.target / "text" / "eng" / "extra.lxb").is_file())
        tool.restore_overlay(self.target)
        self.assertFalse((self.target / "text" / "eng" / "global.lxb").exists())
        self.assertFalse((self.target / "text" / "eng" / "extra.lxb").exists())


class BuildOverlayTests(unittest.TestCase):
    def _disc(self, root, hash_text):
        (root / "text" / "eng").mkdir(parents=True)
        (root / "text" / "rus").mkdir(parents=True)
        (root / "textures").mkdir()
        (root / "default.xex").write_bytes(hash_text)
        for name in ("global", "level_027"):
            (root / "text" / "eng" / f"{name}.lxb").write_bytes(make_lxb([(1, b"Press")]))
            (root / "text" / "rus" / f"{name}.lxb").write_bytes(
                make_lxb([(1, "Нажать".encode("utf-8"))])
            )
        for font in tool.FONTS:
            (root / "textures" / f"{font}.fnt").write_bytes(make_fnt())
            (root / "textures" / f"{font}.apk").write_bytes(b"page")
        (root / "audio" / "rus").mkdir(parents=True)
        (root / "audio" / "rus" / "voice.bnk").write_bytes(b"voice")

    def test_builds_overlay_from_case_variant_disc(self):
        saved_supported, saved_russian = tool.SUPPORTED_XEX_SHA256, tool.RUSSIAN_XEX_SHA256
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            supported = root / "supported"
            russian = root / "russian"
            self._disc(supported, b"supported")
            # A Windows-style extraction: uppercase folders the lookup must still find.
            (russian / "Text" / "Eng").mkdir(parents=True)
            (russian / "Text" / "Rus").mkdir(parents=True)
            (russian / "Textures").mkdir()
            (russian / "default.xex").write_bytes(b"russian")
            for name in ("global", "level_027"):
                (russian / "Text" / "Eng" / f"{name}.lxb").write_bytes(make_lxb([(1, b"x")]))
                (russian / "Text" / "Rus" / f"{name}.lxb").write_bytes(
                    make_lxb([(1, "Нажать".encode("utf-8"))])
                )
            for font in tool.FONTS:
                (russian / "Textures" / f"{font}.fnt").write_bytes(make_fnt())
                (russian / "Textures" / f"{font}.apk").write_bytes(b"page")
            (russian / "Audio" / "Rus").mkdir(parents=True)
            (russian / "Audio" / "Rus" / "voice.bnk").write_bytes(b"voice")
            tool.SUPPORTED_XEX_SHA256 = tool.sha256(supported / "default.xex")
            tool.RUSSIAN_XEX_SHA256 = tool.sha256(russian / "default.xex")
            try:
                result = tool.build_overlay(supported, russian, root / "out")
            finally:
                tool.SUPPORTED_XEX_SHA256, tool.RUSSIAN_XEX_SHA256 = saved_supported, saved_russian
            out = result["out"]
            self.assertEqual(result["merged"], 2)
            _, entries, _ = tool.parse_lxb(out / "text" / "eng" / "global.lxb")
            self.assertEqual(dict(entries)[1].decode("utf-8"), "Íàæàòü")
            self.assertTrue((out / "text" / "eng" / "global.port.lxb").is_file())
            self.assertTrue((out / "textures" / "arial.fnt").is_file())
            self.assertTrue((out / "audio" / "eng" / "voice.bnk").is_file())


class FontTests(unittest.TestCase):
    def test_layout_and_remap(self):
        data = make_fnt()
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "arial.fnt"
            out = Path(tmp) / "arial-out.fnt"
            source.write_bytes(data)
            _, entries = tool.read_fnt(source)
            self.assertEqual(tool.font_layout(entries), (369, 354, 434))
            tool.remap_font(source, out)
            _, remapped = tool.read_fnt(out)
            for i in range(64):
                self.assertEqual(remapped[225 + i][2], (369 + i) & 0xFF)
            self.assertEqual(remapped[201][2], 354 & 0xFF)
            self.assertEqual(remapped[217][2], 434 & 0xFF)

    def test_layout_other_font(self):
        data = make_fnt(block_start=386, yo=371, yo_small=451, total=454)
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "ltnob.fnt"
            source.write_bytes(data)
            _, entries = tool.read_fnt(source)
            self.assertEqual(tool.font_layout(entries), (386, 371, 451))


if __name__ == "__main__":
    unittest.main()
