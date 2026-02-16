#!/bin/bash
set -e

DIST="distribution"
EXE="ruins-of-eternal-struggle-tiles.exe"

echo "=== Packaging $EXE for distribution ==="

# Clean and create
rm -rf "$DIST"
mkdir -p "$DIST/lang"

# Copy executable
cp "$EXE" "$DIST/"
echo "[OK] Executable copied"

# Copy required DLLs
ldd "$EXE" | grep ucrt64 | sed 's/.*=> //' | sed 's/ (.*//' | while IFS= read -r dll; do
    dll=$(echo "$dll" | tr -d '\r\n ')
    if [ -f "$dll" ]; then
        cp "$dll" "$DIST/"
    fi
done
echo "[OK] DLLs copied: $(ls "$DIST"/*.dll 2>/dev/null | wc -l) files"

# Copy game data
cp -r data "$DIST/"
echo "[OK] Game data copied"

# Copy tilesets
cp -r gfx "$DIST/"
echo "[OK] Tilesets copied"

# Copy translations
if [ -d "lang/mo" ]; then
    cp -r lang/mo "$DIST/lang/"
    echo "[OK] Translations copied"
else
    echo "[SKIP] No translations found"
fi

# Copy config if exists
if [ -d "config" ]; then
    cp -r config "$DIST/"
    echo "[OK] Config copied"
fi

echo ""
echo "=== Distribution package ready ==="
echo "Location: $DIST/"
echo "Total size: $(du -sh "$DIST" | cut -f1)"
echo "Files: $(find "$DIST" -type f | wc -l)"
echo "DLLs: $(ls "$DIST"/*.dll 2>/dev/null | wc -l)"
ls -la "$DIST"/*.exe "$DIST"/*.dll 2>/dev/null
