#!/usr/bin/env python3
"""Unit tests for tools/add_russian_language.py (no game data needed)."""

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
