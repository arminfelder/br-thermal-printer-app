#!/usr/bin/env bash
# Build the Brother thermal printer .deb packages and drop them into a target dir.
#
#   static  -> Dockerfile.static : built natively ON Debian Bookworm (under
#              qemu/binfmt for a foreign arch) with Clang + libc++; CUPS/PAPPL +
#              libc++ linked statically. Installable on Bookworm and newer.
#   dynamic -> Dockerfile        : Trixie cross build, linked against the distro
#              libpappl/libcups. For Trixie and newer only.
#
# Usage:
#   ./build-debs.sh [TARGET_DIR]      # default TARGET_DIR = ./dist
#
# Env overrides:
#   ARCH      target Debian architecture (default: arm64)
#   VARIANTS  space-separated subset of {static dynamic} (default: both)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Default output dir is 'debs/' (NOT 'dist/' — that is a source dir CMake reads).
OUT_DIR="${1:-$SCRIPT_DIR/debs}"
ARCH="${ARCH:-arm64}"
VARIANTS="${VARIANTS:-static dynamic}"

mkdir -p "$OUT_DIR"

# Map a Debian architecture to a Docker --platform value.
docker_platform() {
  case "$1" in
    arm64) echo "linux/arm64" ;;
    amd64) echo "linux/amd64" ;;
    armhf) echo "linux/arm/v7" ;;
    *)     echo "linux/$1" ;;
  esac
}

# Pull the staged .deb out of an image via a throwaway container.
extract_debs() {
  local tag="$1" cid
  cid="$(docker create "$tag")"
  docker cp "$cid:/output/." "$OUT_DIR/"
  docker rm "$cid" >/dev/null
}

build_static() {
  local platform tag
  platform="$(docker_platform "$ARCH")"
  tag="br-thermal-build:static-${ARCH}"

  # Native (emulated) build needs binfmt/qemu for a foreign target arch.
  if ! docker run --rm --platform "$platform" debian:bookworm true 2>/dev/null; then
    echo "!! cannot run $platform containers — register qemu/binfmt first:" >&2
    echo "     docker run --privileged --rm tonistiigi/binfmt --install all" >&2
    exit 1
  fi

  echo ">> building 'static' .deb (Bookworm/Clang/libc++, platform=$platform)"
  docker build \
    --platform "$platform" \
    -f "$SCRIPT_DIR/Dockerfile.static" \
    -t "$tag" \
    "$SCRIPT_DIR"
  extract_debs "$tag"
}

build_dynamic() {
  local tag="br-thermal-build:dynamic-${ARCH}"
  echo ">> building 'dynamic' .deb (Trixie cross, arch=$ARCH)"
  docker build \
    --build-arg ARCH="$ARCH" \
    --build-arg PROFILES="cross,nocheck" \
    --build-arg EXTRA_BUILD_DEPS="libpappl-dev:$ARCH libcups2-dev:$ARCH" \
    -f "$SCRIPT_DIR/Dockerfile" \
    -t "$tag" \
    "$SCRIPT_DIR"
  extract_debs "$tag"
}

for variant in $VARIANTS; do
  case "$variant" in
    static)  build_static ;;
    dynamic) build_dynamic ;;
    *) echo "unknown variant: $variant (expected: static|dynamic)" >&2; exit 1 ;;
  esac
done

echo ">> .deb packages in ${OUT_DIR}:"
ls -1 "$OUT_DIR"/*.deb

# Best-effort policy check (non-fatal). lintian is arch-independent, so a host
# lintian validates the arm64 .deb fine. Install 'lintian' to enable.
# For install/upgrade/purge testing of the user-creating maintainer scripts use
# piuparts (needs root/an arm64 chroot), e.g.:
#   sudo piuparts --arch arm64 -d bookworm "$OUT_DIR"/br-thermal-static_*.deb
if command -v lintian >/dev/null 2>&1; then
  echo ">> lintian (informational; 'static'/'libcxx' unknown-build-profile tags are expected):"
  lintian -- "$OUT_DIR"/*.deb || true
else
  echo ">> lintian not installed — skipping policy check (apt install lintian)"
fi
