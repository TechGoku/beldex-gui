#!/usr/bin/env bash
# Builds Beldex core's wallet library (libwallet_merged.a) for beldex-gui.
#
#   ./scripts/build-beldex-core.sh            # release, all cores
#   JOBS=4 ./scripts/build-beldex-core.sh
#
# BUILD_STATIC_DEPS downloads and statically builds Beldex's dependencies (boost, openssl,
# sodium, zmq, sqlite, curl, …) so the GUI binary has no runtime dependency on them. The
# first run takes a while; later runs are incremental.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${BELDEX_SOURCE_DIR:-$ROOT/external/beldex}"
BUILD="${BELDEX_BUILD_DIR:-$SRC/build/release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

if [ ! -f "$SRC/src/wallet/api/wallet2_api.h" ]; then
    echo "Beldex core not found at $SRC — run: git submodule update --init --recursive" >&2
    exit 1
fi

# Host tools the static dependency builds need (eudev needs gperf on Linux).
missing=()
for tool in cmake make git autoreconf libtoolize pkg-config gperf; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if [ "$(uname)" = "Linux" ] && [ ${#missing[@]} -gt 0 ]; then
    echo "Missing build tools: ${missing[*]}" >&2
    echo "Debian/Ubuntu: sudo apt install build-essential cmake git autoconf automake libtool pkg-config gperf" >&2
    exit 1
fi

# CMake caches absolute paths. If the checkout was moved, the old build dir cannot be reused;
# the GUI only needs the finished libraries, so keep them unless asked to rebuild.
CACHE="$BUILD/CMakeCache.txt"
if [ -f "$CACHE" ] && ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$SRC" "$CACHE"; then
    if [ -f "$BUILD/src/wallet/api/libwallet_merged.a" ] && [ "${FORCE_REBUILD:-0}" != 1 ]; then
        echo "==> $BUILD was configured in another location but libwallet_merged.a is already built; reusing it."
        echo "    (FORCE_REBUILD=1 $0 rebuilds Beldex core from scratch.)"
        exit 0
    fi
    echo "==> Build dir was configured elsewhere; starting a clean Beldex core build"
    rm -rf "$BUILD"
fi

echo "==> Initialising Beldex core submodules"
git -C "$SRC" submodule update --init --recursive --depth 1

# Local fixes to Beldex core that beldex-gui relies on (each one is also meant for upstream).
for p in "$ROOT"/scripts/patches/*.patch; do
    [ -e "$p" ] || continue
    if git -C "$SRC" apply --reverse --check "$p" 2>/dev/null; then
        echo "==> Patch already applied: $(basename "$p")"
    else
        echo "==> Applying patch: $(basename "$p")"
        git -C "$SRC" apply "$p"
    fi
done

# Beldex's static curl build auto-detects the host's zstd; programs linking it (beldexd) then need -lzstd.
EXTRA_LIBS_ARG=""
if [ "$(uname)" = "Linux" ] && { [ -e /usr/lib/x86_64-linux-gnu/libzstd.so ] || [ -e /usr/lib/libzstd.so ] || ldconfig -p 2>/dev/null | grep -q libzstd; }; then
    EXTRA_LIBS_ARG="-DCMAKE_CXX_STANDARD_LIBRARIES=-lzstd"
fi

echo "==> Configuring $BUILD"
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_STATIC_DEPS=ON \
    -DSTATIC=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_DOCUMENTATION=OFF \
    -DUSE_LTO=OFF \
    -DUSE_READLINE=OFF \
    -DWITH_SYSTEMD=OFF \
    ${EXTRA_LIBS_ARG:+"$EXTRA_LIBS_ARG"} \
    ${BELDEX_CMAKE_ARGS:-}

echo "==> Building wallet_merged and beldexd with $JOBS jobs"
cmake --build "$BUILD" --target wallet_merged -j "$JOBS"
cmake --build "$BUILD" --target daemon -j "$JOBS"   # local node the GUI can run (Settings › Node)

LIB="$(find "$BUILD" -name 'libwallet_merged.a' | head -n1)"
if [ -z "$LIB" ]; then
    echo "libwallet_merged.a was not produced" >&2
    exit 1
fi
echo "==> Done: $LIB"
echo "    Now: cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt 6.8>/gcc_64 && cmake --build build"
