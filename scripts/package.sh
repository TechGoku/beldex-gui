#!/usr/bin/env bash
# Builds release packages of beldex-gui for the OS it runs on.
#
#   QT_DIR=~/Qt/6.8.3/gcc_64 ./scripts/package.sh          # Linux:   AppImage, .deb, .rpm*, .tar.gz
#   QT_DIR=~/Qt/6.8.3/macos  ./scripts/package.sh          # macOS:   .dmg
#   BELDEX_TARGET=win64 QT_DIR=~/Qt/6.8.3/mingw_64 QT_HOST_PATH=~/Qt/6.8.3/gcc_64 ./scripts/package.sh
#                                                           # Windows x64 from Linux: .exe installer, .zip
#
#   * .rpm only when rpmbuild is installed.
# Beldex core must be built first (scripts/build-beldex-core.sh); the local node (beldexd) is bundled.
# Output goes to dist/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-release}"
DIST="$ROOT/dist"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"
APPIMAGETOOL_VERSION="1.9.1"

TARGET="${BELDEX_TARGET:-native}"
case "$(uname -s)" in
    Linux*) OS=linux ;;
    Darwin*) OS=macos ;;
    MINGW*|MSYS*|CYGWIN*) OS=windows ;;
    *) echo "unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac
ARCH="$(uname -m)"; case "$ARCH" in arm64|aarch64) ARCH=arm64 ;; *) ARCH=x86_64 ;; esac
CROSS_ARGS=()
if [ "$TARGET" = win64 ]; then
    # Cross-compile for Windows: core's MinGW toolchain, Windows Qt for the target, Linux Qt for host tools.
    OS=windows; ARCH=x86_64
    BUILD="${BUILD_DIR:-$ROOT/build-win64}"
    : "${QT_HOST_PATH:?set QT_HOST_PATH to a Linux Qt (e.g. ~/Qt/6.8.3/gcc_64)}"
    CROSS_ARGS=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/external/beldex/cmake/64-bit-toolchain.cmake"
                -DQT_HOST_PATH="$QT_HOST_PATH"
                -DBELDEX_BUILD_DIR="$ROOT/external/beldex/build/win64")
fi

PREFIX_ARGS=()
[ -n "${QT_DIR:-}" ] && PREFIX_ARGS=(-DCMAKE_PREFIX_PATH="$QT_DIR")
GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)

echo "==> Configuring ($OS/$ARCH) in $BUILD"
# ${a[@]+"${a[@]}"}: empty arrays are fine under set -u even on macOS's bash 3.2
cmake -S "$ROOT" -B "$BUILD" ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release ${PREFIX_ARGS[@]+"${PREFIX_ARGS[@]}"} \
    ${CROSS_ARGS[@]+"${CROSS_ARGS[@]}"} ${BELDEX_GUI_CMAKE_ARGS:-}
echo "==> Building"
cmake --build "$BUILD" -j "$JOBS"
mkdir -p "$DIST"

pack() {  # cpack generator(s)
    (cd "$BUILD" && cpack -G "$1" -B "$BUILD/cpack")
    find "$BUILD/cpack" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.rpm' -o -name '*.tar.gz' -o -name '*.dmg' \
        -o -name '*.exe' -o -name '*.zip' \) -exec mv -f {} "$DIST/" \;
}

case "$OS" in
linux)
    pack DEB
    pack TGZ
    if command -v rpmbuild >/dev/null; then pack RPM; else echo "==> rpmbuild not installed; skipping .rpm"; fi

    echo "==> AppImage"
    APPDIR="$BUILD/AppDir"
    rm -rf "$APPDIR"
    cmake --install "$BUILD" --prefix "$APPDIR/usr"
    ln -sf usr/bin/beldex-wallet-gui "$APPDIR/AppRun"
    cp "$ROOT/packaging/linux/io.beldex.WalletGui.desktop" "$APPDIR/"
    cp "$ROOT/packaging/icons/beldex-wallet-256.png" "$APPDIR/io.beldex.WalletGui.png"
    ln -sf io.beldex.WalletGui.png "$APPDIR/.DirIcon"
    TOOL_ARCH=$([ "$ARCH" = arm64 ] && echo aarch64 || echo x86_64)
    TOOL="$BUILD/appimagetool-$APPIMAGETOOL_VERSION-$TOOL_ARCH.AppImage"
    if [ ! -x "$TOOL" ]; then
        curl -fL -o "$TOOL" "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-$TOOL_ARCH.AppImage"
        chmod +x "$TOOL"
    fi
    VERSION="$(sed -n 's/^project(beldex-gui VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
    OUT="$DIST/beldex-wallet-$VERSION-linux-$ARCH.AppImage"
    # --appimage-extract-and-run: works where FUSE isn't available (containers, CI)
    ARCH=$TOOL_ARCH "$TOOL" --appimage-extract-and-run --no-appstream "$APPDIR" "$OUT"
    ;;
macos)
    # hdiutil on GitHub's macOS runners sometimes fails with "Resource busy" while a system service still holds
    # the new image; it succeeds on a retry.
    for attempt in 1 2 3; do
        if pack DragNDrop; then break; fi
        [ "$attempt" = 3 ] && { echo "==> .dmg creation failed 3 times" >&2; exit 1; }
        echo "==> .dmg creation failed (attempt $attempt); retrying in 20 s"
        sleep 20
    done
    ;;
windows)
    command -v makensis >/dev/null && pack NSIS || echo "==> makensis not found; skipping installer"
    pack ZIP
    ;;
esac

echo "==> Packages in $DIST:"
ls -lh "$DIST"
