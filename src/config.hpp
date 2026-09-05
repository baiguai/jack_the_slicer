#ifndef JACK_THE_SLICER_CONFIG_HPP
#define JACK_THE_SLICER_CONFIG_HPP

#include <filesystem>

namespace jack {

struct Config {
  std::filesystem::path last_directory;
};

std::filesystem::path DefaultDirectory();
Config LoadConfig();
void SaveConfig(const Config& config);

}  // namespace jack

#endif  // JACK_THE_SLICER_CONFIG_HPP