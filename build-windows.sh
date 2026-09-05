#!/bin/bash

# Requirements:
# sudo apt install -y mingw-w64

source ./config.sh

set -e

echo "Building $APP_NAME for Windows..."

BUILD_TYPE="Debug"
if [ "$1" == "r" ]; then
    BUILD_TYPE="Release"
fi
echo "Performing $BUILD_TYPE build."

TOOLCHAIN="$(dirname "$0")/cmake/mingw-x86_64.cmake"

mkdir -p build-windows

# Generate CMakeLists.txt from template
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

cmake -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
      -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
      -B build-windows \
      -S .

cmake --build build-windows

echo "Build complete: build-windows/bin/$APP_NAME.exe"

# The cross-compiled exe depends on the MinGW-w64 runtime DLLs, which are not
# present on a stock Windows machine. Resolve each DLL through the compiler
# driver and copy it next to the exe.
MINGW_CC="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
DEST="build-windows/bin"
for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
    src="$("$MINGW_CC" -print-file-name="$dll")"
    if [ -f "$src" ] && [ "$src" != "$dll" ]; then
        cp -f "$src" "$DEST/"
        echo "Copied $dll next to $APP_NAME.exe"
    else
        echo "Warning: could not locate $dll (got: $src) - $APP_NAME.exe may not run on a clean Windows machine" >&2
    fi
done
