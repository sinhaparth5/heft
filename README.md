# pkgtop

`top` for your package managers. One keyboard-driven terminal view of everything installed across apt, pacman, Homebrew, cargo, go, uv/pip and npm/pnpm, with the physical disk usage of each package and cache, when you last used it, and which binary on your `$PATH` actually wins.

pkgtop is read-only. It never runs a package manager just to look. When you uninstall something, it hands over to that ecosystem's own command.

> Status: early. See [ROADMAP.md](ROADMAP.md).

## Build

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/pkgtop --version
```

Requires a C++23 compiler (GCC 14+ / Clang 18+) and CMake 3.28+.

## Docs

- [design/tui.md](design/tui.md): what the interface looks like and how it behaves
- [design/architecture.md](design/architecture.md): how it's built
- [CHANGELOG.md](CHANGELOG.md)

## License

MIT
