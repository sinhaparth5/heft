# Changelog

All notable changes to pkgtop are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [CalVer](https://calver.org/) `YYYY.MM.PATCH`. PATCH starts at 0 each month and goes up with each release in that month.

## [Unreleased]

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
