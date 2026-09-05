#!/bin/bash

# Central configuration - edit values here, all scripts pick them up

APP_NAME="App"

SOURCES=(
    "src/main.cpp"
    "src/config.cpp"
    "src/file_browser.cpp"
)

LIBS=(
    "ftxui::screen"
    "ftxui::dom"
    "ftxui::component"
)

HEADERS=(
    "src/main.hpp"
    "src/config.hpp"
    "src/file_browser.hpp"
)
