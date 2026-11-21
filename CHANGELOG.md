# Changelog

## [Unreleased]

## [2026.11.3] - 2026-11-21
### Changed
- Re-running `install.sh` upgrades in place: it replaces the `pkgtop` already on `PATH` atomically (safe while it is running) and prints `Updated old -> new`.
