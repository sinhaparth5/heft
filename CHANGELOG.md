# Changelog

All notable changes to pkgtop are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [CalVer](https://calver.org/) `YYYY.MM.PATCH`. PATCH starts at 0 each month and goes up with each release in that month.

## [Unreleased]
### Added
- Providers: pacman, Homebrew, uv tools, pipx, npm globals (including every nvm node), pnpm globals, and Go binaries (module version read from the `.go.buildinfo` ELF section). All read from disk; none run the tool.
- Interactive TUI (run `pkgtop` with no arguments): Packages and Caches tabs, Ecosystems pane with disk bars, Details pane, keys panel and status line in the gruvbox palette. Sizes stream in from background workers while the UI stays responsive. `j/k`, `g/G`, PgUp/PgDn, `/` fuzzy filter, `s`/`S` sort, `e` ecosystem filter, `1`–`4`/`Tab` tabs, `?` keys panel, `q` quit. Honors `NO_COLOR`; falls back to 256 colours without truecolor; collapses panes below 120 and 100 columns.
- `pkgtop --list`: headless inventory of installed apt packages, read directly from `/var/lib/dpkg/status` (no subprocess), with Essential flag and scan time.
- cargo provider: packages from `$CARGO_HOME/.crates.toml` (default `~/.cargo`) with their binaries; `--list` gains a BINS column.
- Physical disk accounting: `st_blocks`-based walker that counts hardlinks once, never follows symlinks, never crosses mount points, and counts unreadable entries as skipped (shown as `+`). Matches `du -sx -B1` byte-for-byte except for symlinks with long targets (counted as 0 to save a `stat` each).
- `pkgtop --list --sizes`: per-package physical size (apt via `/var/lib/dpkg/info/*.list`, cargo via its binaries), sorted largest first.
- `pkgtop --caches`: size and file count of known caches (apt, cargo, pip, uv, npm, pnpm, go, Homebrew), honoring `XDG_CACHE_HOME`, `CARGO_HOME`, `PIP_CACHE_DIR`, `UV_CACHE_DIR`, `PNPM_HOME`, `GOPATH`, `GOCACHE`.

## [2026.11.0] - 2026-11-21
### Added
- CMake + C++23 project skeleton, `pkgtop --version`.
- `CLAUDE.md`, `ROADMAP.md`, `README.md`, MIT `LICENSE`.
- TUI design spec (`design/tui.md`) and architecture doc (`design/architecture.md`).
