# Changelog

All notable changes to pkgtop are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [CalVer](https://calver.org/) `YYYY.MM.PATCH`. PATCH starts at 0 each month and goes up with each release in that month.

## [Unreleased]
### Added
- `pkgtop --list`: headless inventory of installed apt packages, read directly from `/var/lib/dpkg/status` (no subprocess), with Essential flag and scan time.
- cargo provider: packages from `$CARGO_HOME/.crates.toml` (default `~/.cargo`) with their binaries; `--list` gains a BINS column.

## [2026.11.0] - 2026-11-21
### Added
- CMake + C++23 project skeleton, `pkgtop --version`.
- `CLAUDE.md`, `ROADMAP.md`, `README.md`, MIT `LICENSE`.
- TUI design spec (`design/tui.md`) and architecture doc (`design/architecture.md`).
