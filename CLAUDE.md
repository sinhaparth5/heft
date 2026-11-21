# pkgtop

`top` for package managers: a C++23 TUI that shows every globally installed package, cache, and `$PATH` binary across ecosystems (apt, pacman, brew, cargo, go, uv/pip, npm/pnpm, version shims), with physical disk usage, last-used times, and shadowing conflicts.

Open work is tracked in GitHub issues.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/pkgtop
```

Toolchain: GCC 15 / CMake 4 / Ninja, Linux first. `compile_commands.json` is generated in `build/`.

## Invariants (do not break these)

1. **No subprocesses on read paths.** Discovery never runs `apt`, `npm`, `pip`, `brew`, etc. Parse on-disk state directly: `/var/lib/dpkg/status`, `/var/lib/pacman/local/*/desc`, `~/.cargo/.crates2.json`, `uv-receipt.toml`, `package.json`, the `.go.buildinfo` ELF section.
2. **Physical, not logical, disk usage.** Sum `st_blocks * 512`, dedupe by `(st_dev, st_ino)` within a scan, never follow symlinks (`AT_SYMLINK_NOFOLLOW`), never cross `st_dev` boundaries. Use `d_type` from `getdents` to skip `stat` where possible.
3. **Render first, hydrate later.** Names, versions, and paths must be on screen in tens of ms. Sizes, shell-history usage, and remote version checks stream in from background workers and never block input.
4. **Mutation is delegated.** pkgtop never deletes package files itself. Uninstall and purge run the ecosystem's own command in the foreground: restore the terminal, run the child, re-enter the TUI, and rescan only that provider.

## Safety rules

- `EACCES`/`EPERM` during a walk: count it as skipped and keep going. Never abort the scan.
- Never offer uninstall for `Essential: yes` / base-system packages.
- Always show a confirmation modal with the exact command before running anything that mutates.
- A broken symlink on `$PATH` is reported, never auto-deleted.

## Versioning & changelog

- CalVer `YYYY.MM.PATCH` (e.g. `2026.11.0`). PATCH resets to 0 each new month. The single source is `project(... VERSION ...)` in `CMakeLists.txt`, and tags are `vYYYY.MM.PATCH`.
- Every user-visible change adds a line under `## [Unreleased]` in `CHANGELOG.md`. `make tag VERSION=YYYY.MM.PATCH` turns it into the release.

## Conventions

- Format with `.clang-format` (LLVM base, 4-space, 100 cols).
- UI code uses the palette **tokens** defined at the top of `src/tui.cpp`, never raw hex inline.
- C++23, stdlib first (`std::print`, `std::expected`, `std::filesystem` only where it doesn't cost syscalls). POSIX calls (`openat`, `fstatat`, `getdents64`, `mmap`) on hot paths.
- One provider = one `.cpp` file. Add a shared abstraction only once a second provider actually needs it.
- Dependencies: add only when a feature needs them (FTXUI is fetched via CMake `FetchContent`; libcurl is optional).
- Tests: one small assert-based test per parser, fed by fixture files under `tests/fixtures/`. Never test against the host's real `/var/lib`.
