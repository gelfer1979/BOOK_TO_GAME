#!/bin/bash
# =============================================================================
# build_appimage.sh  —  Build a portable AppImage for BOOK_TO_GAME
# Usage: bash platforms/linux/build_appimage.sh [build_dir]
#
# Requirements (auto-downloaded if missing):
#   - linuxdeploy-x86_64.AppImage
#   - linuxdeploy-plugin-appimage-x86_64.AppImage  (provides appimagetool)
#
# Output:
#   BOOK_TO_GAME-x86_64.AppImage   (in project root)
# =============================================================================

set -e

# ── paths ──────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${1:-$PROJECT_ROOT/build_linux}"
APPDIR="$PROJECT_ROOT/build_appimage/AppDir"
TOOLS_DIR="$PROJECT_ROOT/build_appimage/tools"

# ── sanity checks ──────────────────────────────────────────────────────────
if [ ! -f "$BUILD_DIR/BOOK_TO_GAME" ]; then
    echo ""
    echo "  ❌  ERROR: '$BUILD_DIR/BOOK_TO_GAME' not found."
    echo "      Run 'Build Linux (Release)' task first, then retry."
    echo ""
    exit 1
fi

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "   Building AppImage for BOOK_TO_GAME"
echo "   Build dir : $BUILD_DIR"
echo "   AppDir    : $APPDIR"
echo "═══════════════════════════════════════════════════════════"
echo ""

# ── prepare directories ────────────────────────────────────────────────────
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$TOOLS_DIR"

# ── copy main executable ───────────────────────────────────────────────────
echo "[1/6] Copying binaries..."
cp "$BUILD_DIR/BOOK_TO_GAME" "$APPDIR/usr/bin/BOOK_TO_GAME"
chmod +x "$APPDIR/usr/bin/BOOK_TO_GAME"

# Copy PuterBridge if it exists (optional)
if [ -f "$BUILD_DIR/PuterBridge" ]; then
    cp "$BUILD_DIR/PuterBridge" "$APPDIR/usr/bin/PuterBridge"
    chmod +x "$APPDIR/usr/bin/PuterBridge"
    echo "       ✓ PuterBridge included"
fi

# Copy NativeWebViewWrapper if it exists (optional)
if [ -f "$BUILD_DIR/NativeWebViewWrapper" ]; then
    cp "$BUILD_DIR/NativeWebViewWrapper" "$APPDIR/usr/bin/NativeWebViewWrapper"
    chmod +x "$APPDIR/usr/bin/NativeWebViewWrapper"
    echo "       ✓ NativeWebViewWrapper included"
fi

# ── copy game assets ───────────────────────────────────────────────────────
echo "[2/6] Copying assets and config files..."
if [ -d "$BUILD_DIR/assets" ]; then
    cp -r "$BUILD_DIR/assets" "$APPDIR/usr/bin/assets"
    echo "       ✓ assets/ directory"
fi

for f in book.json settings.json options.json save.json; do
    if [ -f "$BUILD_DIR/$f" ]; then
        cp "$BUILD_DIR/$f" "$APPDIR/usr/bin/$f"
        echo "       ✓ $f"
    fi
done

# Copy all ai_*.json config files
for f in "$BUILD_DIR"/ai_*.json; do
    if [ -f "$f" ]; then
        cp "$f" "$APPDIR/usr/bin/$(basename "$f")"
        echo "       ✓ $(basename "$f")"
    fi
done

# Copy all nbook_*.txt files
for f in "$BUILD_DIR"/nbook_*.txt; do
    if [ -f "$f" ]; then
        cp "$f" "$APPDIR/usr/bin/$(basename "$f")"
        echo "       ✓ $(basename "$f")"
    fi
done

# ── create AppRun entry point ──────────────────────────────────────────────
echo "[3/6] Creating AppRun entry point..."
cat > "$APPDIR/AppRun" << 'APPRUN_EOF'
#!/bin/bash
# AppRun — entry point for the AppImage
APPDIR="$(dirname "$(readlink -f "$0")")"

# Directory where the .AppImage file lives (used to persist mutable data)
APPIMAGE_DIR="$(cd "$(dirname "${APPIMAGE:-$0}")" && pwd)"

# ── writable working directory ─────────────────────────────────────────────
# The AppImage squashfs is read-only, so we cannot write puter_request.json,
# puter_response.json, save.json etc. directly into $APPDIR/usr/bin.
# Replicate the same approach as BookToGame.run: extract to a tmp dir.
WORK_DIR=$(mktemp -d /tmp/booktogame.XXXXXX)

# Symlink the read-only assets directory (no need to copy)
ln -s "$APPDIR/usr/bin/assets" "$WORK_DIR/assets"

# Mutable config files: prefer a copy in $APPIMAGE_DIR (persisted from last run),
# otherwise seed from the bundled defaults inside the AppImage.
MUTABLE_FILES=(book.json settings.json options.json save.json)
for f in "${MUTABLE_FILES[@]}"; do
    if [ -f "$APPIMAGE_DIR/$f" ]; then
        cp "$APPIMAGE_DIR/$f" "$WORK_DIR/$f"
    elif [ -f "$APPDIR/usr/bin/$f" ]; then
        cp "$APPDIR/usr/bin/$f" "$WORK_DIR/$f"
    fi
done

# AI config files (ai_*.json): bundled first, then user overrides from $APPIMAGE_DIR
for f in "$APPDIR/usr/bin"/ai_*.json; do
    [ -f "$f" ] && cp "$f" "$WORK_DIR/$(basename "$f")"
done
for f in "$APPIMAGE_DIR"/ai_*.json; do
    [ -f "$f" ] && cp "$f" "$WORK_DIR/$(basename "$f")"
done

# Book files (nbook_*.txt): symlink from both bundled and $APPIMAGE_DIR
for f in "$APPDIR/usr/bin"/nbook_*.txt; do
    [ -f "$f" ] && ln -s "$f" "$WORK_DIR/$(basename "$f")" 2>/dev/null
done
for f in "$APPIMAGE_DIR"/nbook_*.txt; do
    [ -f "$f" ] && [ ! -e "$WORK_DIR/$(basename "$f")" ] && ln -s "$f" "$WORK_DIR/$(basename "$f")"
done

# PuterBridge / NativeWebViewWrapper: symlink so they are "in" the working dir
for bin in PuterBridge NativeWebViewWrapper; do
    [ -f "$APPDIR/usr/bin/$bin" ] && ln -s "$APPDIR/usr/bin/$bin" "$WORK_DIR/$bin"
done

cd "$WORK_DIR"

# ── library path for child processes (PuterBridge needs GTK/WebKit2) ───────
export APPDIR
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH}"

# ── display server auto-detection ──────────────────────────────────────────
if [ -n "$WAYLAND_DISPLAY" ] && [ -z "$SDL_VIDEODRIVER" ]; then
    export SDL_VIDEODRIVER=wayland
fi

# ── run the game ───────────────────────────────────────────────────────────
"$APPDIR/usr/bin/BOOK_TO_GAME" "$@"
STATUS=$?

# ── persist mutable files back next to the .AppImage ──────────────────────
for f in book.json settings.json options.json save.json; do
    if [ -f "$WORK_DIR/$f" ] && [ ! -L "$WORK_DIR/$f" ]; then
        cp "$WORK_DIR/$f" "$APPIMAGE_DIR/$f"
    fi
done
for f in "$WORK_DIR"/ai_*.json; do
    if [ -f "$f" ] && [ ! -L "$f" ]; then
        cp "$f" "$APPIMAGE_DIR/$(basename "$f")"
    fi
done

# ── cleanup ────────────────────────────────────────────────────────────────
rm -rf "$WORK_DIR"
exit $STATUS
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

# ── create .desktop file ───────────────────────────────────────────────────
echo "[4/6] Creating .desktop file..."
cat > "$APPDIR/usr/share/applications/BOOK_TO_GAME.desktop" << 'DESKTOP_EOF'
[Desktop Entry]
Name=Book to Game
Comment=An interactive book game powered by AI
Exec=BOOK_TO_GAME
Icon=booktogame
Type=Application
Categories=Game;
Terminal=false
DESKTOP_EOF

# Also place the .desktop at the AppDir root (required by AppImage spec)
cp "$APPDIR/usr/share/applications/BOOK_TO_GAME.desktop" "$APPDIR/BOOK_TO_GAME.desktop"

# ── create / copy icon ─────────────────────────────────────────────────────
echo "[5/6] Setting up application icon..."
# itch_banner_small.png is already 256×256 — valid for linuxdeploy
ICON_SRC="$PROJECT_ROOT/itch_banner_small.png"
ICON_DEST_DIR="$APPDIR/usr/share/icons/hicolor/256x256/apps"
ICON_DEST="$ICON_DEST_DIR/booktogame.png"

if [ -f "$ICON_SRC" ]; then
    cp "$ICON_SRC" "$ICON_DEST"
    cp "$ICON_SRC" "$APPDIR/booktogame.png"
    echo "       ✓ Icon: itch_banner_small.png (256×256)"
else
    echo "  ❌  ERROR: '$ICON_SRC' not found. Cannot build AppImage without an icon."
    exit 1
fi

# ── download linuxdeploy if needed ─────────────────────────────────────────
echo "[6/6] Downloading / verifying linuxdeploy tools..."

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_APPIMAGE_PLUGIN="$TOOLS_DIR/linuxdeploy-plugin-appimage-x86_64.AppImage"

download_tool() {
    local url="$1"
    local dest="$2"
    local name="$(basename "$dest")"

    if [ ! -f "$dest" ]; then
        echo "       Downloading $name ..."
        if command -v wget &>/dev/null; then
            wget -q --show-progress -O "$dest" "$url"
        elif command -v curl &>/dev/null; then
            curl -L --progress-bar -o "$dest" "$url"
        else
            echo "  ❌  ERROR: wget or curl is required to download tools."
            exit 1
        fi
        chmod +x "$dest"
        echo "       ✓ $name downloaded"
    else
        echo "       ✓ $name already present"
    fi
}

download_tool \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    "$LINUXDEPLOY"

download_tool \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage" \
    "$LINUXDEPLOY_APPIMAGE_PLUGIN"

# ── run linuxdeploy ────────────────────────────────────────────────────────
echo ""
echo "Deploying shared libraries with linuxdeploy..."
echo "(This bundles all required .so files into the AppImage)"
echo ""

OUTPUT_DIR="$PROJECT_ROOT"

# Build --executable list: always include main binary, conditionally add PuterBridge / NativeWebViewWrapper
EXECUTABLE_ARGS=("--executable" "$APPDIR/usr/bin/BOOK_TO_GAME")
if [ -f "$APPDIR/usr/bin/PuterBridge" ]; then
    EXECUTABLE_ARGS+=("--executable" "$APPDIR/usr/bin/PuterBridge")
    echo "       ✓ PuterBridge added to linuxdeploy (GTK/WebKit2 deps will be bundled)"
fi
if [ -f "$APPDIR/usr/bin/NativeWebViewWrapper" ]; then
    EXECUTABLE_ARGS+=("--executable" "$APPDIR/usr/bin/NativeWebViewWrapper")
    echo "       ✓ NativeWebViewWrapper added to linuxdeploy"
fi

ARCH=x86_64 \
OUTPUT="$OUTPUT_DIR/BOOK_TO_GAME-x86_64.AppImage" \
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    "${EXECUTABLE_ARGS[@]}" \
    --desktop-file "$APPDIR/BOOK_TO_GAME.desktop" \
    --icon-file "$APPDIR/booktogame.png" \
    --output appimage

# ── result ─────────────────────────────────────────────────────────────────
APPIMAGE_PATH="$OUTPUT_DIR/BOOK_TO_GAME-x86_64.AppImage"

if [ -f "$APPIMAGE_PATH" ]; then
    SIZE_MB=$(du -sh "$APPIMAGE_PATH" | cut -f1)
    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║  ✅  AppImage built successfully!                         ║"
    echo "╠═══════════════════════════════════════════════════════════╣"
    printf "║  📦 File : %-48s║\n" "$(basename "$APPIMAGE_PATH")"
    printf "║  📏 Size : %-48s║\n" "$SIZE_MB"
    printf "║  📂 Path : %-48s║\n" "$APPIMAGE_PATH"
    echo "╠═══════════════════════════════════════════════════════════╣"
    echo "║  Run with:  ./BOOK_TO_GAME-x86_64.AppImage               ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo ""
else
    echo ""
    echo "  ❌  ERROR: AppImage was not created. Check linuxdeploy output above."
    exit 1
fi
