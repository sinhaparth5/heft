# Changelog

## [Unreleased]
### Added
- Docker tab (5): containers, images, volumes and networks with their sizes, read from the Docker API socket. `u` removes the selected item and `t` starts or stops a container, each after a confirmation. Removing a running container or a built-in network (`bridge`, `host`, `none`) is refused; images are removed with all their tags.

## [2026.11.4] - 2026-11-22
### Added
- Update checks for apt and pacman, read from the local package indexes (`apt update` / `pacman -Sy` keep them fresh; no network), and for Homebrew via formulae.brew.sh. apt and pacman checks also work in builds without libcurl.
- `--ascii` draws the TUI with ASCII only (borders, gauges, spinner, status glyphs), for terminals without box-drawing characters.
- Mouse: click a tab, row or ecosystem; the wheel scrolls.
- `e` opens an ecosystem picker (j/k to choose, Enter to keep, Esc to undo) instead of cycling.
- `Tab` moves focus between the table and the Ecosystems pane (it no longer cycles tabs; use 1-4).
- Sizes are remembered between runs (`$XDG_CACHE_HOME/pkgtop/sizes`), so the TUI paints them at once. A package is only re-measured when its version, root or root's mtime changes. Caches show the last size until a fresh scan replaces it.
- PATH ownership: pnpm's global bins (wrapper scripts) are traced to their package, and binaries in system dirs show the apt/pacman package that installed them instead of `system` (filled in once manifests are read).
- LAST ACTIVE column on the Packages tab (when the terminal is wide enough).
- apt/pacman libraries (no binaries, installed as a dependency) get a last-used time from the packages that depend on them, so they show up on the Stale tab and in `--stale`. A library nothing depends on, or whose dependents are all unused, counts as unused since install.
- `u` on the PATH tab uninstalls the package that owns the binary.
- npm's cache index is used as weak evidence of use for npm/pnpm globals: a registry fetch after install (npx, a project install) counts as use when nothing better is known.

### Changed
- Rows on screen are sized first, so they no longer wait behind a huge cache or store.
- `u` and `p` no longer wait for loading to finish; sizing pauses while the command runs and picks up after.

### Fixed
- `u` on an older Homebrew version runs `brew cleanup <name>` instead of `brew uninstall <name>`, which removed the version in use rather than the selected one.
- Versions are now compared properly (epochs, numeric parts, pre-releases), so an installed version newer than the registry's latest no longer shows as outdated.
