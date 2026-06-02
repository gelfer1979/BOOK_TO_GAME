#!/bin/bash
# build_and_run.sh - Called by the VSCode "Build Run file for Linux" task via WSL.
# Usage: bash platforms/linux/build_and_run.sh [build_dir]
# Only builds and packs the .run file; does NOT launch it.
# All logic lives here so no special characters need to survive Windows shell quoting.

BUILD_DIR="${1:-build_linux}"

bash platforms/linux/pack_run.sh "$BUILD_DIR" || exit 1
cp "$BUILD_DIR/BookToGame.run" .
chmod +x BookToGame.run

echo ""
echo "> BookToGame.run is ready. Launch it manually when needed."
echo ""
