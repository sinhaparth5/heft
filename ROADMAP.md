# pkgtop roadmap

Each milestone ships something runnable. Do them in order and don't start one before the previous works end to end.

## M0: Skeleton ✅
- [x] CMake + C++23 + Ninja build, `pkgtop --version`
- [x] CalVer, CHANGELOG, LICENSE (MIT), README
- [x] TUI design spec + architecture doc (`design/`)

## M1: Headless inventory (`pkgtop --list`) ✅
- [x] `Package { ecosystem, name, version, root, bins }` and `Cache { ecosystem, path }`
- [x] dpkg provider: mmap `/var/lib/dpkg/status`, parse stanzas, honor `Essential: yes`
- [x] cargo provider: `~/.cargo/.crates.toml` (same data as `.crates2.json`, one line per install)
- [x] Plain table output to stdout, with a startup-time measurement (target < 50 ms)
- [x] Fixture-based parser tests

## M2: Physical disk accounting ✅
- [x] `openat`/`getdents64`/`fstatat` walker: `st_blocks`, inode dedupe, no symlink follow, no `st_dev` crossing, skipped-entry counter
- [x] Sizes for packages and caches (`~/.cargo/registry`, `~/.cache/pip`, `~/.cache/uv`, `~/.npm`, pnpm store, apt archives)
- [x] Check against `du -s --block-size=1` on fixtures with hardlinks and sparse files

## M3: TUI
- [x] Layout per `design/tui.md`: top bar + tabs, Ecosystems / Packages / Details panes, keys panel, status line
- [x] Palette tokens with truecolor → 256 fallback, `NO_COLOR`
- [ ] `--ascii` borders and glyphs (FTXUI has no ASCII border style; needs a custom border)
- [x] FTXUI table: sort by size/name/ecosystem, fuzzy filter, detail pane
- [x] Responsive collapse at <120 / <100 / <80 cols
- [x] Render the inventory immediately; worker pool streams sizes in; UI redraws without blocking input
- [ ] Later polish: mouse, `e` popup (currently cycles), pane focus on `Tab`, visible-rows-first size priority

## M4: More providers
- [ ] pacman (`/var/lib/pacman/local/*/desc`)
- [ ] Homebrew (Cellar `INSTALL_RECEIPT.json`)
- [ ] uv tools (`uv-receipt.toml`), pipx
- [ ] npm/pnpm globals (`package.json` under the global prefix)
- [ ] Go (`.go.buildinfo` section in `~/go/bin/*`)
- [ ] rpm (later: the db is SQLite, so this needs a dependency or a minimal reader)

## M5: `$PATH` graph
- [ ] Ordered `$PATH` index, symlink-chain resolution, ownership by provider root prefix
- [ ] Flags: shadowed, shim (pyenv/rbenv/asdf/mise/nvm), dangling link
- [ ] "Which binary wins" view

## M6: Usage and staleness
- [ ] Reverse scan of zsh/bash/fish history to get the last invocation per binary
- [ ] Combine with atime/mtime and ecosystem activity → `Last Active` plus a confidence level
- [ ] Bloat score: size vs. time since last use

## M7: Delegated mutation
- [ ] Uninstall/purge command per provider
- [ ] Confirmation modal (exact command + space reclaimed); essential packages blocked
- [ ] Suspend the TUI, run the child in the foreground, resume, rescan only the affected provider

## M8: Optional enrichment
- [ ] Remote "update available" checks (off by default, low priority, never blocks)
- [ ] macOS support (APFS, Homebrew paths)

## Non-goals
- Installing packages
- Deleting files directly, or editing any package manager's database
- Windows
