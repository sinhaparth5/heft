# Changelog

All notable changes to pkgtop are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [CalVer](https://calver.org/) `YYYY.MM.PATCH`. PATCH starts at 0 each month and goes up with each release in that month.

## [Unreleased]
### Added
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
