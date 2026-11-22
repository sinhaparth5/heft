# Changelog

## [Unreleased]
### Added
- Update checks for apt and pacman, read from the local package indexes (`apt update` / `pacman -Sy` keep them fresh; no network), and for Homebrew via formulae.brew.sh. apt and pacman checks also work in builds without libcurl.

### Fixed
- Versions are now compared properly (epochs, numeric parts, pre-releases), so an installed version newer than the registry's latest no longer shows as outdated.

## [2026.11.3] - 2026-11-21
### Changed
- Re-running `install.sh` upgrades in place: it replaces the `pkgtop` already on `PATH` atomically (safe while it is running) and prints `Updated old -> new`.
