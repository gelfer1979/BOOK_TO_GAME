#!/bin/bash
# pack_run.sh - Packs the Linux build folder into a single self-extracting executable

TARGET_DIR="$1"
if [ -z "$TARGET_DIR" ]; then
    TARGET_DIR="build_linux"
fi

if [ ! -d "$TARGET_DIR" ]; then
    echo "[Error] Target directory $TARGET_DIR does not exist."
    exit 1
fi

cd "$TARGET_DIR"

# Verify required files exist
if [ ! -f "BOOK_TO_GAME" ]; then
    echo "[Error] BOOK_TO_GAME binary not found in $TARGET_DIR. Build it first!"
    exit 1
fi

echo "[Packer] Packaging Linux targets from $(pwd) into BookToGame.run..."

# Create self-extracting header with absolute persistence for options.json, settings.json, save.json, book.json, and ai_*.json
cat << 'EOF' > header.sh
#!/bin/bash

# Determine the directory where the .run script is located
RUN_DIR="$(cd "$(dirname "$0")" && pwd)"

# Create a temporary directory for extraction
export TMPDIR=$(mktemp -d /tmp/booktogame.XXXXXX)

# Extract tarball payload to /tmp
ARCHIVE=$(awk '/^__ARCHIVE_BELOW__/ {print NR + 1; exit 0;}' "$0")
tail -n +$ARCHIVE "$0" | tar -xz -C "$TMPDIR"

cd "$TMPDIR"

# If RUN_DIR is writeable, set up persistent configurations and user data next to the .run executable
if [ -w "$RUN_DIR" ]; then
    # List of mutable config/data files we want to persist in RUN_DIR
    MUTABLE_FILES=("book.json" "settings.json" "options.json")
    
    # Add any extracted ai_*.json files
    for ai_file in ai_*.json; do
        if [ -f "$ai_file" ]; then
            MUTABLE_FILES+=("$ai_file")
        fi
    done
    
    # Create persistent files in RUN_DIR if they do not exist, and symlink them
    for f in "${MUTABLE_FILES[@]}"; do
        if [ -f "$f" ]; then
            if [ ! -f "$RUN_DIR/$f" ]; then
                cp "$f" "$RUN_DIR/$f"
            fi
            rm -f "$f"
            ln -s "$RUN_DIR/$f" "$f"
        fi
    done
    
    # Dynamically symlink any user-provided books (nbook_*.txt) next to the .run file so the game loads them
    for run_book in "$RUN_DIR"/nbook_*.txt; do
        if [ -f "$run_book" ]; then
            book_name=$(basename "$run_book")
            if [ ! -e "$book_name" ]; then
                ln -s "$run_book" "$book_name"
            fi
        fi
    done
    
    # Dynamically symlink any new user-provided AI JSON config files from RUN_DIR
    for run_ai in "$RUN_DIR"/ai_*.json; do
        if [ -f "$run_ai" ]; then
            ai_name=$(basename "$run_ai")
            if [ ! -e "$ai_name" ]; then
                ln -s "$run_ai" "$ai_name"
            fi
        fi
    done
fi

# Run the game
chmod +x BOOK_TO_GAME PuterBridge NativeWebViewWrapper 2>/dev/null || true
./BOOK_TO_GAME "$@"
status=$?

# ── repack .run with updated JSON files after game exits ──────────────────────
if [ -w "$RUN_DIR" ]; then
    RUN_FILE="$RUN_DIR/BookToGame.run"

    # Copy back any non-symlinked mutable files that may have been created/changed
    for f in book.json settings.json options.json; do
        if [ -f "$f" ] && [ ! -L "$f" ]; then
            cp "$f" "$RUN_DIR/$f"
        fi
    done
    for f in ai_*.json; do
        if [ -f "$f" ] && [ ! -L "$f" ]; then
            cp "$f" "$RUN_DIR/$f"
        fi
    done

    # Repack the .run file so bundled JSON defaults are updated
    if [ -f "$RUN_FILE" ] && command -v tar >/dev/null 2>&1; then
        echo "[BookToGame] Repacking $RUN_FILE with updated configs..."

        REPACK_TMP=$(mktemp -d /tmp/booktogame_repack.XXXXXX)

        # Extract header (everything up to and including the __ARCHIVE_BELOW__ line)
        ARCHIVE_LINE=$(awk '/^__ARCHIVE_BELOW__/ {print NR; exit 0;}' "$RUN_FILE")
        head -n "$ARCHIVE_LINE" "$RUN_FILE" > "$REPACK_TMP/header.sh"

        # Gather files to repack from TMPDIR (binaries + assets are still there)
        REPACK_FILES=()
        for bin in BOOK_TO_GAME PuterBridge NativeWebViewWrapper; do
            [ -f "$bin" ] && REPACK_FILES+=("$bin")
        done
        [ -d "assets" ] && REPACK_FILES+=("assets")

        # Include updated JSON files from RUN_DIR (source of truth after a run)
        for f in book.json settings.json options.json; do
            if [ -f "$RUN_DIR/$f" ]; then
                cp "$RUN_DIR/$f" "$f"
                REPACK_FILES+=("$f")
            fi
        done
        for f in "$RUN_DIR"/ai_*.json; do
            [ -f "$f" ] && cp "$f" "$(basename "$f")" && REPACK_FILES+=("$(basename "$f")")
        done
        # Include nbook_*.txt files (symlinks resolved)
        for f in nbook_*.txt; do
            [ -f "$f" ] && REPACK_FILES+=("$f")
        done

        # Build new payload and assemble the .run
        tar -czf "$REPACK_TMP/payload.tar.gz" "${REPACK_FILES[@]}" 2>/dev/null
        cat "$REPACK_TMP/header.sh" "$REPACK_TMP/payload.tar.gz" > "$REPACK_TMP/BookToGame.run"
        chmod +x "$REPACK_TMP/BookToGame.run"
        mv "$REPACK_TMP/BookToGame.run" "$RUN_FILE"
        rm -rf "$REPACK_TMP"

        echo "[BookToGame] Repack complete: $RUN_FILE"
    fi
fi

# Clean up /tmp directory
rm -rf "$TMPDIR"
exit $status
__ARCHIVE_BELOW__
EOF

# Determine what payload files to compress
FILES_TO_PACK=("BOOK_TO_GAME" "assets")
if [ -f "PuterBridge" ]; then
    FILES_TO_PACK+=("PuterBridge")
fi
if [ -f "NativeWebViewWrapper" ]; then
    FILES_TO_PACK+=("NativeWebViewWrapper")
fi
if [ -f "book.json" ]; then
    FILES_TO_PACK+=("book.json")
fi
if [ -f "options.json" ]; then
    FILES_TO_PACK+=("options.json")
fi
if [ -f "settings.json" ]; then
    FILES_TO_PACK+=("settings.json")
fi

# Package any ai_*.json files if they exist in the target directory
for ai_file in ai_*.json; do
    if [ -f "$ai_file" ]; then
        FILES_TO_PACK+=("$ai_file")
    fi
done

# Package any nbook_*.txt files if they exist in the target directory
for nbook_file in nbook_*.txt; do
    if [ -f "$nbook_file" ]; then
        FILES_TO_PACK+=("$nbook_file")
    fi
done

# Ensure payload binaries are executable before archiving
chmod +x BOOK_TO_GAME PuterBridge NativeWebViewWrapper 2>/dev/null || true

# Pack files
tar -czf payload.tar.gz "${FILES_TO_PACK[@]}"

# Concatenate header and payload
cat header.sh payload.tar.gz > BookToGame.run
chmod +x BookToGame.run

# Clean up temporary pack files
rm header.sh payload.tar.gz

echo "====================================================="
echo "SUCCESS: Packed single-file Linux build:"
echo "  $(pwd)/BookToGame.run"
echo "====================================================="
