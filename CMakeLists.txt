cmake_minimum_required(VERSION 3.16)
project(App VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create executable
add_executable(App
    src/main.cpp
    src/config.cpp
    src/file_browser.cpp
)

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
  PATCH_COMMAND git checkout -- . && git apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-c0-delivery.patch
)
FetchContent_MakeAvailable(ftxui)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    # Linux/X11
    find_package(X11 REQUIRED)
    target_link_libraries(App PRIVATE X11::X11 X11::Xfixes)
elseif(APPLE)
    # macOS
    find_library(COCOA Cocoa)
    find_library(COREFOUNDATION CoreFoundation)
    target_link_libraries(App PRIVATE ${COCOA} ${COREFOUNDATION})
elseif(WIN32)
    # Windows
    target_link_libraries(App PRIVATE user32 kernel32 gdi32)
endif()

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(App PRIVATE -Wall -Wextra -O2)
endif()

# Set output directory
set_target_properties(App PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
target_link_libraries(App PRIVATE ftxui::screen)
target_link_libraries(App PRIVATE ftxui::dom)
target_link_libraries(App PRIVATE ftxui::component)
