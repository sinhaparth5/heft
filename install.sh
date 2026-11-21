#!/bin/sh
# Install the latest pkgtop release (static Linux binary, x86_64 or aarch64):
#   curl -fsSL https://raw.githubusercontent.com/sinhaparth5/pkgtop/main/install.sh | sh
# Options (environment):
#   PKGTOP_VERSION=v2026.11.1   a specific release instead of the latest
#   PKGTOP_BIN_DIR=/usr/local/bin   where to put it (default ~/.local/bin, /usr/local/bin as root)
set -eu

repo=sinhaparth5/pkgtop
die() { echo "pkgtop install: $*" >&2; exit 1; }

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

asset="pkgtop-linux-$arch"
if [ -n "${PKGTOP_VERSION:-}" ]; then
    base="https://github.com/$repo/releases/download/$PKGTOP_VERSION"
else
    base="https://github.com/$repo/releases/latest/download"
fi
base="${PKGTOP_BASE_URL:-$base}" # mirrors, or file:// for testing

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
bin_dir="${PKGTOP_BIN_DIR:-$default_dir}"
mkdir -p "$bin_dir"
install -m 755 "$tmp/$asset/pkgtop" "$bin_dir/pkgtop" ||
    die "can't write to $bin_dir (try PKGTOP_BIN_DIR=... or sudo)"
echo "Installed $("$bin_dir/pkgtop" --version) to $bin_dir/pkgtop"
case ":$PATH:" in
*":$bin_dir:"*) ;;
*) echo "Note: $bin_dir is not on your PATH. Add it, e.g.: export PATH=\"$bin_dir:\$PATH\"" ;;
esac
