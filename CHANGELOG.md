# Changelog

## [Unreleased]
### Changed
- Re-running `install.sh` upgrades in place: it replaces the `pkgtop` already on `PATH` atomically (safe while it is running) and prints `Updated old -> new`.

## [2026.11.2] - 2026-11-21
### Changed
- `install.sh` installs to `/usr/local/bin` when run as root, so `pkgtop` is on `PATH` right away.
