# Cross-compile toolchain for building Windows binaries from Linux.
#
# Usage (from the project root):
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake -B build-windows -S .
#
# System packages needed on the Linux host:
#   sudo apt install -y mingw-w64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Where the MinGW-w64 runtime and standard libraries live.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Never pick up Linux host programs/tools; only search the target root for
# libraries and headers so nothing leaks through from /usr.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)