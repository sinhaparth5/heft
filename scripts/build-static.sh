#!/bin/sh
# Release build: one fully static binary (musl, libcurl included) that runs on any Linux.
# Used by CI and the release workflow; run it locally the same way:
#   docker run --rm -e HOST_UID=$(id -u) -e HOST_GID=$(id -g) -v "$PWD":/src -w /src \
#     alpine:3.23 sh scripts/build-static.sh
# Output: dist/heft-linux-<arch>.tar.gz and its .sha256. No version in the name, so
# install.sh can use GitHub's releases/latest/download/<name> URL.
set -eu
apk add --no-cache -q build-base cmake samurai git linux-headers coreutils pkgconf \
    curl-dev curl-static openssl-libs-static nghttp2-static zlib-static brotli-static \
    zstd-static libidn2-static libpsl-static libunistring-static c-ares-dev

cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DHEFT_STATIC=ON
cmake --build build-static
ctest --test-dir build-static --output-on-failure
strip build-static/heft

./build-static/heft --version
name="heft-linux-$(uname -m)"
rm -rf "dist/$name" && mkdir -p "dist/$name"
cp build-static/heft README.md LICENSE CHANGELOG.md "dist/$name/"
tar -C dist -czf "dist/$name.tar.gz" "$name"
(cd dist && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
# The container runs as root; hand the files back to whoever ran it.
chown -R "${HOST_UID:-0}:${HOST_GID:-0}" build-static dist
ls -l dist
