#!/bin/bash
#
# app setup - checks prerequisites and prepares the FTXUI dependency.
#
# Safe to run at any time; it does NOT assume a blank/fresh project and never
# creates scaffolding. Optional flag:
#   ./setup.sh --prefetch   download FTXUI now so the first ./build.sh works
#                           even without network access.
#
# FTXUI requirements covered here:
#   - cmake 3.16+            (FetchContent needs it; build.sh uses cmake+make)
#   - a C++17 compiler        (g++ or clang++)
#   - git                     (FetchContent clones FTXUI from GitHub; the
#                              PATCH_COMMAND also runs 'git apply')
#   - make                    (build.sh compiles with 'make')
#   - network to github.com   (to download FTXUI on first build)
#   - mingw-w64               (optional, only for ./build-windows.sh)

set -e

PREFETCH=0
case "${1:-}" in
    "" ) ;;
    --prefetch|-p) PREFETCH=1 ;;
    * ) echo "usage: ./setup.sh [--prefetch]"; exit 2 ;;
esac

echo "=== app setup ==="

# --- cmake -----------------------------------------------------------------
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake is required (3.16+). Install it and try again."; exit 1; }
CMAKE_VER=$(cmake --version | head -1 | grep -oP '\d+\.\d+')
if [ "$(printf '%s\n3.16\n' "$CMAKE_VER" | sort -V | head -1)" != "3.16" ]; then
    echo "ERROR: cmake 3.16+ required (found $CMAKE_VER)"
    exit 1
fi
echo "[OK] cmake $CMAKE_VER"

# --- compiler --------------------------------------------------------------
if command -v g++ >/dev/null 2>&1; then
    echo "[OK] g++ $(g++ -dumpversion)"
elif command -v clang++ >/dev/null 2>&1; then
    echo "[OK] clang++ ($(clang++ --version | head -1))"
else
    echo "ERROR: no C++17 compiler found (g++ or clang++)"
    exit 1
fi

# --- git (FTXUI is fetched from GitHub and patched with 'git apply') -------
command -v git >/dev/null 2>&1 || { echo "ERROR: git is required (FTXUI is fetched from GitHub and patched)."; exit 1; }
echo "[OK] git $(git --version | cut -d' ' -f3)"

# --- make (build.sh compiles with 'make') ----------------------------------
command -v make >/dev/null 2>&1 || { echo "ERROR: make is required (build.sh runs 'make')."; exit 1; }
echo "[OK] make $(make --version | head -1 | sed 's/^GNU Make //')"

# --- FTXUI dependency ------------------------------------------------------
FTXUI_SRC="build/_deps/ftxui-src"

# A checkout counts as present only if it is actually populated (a stale empty
# directory must not look like a cached dependency).
ftxui_present() {
    [ -f "$FTXUI_SRC/CMakeLists.txt" ] || [ -d "$FTXUI_SRC/src" ]
}

# Report the checked-out version, but only from the checkout's own git repo -
# `git -C` would otherwise walk up into the project's repo if .git is missing.
ftxui_version() {
    if [ -e "$FTXUI_SRC/.git" ] || [ -f "$FTXUI_SRC/.git" ]; then
        git -C "$FTXUI_SRC" describe --tags --always 2>/dev/null || echo "cached"
    else
        echo "cached"
    fi
}

if ftxui_present; then
    echo "[OK] FTXUI $(ftxui_version) (cached in $FTXUI_SRC)"
elif [ "$PREFETCH" -eq 1 ]; then
    echo "Prefetching FTXUI dependency (fetch only, no compile)..."
    # Regenerate CMakeLists.txt from the template, exactly like build.sh does
    # (keep this block in sync with build.sh / build-windows.sh).
    source ./config.sh
    cp CMakeLists.txt.in CMakeLists.txt
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
    # Clear any stale/empty checkout so FetchContent's download step can clone
    # fresh (git clone refuses an existing non-empty directory).
    rm -rf "$FTXUI_SRC"
    cmake -S . -B build
    if ftxui_present; then
        echo "[OK] FTXUI $(ftxui_version) fetched into $FTXUI_SRC"
    else
        echo "WARNING: FTXUI fetch did not produce $FTXUI_SRC - check network and rerun."
    fi
else
    echo "FTXUI not fetched yet - it will be downloaded on first ./build.sh (needs network)."
    echo "  To fetch it now:  ./setup.sh --prefetch"
fi

# --- Windows cross-build (optional) ----------------------------------------
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "[OK] MinGW-w64 cross-compiler (Windows builds supported)"
else
    echo "MinGW-w64 not found - Windows builds (./build-windows.sh) will fail."
    echo "  Install it with:  sudo apt install -y mingw-w64"
fi

echo ""
echo "=== Setup complete ==="
echo "Run ./build.sh to build."
    