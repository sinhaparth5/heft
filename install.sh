#!/bin/sh
# Install the latest heft release (static Linux binary, x86_64 or aarch64):
#   curl -fsSL https://raw.githubusercontent.com/sinhaparth5/heft/main/install.sh | sh
# Options (environment):
#   HEFT_VERSION=v2026.11.1   a specific release instead of the latest
#   HEFT_BIN_DIR=/usr/local/bin   where to put it (default ~/.local/bin, /usr/local/bin as root)
set -eu
# Renamed from pkgtop: the old variable names still work.
HEFT_VERSION="${HEFT_VERSION:-${PKGTOP_VERSION:-}}"
HEFT_BIN_DIR="${HEFT_BIN_DIR:-${PKGTOP_BIN_DIR:-}}"

repo=sinhaparth5/heft
die() { echo "heft install: $*" >&2; exit 1; }

case "$(uname -s)" in
Linux) ;;
Darwin) die "macOS isn't supported yet (https://github.com/$repo/issues/1). Build from source: https://github.com/$repo#build" ;;
MINGW* | MSYS* | CYGWIN*) die "Windows isn't supported. Inside WSL, run this same command." ;;
*) die "only Linux is supported, not $(uname -s)" ;;
esac
case "$(uname -m)" in
x86_64 | amd64) arch=x86_64 ;;
aarch64 | arm64) arch=aarch64 ;;
*) die "no prebuilt binary for $(uname -m); build from source: https://github.com/$repo#build" ;;
esac

asset="heft-linux-$arch"
if [ -n "${HEFT_VERSION:-}" ]; then
    base="https://github.com/$repo/releases/download/$HEFT_VERSION"
else
    base="https://github.com/$repo/releases/latest/download"
fi
base="${HEFT_BASE_URL:-$base}" # mirrors, or file:// for testing

if command -v curl >/dev/null 2>&1; then
    fetch() { curl -fsSL "$1" -o "$2"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget -qO "$2" "$1"; }
else
    die "needs curl or wget"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "Downloading $asset.tar.gz..."
fetch "$base/$asset.tar.gz" "$tmp/$asset.tar.gz" || die "download failed: $base/$asset.tar.gz"
fetch "$base/$asset.tar.gz.sha256" "$tmp/$asset.tar.gz.sha256" || die "checksum download failed"
(cd "$tmp" && sha256sum -c --quiet "$asset.tar.gz.sha256") || die "checksum mismatch, not installing"
tar -xzf "$tmp/$asset.tar.gz" -C "$tmp"

default_dir="$HOME/.local/bin"
[ "$(id -u)" = 0 ] && default_dir=/usr/local/bin
# Upgrade: replace the copy already on PATH instead of adding a second one.
old=$(command -v heft 2>/dev/null || command -v pkgtop 2>/dev/null || true)
[ -n "$old" ] && [ -w "$(dirname "$old")" ] && default_dir=$(dirname "$old")
bin_dir="${HEFT_BIN_DIR:-$default_dir}"
was=$("$bin_dir/heft" --version 2>/dev/null || "$bin_dir/pkgtop" --version 2>/dev/null || true)
mkdir -p "$bin_dir"
# Copy next to it, then rename: atomic, and a running heft keeps its old file.
install -m 755 "$tmp/$asset/heft" "$bin_dir/.heft.new" && mv -f "$bin_dir/.heft.new" "$bin_dir/heft" ||
    die "can't write to $bin_dir (try HEFT_BIN_DIR=... or sudo)"
now=$("$bin_dir/heft" --version)
if [ -n "$was" ] && [ "$was" != "$now" ]; then
    echo "Updated $was -> $now in $bin_dir/heft"
else
    echo "Installed $now to $bin_dir/heft"
fi
# pkgtop was renamed to heft: replace an old binary with a link so `pkgtop` keeps working.
if [ -f "$bin_dir/pkgtop" ] && [ ! -L "$bin_dir/pkgtop" ]; then
    ln -sf heft "$bin_dir/pkgtop" && echo "pkgtop is now heft: $bin_dir/pkgtop links to it (safe to delete)"
fi
case ":$PATH:" in
*":$bin_dir:"*) ;;
*) echo "Note: $bin_dir is not on your PATH. Add it, e.g.: export PATH=\"$bin_dir:\$PATH\"" ;;
esac
