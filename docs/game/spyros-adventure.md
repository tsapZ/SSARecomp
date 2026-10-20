# Skylanders: Spyro's Adventure (Xbox 360)

What we know about the game and the dump this project targets, and what still has to be checked.

## The game

- The first Skylanders game, 2011, published by Activision, developed by Toys for Bob. Its
  figures (Series 1) also work in every later game; figures of later games don't work here.

## Target dump

| Property | Value | Source |
|---|---|---|
| ISO size | 8,738,846,720 bytes | measured |
| Disc content | 1,142 files, 8,101,645,980 bytes | `extract-xiso`, measured |
| `default.xex` size | 12,595,200 bytes, in the disc root | measured |
| `default.xex` SHA-256 | `e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b` | measured, in `default.xex.sha256` |
| Title ID | `415608AB` (media ID `53CE7B97`) | XEX execution info |
| Version | `0.0.0.2` (base version the same), disc 1 of 1 | XEX execution info |
| Region flags | `0xFFFFFFFF`: region free | XEX security info |
| Languages | text and voices in ten: English, German, French, Spanish, Italian, Dutch, Danish, Finnish, Norwegian, Swedish (`text\<lang>`, `audio\<lang>\voice.bnk`) | disc folders |

## Differences from Swap Force that matter

- **Other file formats.** Swap Force (Vicarious Visions, 2013) keeps its data in Alchemy `.pak`
  archives with `.igz` / `.igx` / `.mua` files, which SwapForceRecomp reads for the portal
  menu's pictures, names, hats and upgrades. This disc has none of them: `.lxb` (702 files),
  `.str` (227), `.lsa`, `.bnk` / `.pck` (audio), `.bik` (Bink 1 movies), `.fnt`, in folders
  `levels\`, `models\`, `textures\`, `text\`, `ui\`. So none of that code was taken over;
  the portal menu draws its own cards until this game's formats are read.
- **It sets up the GPU ring buffer three times** (`VdInitializeRingBuffer`, when it recreates its
  device); Swap Force does once. The SDK needed a fix for that (fork branch
  `fix/ring-buffer-reinit`, docs/STATUS.md 2026-10-20).
- **No game hooks yet.** Every hook of SwapForceRecomp is at an address in its own executable;
  none of them apply here.
