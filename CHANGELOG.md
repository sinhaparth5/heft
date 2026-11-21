# Changelog

## [Unreleased]
### Changed
- `install.sh` installs to `/usr/local/bin` when run as root, so `pkgtop` is on `PATH` right away.

## [2026.11.1] - 2026-11-21
### Added
- Static Linux release binaries (x86_64, aarch64) on GitHub Releases, and `install.sh` (`curl -fsSL https://raw.githubusercontent.com/sinhaparth5/pkgtop/main/install.sh | sh`).
- Opt-in update checks: `pkgtop --outdated` and `pkgtop --updates` (crates.io, PyPI, npm, Go proxy).
- Uninstall (`u`) and cache purge (`p`) through each ecosystem's own command, after a confirmation.
- Last-used tracking from shell history and atime; Stale tab and `pkgtop --stale`.
- `$PATH` graph with owners, shadowed binaries, shims and broken links; PATH tab and `pkgtop --path`.
- Providers: apt, pacman, Homebrew, cargo, uv, pipx, npm, pnpm, Go, all read from disk.
- Interactive TUI with physical disk sizes for packages and caches.
