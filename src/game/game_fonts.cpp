// The game's own fonts for the portal menus, read from the owner's game files at start-up, like
// the pictures (nothing of them is in the repository).
//
// This game's fonts are pairs of files in rom/textures: <name>.fnt, the glyph table, and
// <name>.apk, its page, an "APKF" package as ui_images.cpp reads them (a Xenos texture fetch
// constant after ff ff 00 00 ff ff 00 00: tiled flag in dword 0 bit 31, format in dword 1 bits 0-5,
// size in dword 2; in "SYHP" the texels' offset at +8 and their size at +16). The pages are 32 bits
// per texel (format 6, 8_8_8_8), Xbox 360 tiled, the bytes alpha, red, green, blue: the alpha holds
// the letter with its shadow (and in "lt" a dark outline), the colour the white letter, so the
// coverage taken here is the alpha times the brightness, the letter alone.
// A .fnt (2652 bytes): +0x02 u8 the line's height (60), +0x08 u16 the space's advance, then from
// +0x0C 330 entries of 8 bytes: u16 x, u16 y of the cell's top left in the page, u8 the ink's
// width, u8 the cell's width, u8, u8. Every cell is a line high and its width is the advance: fitted
// on the portal prompt's "Bitte setze einen Skylander" (font lt at 1.54 times its size), the 20
// letter distances inside the words come out within 0.9 px (0.5 px rms), with the ink's width plus
// the next byte up to 4 px off. Entry n is ASCII's '!' + n up to '~'; the Latin-1 letters
// (U+00A1 to U+00FF) are entry code + 33, all of them with German's ä ö ü ß (an earlier run of
// entries has some of them too but stops before ä).
// Of the five fonts the menus take "ltnob", the game's titles' letters without the outline "lt"
// has (the menus draw their own), for the titles and "bleeker", a rounded one, for the text.
// Worked out 2026-10-21 from this disc's files: the decoded pages show every glyph upright.

#include "game_fonts.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <rex/logging.h>

namespace ssarecomp {
namespace {

using skylanders::GameFont;
using skylanders::GameFontGlyph;
using skylanders::GameFontPage;

constexpr char kTitleFont[] = "ltnob";
constexpr char kBodyFont[] = "bleeker";
constexpr char kFontDir[] = "textures";

constexpr size_t kLineHeightField = 0x02;
constexpr size_t kSpaceField = 0x08;
constexpr size_t kTableStart = 0x0C;
constexpr size_t kEntrySize = 8;
constexpr size_t kLatin1Entry = 33;  // U+00A1 .. U+00FF: entry code + 33
constexpr uint32_t kFormat8888 = 6;

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

uint32_t Be32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

uint16_t Be16(const std::vector<uint8_t>& b, size_t at) { return uint16_t(b[at] << 8 | b[at + 1]); }

// Xbox 360 2D tiling (XGAddress2DTiledOffset): the byte offset of texel (x, y) of a surface `pitch`
// texels wide with 2^log2_bytes bytes per texel.
uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2_bytes) {
  pitch = (pitch + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2_bytes + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bytes;
  const uint32_t off = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((off & ~0x1FFu) << 3) + ((y & 16) << 7) + ((off & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 0x3F);
}

// The page of an .apk as coverage, or nullopt.
std::optional<GameFontPage> ReadPage(const std::vector<uint8_t>& apk) {
  static constexpr uint8_t kFetchMarker[] = {0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0};
  static constexpr uint8_t kPhys[] = {'S', 'Y', 'H', 'P'};
  const auto fetch = std::search(apk.begin(), apk.end(), std::begin(kFetchMarker), std::end(kFetchMarker));
  const auto phys = std::search(apk.begin(), apk.end(), std::begin(kPhys), std::end(kPhys));
  if (fetch == apk.end() || phys == apk.end()) return std::nullopt;
  const size_t f = size_t(fetch - apk.begin()) + sizeof(kFetchMarker), p = size_t(phys - apk.begin());
  if (f + 12 > apk.size() || p + 20 > apk.size()) return std::nullopt;
  const uint32_t d0 = Be32(apk, f), d1 = Be32(apk, f + 4), d2 = Be32(apk, f + 8);
  const uint32_t offset = Be32(apk, p + 8), size = Be32(apk, p + 16);
  if ((d1 & 0x3F) != kFormat8888 || uint64_t(offset) + size > apk.size()) return std::nullopt;
  GameFontPage page;
  page.width = int(d2 & 0x1FFF) + 1;
  page.height = int((d2 >> 13) & 0x1FFF) + 1;
  const bool tiled = d0 >> 31;
  page.alpha.resize(size_t(page.width) * page.height);
  for (int y = 0; y < page.height; ++y) {
    for (int x = 0; x < page.width; ++x) {
      const size_t at = tiled ? TiledOffset2D(uint32_t(x), uint32_t(y), uint32_t(page.width), 2)
                              : (size_t(y) * page.width + x) * 4;
      if (at + 4 > size) return std::nullopt;
      const uint8_t* t = apk.data() + offset + at;
      page.alpha[size_t(y) * page.width + x] = uint8_t(t[0] * std::max({t[1], t[2], t[3]}) / 255);
    }
  }
  return page;
}

// The character of table entry `n`, 0 for none.
uint32_t EntryCodepoint(size_t n) {
  if (n <= size_t('~' - '!')) return uint32_t('!' + n);
  if (n >= 0xA1 + kLatin1Entry && n <= 0xFF + kLatin1Entry) return uint32_t(n - kLatin1Entry);
  return 0;
}

// From a line's top to the baseline: the bottom of the H's ink.
float Ascent(const GameFont& font) {
  for (const GameFontGlyph& g : font.glyphs) {
    if (g.codepoint != 'H') continue;
    const GameFontPage& page = font.pages[g.page];
    for (int row = g.height - 1; row >= 0; --row) {
      for (int col = 0; col < g.width; ++col) {
        if (page.alpha[size_t(g.y + row) * page.width + g.x + col] >= 128) {
          return g.y_offset + row + 1;
        }
      }
    }
  }
  return font.size * 0.8f;
}

std::shared_ptr<const GameFont> ReadFont(const std::filesystem::path& dir, const char* name) {
  const auto fnt = ReadFile(dir / (std::string(name) + ".fnt"));
  const auto apk = ReadFile(dir / (std::string(name) + ".apk"));
  if (!fnt || !apk || fnt->size() < kTableStart) return nullptr;
  auto page = ReadPage(*apk);
  if (!page) return nullptr;
  auto font = std::make_shared<GameFont>();
  const int line = (*fnt)[kLineHeightField];
  font->size = float(line);
  font->pages.push_back(std::move(*page));
  const GameFontPage& p = font->pages.front();
  GameFontGlyph space;
  space.codepoint = ' ';
  space.advance = Be16(*fnt, kSpaceField);
  font->glyphs.push_back(space);
  for (size_t n = 0; kTableStart + (n + 1) * kEntrySize <= fnt->size(); ++n) {
    const size_t at = kTableStart + n * kEntrySize;
    const uint32_t codepoint = EntryCodepoint(n);
    const int cell = (*fnt)[at + 5];
    if (!codepoint || cell == 0) continue;
    GameFontGlyph glyph;
    glyph.codepoint = codepoint;
    glyph.advance = float(cell);
    glyph.x = Be16(*fnt, at);
    glyph.y = Be16(*fnt, at + 2);
    if (glyph.x >= p.width || glyph.y >= p.height) continue;
    glyph.width = std::min(cell, p.width - glyph.x);
    glyph.height = std::min(line, p.height - glyph.y);
    font->glyphs.push_back(glyph);
  }
  if (font->glyphs.size() < 2 || font->size <= 0) return nullptr;
  font->ascent = Ascent(*font);
  return font;
}

}  // namespace

skylanders::GameFonts LoadGameFonts(const std::filesystem::path& rom_dir) {
  const auto start = std::chrono::steady_clock::now();
  skylanders::GameFonts fonts;
  fonts.title = ReadFont(rom_dir / kFontDir, kTitleFont);
  fonts.body = ReadFont(rom_dir / kFontDir, kBodyFont);
  REXLOG_INFO("Game fonts: title {} glyphs, body {} glyphs, in {} ms",
              fonts.title ? fonts.title->glyphs.size() : 0,
              fonts.body ? fonts.body->glyphs.size() : 0,
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start).count());
  return fonts;
}

}  // namespace ssarecomp
