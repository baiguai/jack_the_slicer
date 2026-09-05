#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace jack {
namespace {
namespace fs = std::filesystem;

fs::path ConfigPath() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "jack_the_slicer" / "config";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".config" / "jack_the_slicer" / "config";
  }
  return fs::current_path() / ".jack_the_slicer_config";
}

}  // namespace

fs::path DefaultDirectory() {
  if (const char* home = std::getenv("HOME"); home && *home) {
    return fs::path(home);
  }
  return fs::current_path();
}

Config LoadConfig() {
  Config config;
  config.last_directory = DefaultDirectory();

  std::ifstream in(ConfigPath());
  std::string line;
  if (std::getline(in, line)) {
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) {
      line.pop_back();
    }
    if (!line.empty()) {
      std::error_code ec;
      const fs::path dir(line);
      if (fs::is_directory(dir, ec)) {
        config.last_directory = dir;
      }
    }
  }
  return config;
}

void SaveConfig(const Config& config) {
  if (config.last_directory.empty()) {
    return;
  }
  std::error_code ec;
  const fs::path path = ConfigPath();
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (out) {
    out << config.last_directory.string() << '\n';
  }
}

}  // namespace jack