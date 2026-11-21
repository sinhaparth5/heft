# pkgtop

`top` for your package managers. One keyboard-driven terminal view of everything installed across apt, pacman, Homebrew, cargo, go, uv/pip and npm/pnpm, with the physical disk usage of each package and cache, when you last used it, and which binary on your `$PATH` actually wins.

pkgtop is read-only. It never runs a package manager just to look. When you uninstall something, it hands over to that ecosystem's own command.

> Status: early. See [ROADMAP.md](ROADMAP.md).

## Install

Linux (x86_64 or aarch64, any distro; a single static binary):

```sh
curl -fsSL https://raw.githubusercontent.com/sinhaparth5/pkgtop/main/install.sh | sh
```

It installs to `~/.local/bin` after checking the sha256. Pick another place with `PKGTOP_BIN_DIR=/usr/local/bin`, or a specific release with `PKGTOP_VERSION=v2026.11.1`. You can also download the tarball from [Releases](https://github.com/sinhaparth5/pkgtop/releases) yourself.

- **Windows:** not supported natively. Run the same command inside WSL.
- **macOS:** not yet (on the roadmap). Until then, build from source below.

## Build

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/pkgtop            # TUI (u uninstalls, p purges a cache, always after a confirm)
./build/pkgtop --list --sizes
./build/pkgtop --caches
./build/pkgtop --path        # every $PATH binary: owner, shadowed/shim/broken flags
./build/pkgtop --stale       # packages by bloat score: size x days since last use
./build/pkgtop --outdated    # newer versions on crates.io, PyPI, npm, Go proxy (network, opt-in)
./build/pkgtop --updates     # TUI with the same checks in the background
```

Releasing: bump `project(VERSION)` in `CMakeLists.txt`, move `[Unreleased]` in `CHANGELOG.md` to a `[YYYY.MM.PATCH] - date` section, commit, then `git tag vYYYY.MM.PATCH && git push origin vYYYY.MM.PATCH`. The release workflow checks that tag, version and changelog agree, builds both architectures (`scripts/build-static.sh`, runnable locally with Docker) and publishes.

The TUI's dependency, [FTXUI](https://github.com/ArthurSonzogni/FTXUI), is fetched by CMake on first configure.

Reads apt, pacman, Homebrew, cargo, Go, uv tools, pipx, npm and pnpm straight from disk.

Requires a C++23 compiler (GCC 14+ / Clang 18+) and CMake 3.28+.

## Docs

- [design/tui.md](design/tui.md): what the interface looks like and how it behaves
- [design/architecture.md](design/architecture.md): how it's built
- [CHANGELOG.md](CHANGELOG.md)

## License

MIT
