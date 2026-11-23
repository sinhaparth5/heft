# Changelog

## [Unreleased]
### Fixed
- On WSL, pkgtop no longer sits on a blank screen for ~50 s at startup ([#11](https://github.com/sinhaparth5/pkgtop/issues/11)). WSL adds the Windows `PATH` (`/mnt/c/...`), and checking each of its ~5k files over the 9P mount took several host round trips. Those directories are now listed from the directory entries alone, so the `PATH` scan dropped from 51 s to 0.23 s. Windows binaries still appear on the PATH tab.

## [2026.11.7] - 2026-11-22
### Fixed
- Scrolling with the mouse wheel or a touchpad no longer freezes the UI. Each wheel notch re-sorted the whole list and built ~50k formatted rows to do it (~11 ms at 2k packages), so a fast scroll queued up to a minute of work. Sorting now builds each row once, renders dropped from ~50 ms to ~4 ms, and 5000 wheel events drain instantly.

## [2026.11.6] - 2026-11-22
### Added
- `?` opens a popup with every key.

### Changed
- The Keys bar shows only the hints that fit the terminal width, starting with `? Keys`, and is always on (`?` used to hide it). At 80 columns the top bar drops the ecosystem count so all five tabs fit.

## [2026.11.5] - 2026-11-22
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

## [2026.11.3] - 2026-11-21
### Changed
- Re-running `install.sh` upgrades in place: it replaces the `pkgtop` already on `PATH` atomically (safe while it is running) and prints `Updated old -> new`.

## [2026.11.2] - 2026-11-21
### Changed
- `install.sh` installs to `/usr/local/bin` when run as root, so `pkgtop` is on `PATH` right away.

## [2026.11.1] - 2026-11-21
### Added
- Prebuilt releases: pushing a `vYYYY.MM.PATCH` tag builds fully static Linux binaries (x86_64 and aarch64, musl, libcurl included) and publishes them on GitHub Releases with sha256 checksums and the changelog section as notes. CI builds and tests both architectures on every push and pull request.
- `install.sh`: `curl -fsSL https://raw.githubusercontent.com/sinhaparth5/pkgtop/main/install.sh | sh` installs the latest release to `~/.local/bin` after checking its checksum (`PKGTOP_VERSION`, `PKGTOP_BIN_DIR` to override). On macOS and Windows it explains why not (Windows: use WSL).
- CMake option `PKGTOP_STATIC` for a fully static build; libcurl is now found through pkg-config.
- Opt-in update checks, the only network access pkgtop makes: `pkgtop --outdated` lists packages whose registry has a newer version, and `pkgtop --updates` opens the TUI with lookups running in the background (VERSION gets `↑`, Details shows Latest). Covers cargo (crates.io), uv and pipx (PyPI), npm and pnpm (npm registry) and Go binaries (proxy.golang.org, using the module path from the binary's build info). One request per package, 6 at a time, HTTPS only, 15 s timeout, cancelled on quit. Needs libcurl at build time; without it the flags explain that and exit.
- Delegated uninstall and cache purge: `u` on a package (Packages or Stale tab) or `p` on a cache opens a confirmation modal with the exact command and the space it frees; only `y` runs it. pkgtop leaves the screen, runs the ecosystem's own command on your terminal (no shell, so `sudo` prompts and Ctrl-C reach it), waits for Enter, then rescans only that provider and shows the exit status. Essential packages are refused, and so are Go binaries and cargo caches (no native command). Commands: `apt remove`, `pacman -R`, `brew uninstall`, `cargo uninstall`, `uv tool uninstall`, `pipx uninstall`, `npm uninstall -g --prefix <its prefix>`, `pnpm remove -g`; caches: `apt clean`, `pip cache purge`, `uv cache clean`, `npm cache clean --force`, `pnpm store prune`, `brew cleanup --prune=all`, `go clean -modcache` / `-cache`.
- Usage and staleness: last use per package from zsh, bash and fish history (timestamped or not) and from the binaries' atime after install, with a confidence level (`●` history, `◐` atime only, `◌` never seen or date unknown). apt and pacman binaries come from their manifests, other providers' from the `$PATH` graph. The Stale tab and `pkgtop --stale` rank packages with binaries by bloat score (size × days idle); Details shows Last used, Evidence and Bloat. Essential packages and libraries without binaries are left out.
- Reading files no longer bumps their atime when pkgtop owns them (`O_NOATIME`), so scanning Go binaries doesn't make them look used.
- `$PATH` graph: every executable on `$PATH` in lookup order, with its owning package (resolved through symlink chains), version manager (pyenv, rbenv, asdf, mise, nvm, volta) or `system`. Flags shadowed binaries (`⚠`), shims (`↯`) and broken links (`✗`, reported, never deleted). Directories that resolve to the same place (`/bin` → `/usr/bin`) count once. Shown in the TUI's PATH tab, where Details lists which copy wins, and as `pkgtop --path`.
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
