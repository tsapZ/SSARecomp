# CLAUDE.md: SSARecomp

## Goal

Native Windows port of Skylanders: Spyro's Adventure (Xbox 360, `rom/default.xex`) via ReXGlue
static recompilation, built like SwapForceRecomp (`../../SwapForceRecomp/SwapForceRecomp`): the
same SDK, app, portal and menus. Later part of a Skylanders hub where every game stays its own exe.

## Rules

- Analyse first, then build. When unclear, ask instead of guessing.
- Small commits, Conventional Commits (`feat:`, `fix:`, `build:`, `docs:`).
- Never commit game data, secrets or tokens. Before EVERY commit check `git status`: no `rom/`
  content, `*.xex`, `*.iso` or figure dumps.
- No personal information about the owner in files, commit messages or docs.
- Every `config/default.toml` entry and every kernel stub gets a comment: what is at the address,
  how it was found, why the fix or stub is safe. Addresses from SwapForceRecomp or GiantsRecomp do
  not apply to this XEX.
- Do not modify submodules (`thirdparty/`) except on clearly named own branches. The portal code
  is shared with SwapForceRecomp (fork `tsapZ/skylanders-portal`, branch `feature/portal-menu`):
  changes there must keep working in both.
- If Spyro's Adventure differs technically from Swap Force in a significant way, report it and
  adapt the plan (known: other file formats, docs/game/spyros-adventure.md).
- Git identity is repo-local (`git config user.name/user.email`), never global.
- Scripted or experimental game runs use a copied `--user_data_root`, never the user's real save
  folder.

## Build

Always from the **x64 Native Tools Command Prompt for VS 2022**.

    just submodules        # git submodule update --init --recursive
    just build-release     # configures on first run, then builds
    just regen-release     # rerun codegen from rom/default.xex, reconfigure, rebuild
    just test-release
    just play-release

## Status convention

`docs/STATUS.md` is updated at the end of every session; `docs/roadmap.md` when the plan changes,
and the README's "How far it is" with it.
