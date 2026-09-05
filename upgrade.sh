#!/bin/bash
#
# upgrade.sh - bump the FTXUI dependency to a newer release.
#
#   ./upgrade.sh              upgrade to the latest FTXUI release on GitHub
#   ./upgrade.sh <tag>        pin a specific tag, e.g. ./upgrade.sh v5.0.0
#
# What it does:
#   - reads the current pin from CMakeLists.txt.in (the template build.sh uses)
#   - updates it to the chosen/latest tag
#   - clears the cached FetchContent checkouts so the new tag is actually used
#   - runs a configure pass to fetch the new tag and verify the local patch
#     (patches/ftxui-c0-delivery.patch) still applies; on failure it rolls
#     the template back to the previous pin
#
# FTXUI upgrades can break the app compile if its API changed - configure
# passing here only means the patch applied; then run ./build.sh.

set -euo pipefail

REPO="https://github.com/ArthurSonzogni/FTXUI.git"
TEMPLATE="CMakeLists.txt.in"
PATCH="patches/ftxui-c0-delivery.patch"

command -v git >/dev/null 2>&1 || { echo "ERROR: git is required."; exit 1; }
[ -f "$TEMPLATE" ] || { echo "ERROR: $TEMPLATE not found. Run from the repo root."; exit 1; }
[ -f "$PATCH" ] || { echo "ERROR: $PATCH not found."; exit 1; }

CURRENT=$(grep -oP '^  GIT_TAG\s+\K\S+' "$TEMPLATE")
echo "FTXUI currently pinned at: $CURRENT"

if [ $# -ge 1 ]; then
    TARGET="$1"
    echo "Using requested tag: $TARGET"
    git ls-remote --tags --refs "$REPO" | grep -q "refs/tags/$TARGET\$" \
        || { echo "ERROR: tag '$TARGET' not found in $REPO."; exit 1; }
else
    echo "Querying latest FTXUI release from GitHub..."
    VER=$(git ls-remote --tags --refs "$REPO" \
        | grep -oP 'refs/tags/\K[vV][0-9]+(\.[0-9]+)+$' \
        | sort -V | tail -1)
    [ -n "$VER" ] || { echo "ERROR: could not determine the latest FTXUI tag (network?)."; exit 1; }
    TARGET="$VER"
fi

if [ "$TARGET" = "$CURRENT" ]; then
    echo "FTXUI is already at $TARGET - nothing to do."
    exit 0
fi

echo ""
echo "Upgrading FTXUI: $CURRENT -> $TARGET"

sed -i "s|^  GIT_TAG .*|  GIT_TAG $TARGET|" "$TEMPLATE"

# Clear cached FetchContent checkouts so the new tag is fetched on next build
for dep in build/_deps/ftxui-src build/_deps/ftxui-build \
           build-windows/_deps/ftxui-src build-windows/_deps/ftxui-build; do
    if [ -e "$dep" ]; then
        rm -rf "$dep"
        echo "Removed cached: $dep"
    fi
done

# Regenerate CMakeLists.txt from the template (same as build.sh) and run a
# configure pass to fetch the new tag and verify the patch still applies.
echo ""
echo "Verifying FTXUI $TARGET fetches and the patch applies..."
source ./config.sh
cp "$TEMPLATE" CMakeLists.txt
sed -i "s/<<TARGET_NAME>>/$APP_NAME/g" CMakeLists.txt
SOURCES_TMP=$(mktemp)
for s in "${SOURCES[@]}"; do
    echo "    $s" >> "$SOURCES_TMP"
done
sed -i "/^<<SOURCES>>$/{
    r $SOURCES_TMP
    d
}" CMakeLists.txt
rm -f "$SOURCES_TMP"
for lib in "${LIBS[@]}"; do
    if [[ "$lib" == *::* ]]; then
        echo "target_link_libraries($APP_NAME PRIVATE $lib)" >> CMakeLists.txt
    else
        echo "target_link_libraries($APP_NAME PRIVATE \${CMAKE_SOURCE_DIR}/$lib)" >> CMakeLists.txt
    fi
done

mkdir -p build
if ! cmake -S . -B build; then
    echo ""
    echo "ERROR: configure failed for FTXUI $TARGET."
    echo "  The local patch ($PATCH) no longer applies cleanly."
    echo ""
    echo "  For FTXUI >= v6.0.0 the CAN/SUB fix is already upstream:"
    echo "  Parse() returns SPECIAL for every C0 byte (incl. Ctrl+X/Ctrl+Z),"
    echo "  so the patch is obsolete - remove the 'PATCH_COMMAND ...' line"
    echo "  from $TEMPLATE and run ./upgrade.sh again."
    echo ""
    echo "  For older versions, re-baseline the patch against the new source"
    echo "  and run ./upgrade.sh again."
    echo ""
    echo "  Rolling back the pin to $CURRENT..."
    sed -i "s|^  GIT_TAG .*|  GIT_TAG $CURRENT|" "$TEMPLATE"
    rm -rf build/_deps/ftxui-src build/_deps/ftxui-build
    echo "Rolled back. Nothing was changed."
    exit 1
fi

echo ""
echo "Done: FTXUI pinned to $TARGET ($TEMPLATE)."
echo "Run ./build.sh to rebuild with the new FTXUI."
    