#include "main.hpp"

#include <filesystem>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/color.hpp>

#include "audio.hpp"
#include "config.hpp"
#include "file_browser.hpp"

namespace jack {
namespace {
namespace fs = std::filesystem;

// Ctrl+O is transmitted as ASCII 0x0F (SI) and arrives as a "special" event.
const ftxui::Event kCtrlO = ftxui::Event::Special("\x0F");

// A trivial focusable wrapper. Container::Tab (used by ftxui::Modal) only
// routes events to its active child when that child reports Focusable()==true,
// and a bare Renderer does not.
class FocusableHost : public ftxui::ComponentBase {
 public:
  explicit FocusableHost(ftxui::Component child) { Add(std::move(child)); }

  bool Focusable() const override { return true; }
};

}  // namespace

int RunApp() {
  Config config = LoadConfig();
  if (config.last_directory.empty() ||
      !fs::is_directory(config.last_directory)) {
    config.last_directory = DefaultDirectory();
  }

  auto screen = ftxui::ScreenInteractive::Fullscreen();

  std::string loaded_file;
  fs::path last_directory = config.last_directory;
  bool show_modal = false;
  jack::WavPlayer player;

  const auto on_open = [&](const fs::path& path) {
    loaded_file = path.string();
    last_directory = path.parent_path();
    if (last_directory.empty()) {
      last_directory = path.root_path();
    }
    player.Stop();
    Config updated = config;
    updated.last_directory = last_directory;
    SaveConfig(updated);
    show_modal = false;
  };
  const auto on_cancel = [&]() { show_modal = false; };

  auto file_browser = ftxui::Make<FileBrowser>(on_open, on_cancel);

  auto main_screen = ftxui::Renderer([&] {
    player.Poll();
    ftxui::Elements lines;
    lines.push_back(ftxui::text(" jack_the_slicer ") | ftxui::bold);
    lines.push_back(ftxui::separator());
    lines.push_back(ftxui::text("Loaded .wav:"));
    if (loaded_file.empty()) {
      lines.push_back(ftxui::text("  (none)") | ftxui::dim);
    } else {
      lines.push_back(
          ftxui::text("  " + loaded_file) |
          ftxui::color(ftxui::Color::Green));
    }
    if (player.IsPlaying()) {
      const std::string loop_note = player.IsLooping() ? " (looped)" : "";
      lines.push_back(ftxui::text("Playing: " + player.CurrentFile() +
                                  loop_note) |
                      ftxui::color(ftxui::Color::Cyan));
    }
    lines.push_back(ftxui::filler());
    lines.push_back(ftxui::separator());
    lines.push_back(ftxui::text(" Ctrl+O  Open .wav    Q  Quit ") | ftxui::dim);
    return ftxui::vbox(std::move(lines)) | ftxui::border;
  });

  auto main_caught = ftxui::CatchEvent(main_screen, [&](ftxui::Event event) {
    if (!show_modal && event == kCtrlO) {
      show_modal = true;
      file_browser->Open(last_directory);
      return true;
    }
    if (!show_modal && !loaded_file.empty() &&
        (event == ftxui::Event::Character('p') ||
         event == ftxui::Event::Character('P'))) {
      player.Play(loaded_file, event == ftxui::Event::Character('P'));
      return true;
    }
    if (!show_modal && event == ftxui::Event::Escape && player.IsPlaying()) {
      player.Stop();
      return true;
    }
    if (!show_modal && (event == ftxui::Event::Character('q') ||
                        event == ftxui::Event::Character('Q'))) {
      screen.ExitLoopClosure()();
      return true;
    }
    return false;
  });

  auto app = ftxui::Modal(ftxui::Make<FocusableHost>(main_caught), file_browser,
                          &show_modal);

  screen.Loop(app);
  return 0;
}

}  // namespace jack

int main() {
  return jack::RunApp();
}