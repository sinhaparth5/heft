# Changelog

## [Unreleased]
### Added
- Update checks for apt and pacman, read from the local package indexes (`apt update` / `pacman -Sy` keep them fresh; no network), and for Homebrew via formulae.brew.sh. apt and pacman checks also work in builds without libcurl.
- `--ascii` draws the TUI with ASCII only (borders, gauges, spinner, status glyphs), for terminals without box-drawing characters.
- Mouse: click a tab, row or ecosystem; the wheel scrolls.
- `e` opens an ecosystem picker (j/k to choose, Enter to keep, Esc to undo) instead of cycling.
- `Tab` moves focus between the table and the Ecosystems pane (it no longer cycles tabs; use 1-4).
- Sizes are remembered between runs (`$XDG_CACHE_HOME/pkgtop/sizes`), so the TUI paints them at once. A package is only re-measured when its version, root or root's mtime changes. Caches show the last size until a fresh scan replaces it.

### Changed
- Rows on screen are sized first, so they no longer wait behind a huge cache or store.

### Fixed
- Versions are now compared properly (epochs, numeric parts, pre-releases), so an installed version newer than the registry's latest no longer shows as outdated.

## [2026.11.3] - 2026-11-21
### Changed
- Re-running `install.sh` upgrades in place: it replaces the `pkgtop` already on `PATH` atomically (safe while it is running) and prints `Updated old -> new`.
