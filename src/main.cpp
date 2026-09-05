#include "main.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/color.hpp>

#include "audio.hpp"
#include "config.hpp"
#include "file_browser.hpp"
#include "help_dialog.hpp"

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
  bool show_help = false;
  int active_row = 0;       // 0 = loop-length row, 1 = slice-length row
  int bars_focus = 0;       // highlighted loop-length option (0..3)
  int slice_focus = 0;      // highlighted slice-length option (0..6)
  int bars_selected = -1;   // selected loop-length option, -1 = none
  int slice_selected = -1;  // selected slice-length option, -1 = none
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
  auto help = ftxui::Make<HelpDialog>([&]() { show_help = false; });

  const std::vector<std::string> kBarOptions = {"1", "2", "3", "4"};
  const std::vector<std::string> kSliceOptions = {"1", "2", "4", "8",
                                                  "16", "32", "64"};
  const auto render_option_row =
      [&](const std::string& row_title, const std::string& unit,
          const std::vector<std::string>& options, int focus, int selected,
          bool active) -> ftxui::Element {
    ftxui::Elements opts;
    auto label = ftxui::text(" " + row_title + " ");
    label |= active ? (ftxui::color(ftxui::Color::Cyan) | ftxui::bold)
                    : ftxui::dim;
    opts.push_back(label);
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
      const bool focused = (i == focus);
      const bool is_selected = (i == selected);
      std::string mark = is_selected ? "●" : "○";
      std::string val = unit == "/" ? "1/" + options[i]
                                    : options[i] + " bar" +
                                          (options[i] == "1" ? "" : "s");
      auto opt = ftxui::text(mark + " " + val + "  ");
      if (focused) {
        opt |= ftxui::color(ftxui::Color::Cyan) | ftxui::bold;
      }
      if (is_selected) {
        opt |= ftxui::color(ftxui::Color::Green) | ftxui::bold;
      }
      opts.push_back(std::move(opt));
    }
    return ftxui::hbox(std::move(opts));
  };

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
    if (!loaded_file.empty()) {
      lines.push_back(ftxui::separator());
      lines.push_back(
          render_option_row("Loop length (4/4)", "bar", kBarOptions,
                            bars_focus, bars_selected, active_row == 0));
      lines.push_back(
          render_option_row("Slice length", "/", kSliceOptions, slice_focus,
                            slice_selected, active_row == 1));
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
    if (!show_modal && event == ftxui::Event::Character('?')) {
      show_help = true;
      return true;
    }
    if (!show_modal && !loaded_file.empty()) {
      if (event == ftxui::Event::ArrowDown) {
        if (active_row < 1) {
          active_row++;
        }
        return true;
      }
      if (event == ftxui::Event::ArrowUp) {
        if (active_row > 0) {
          active_row--;
        }
        return true;
      }
      if (event == ftxui::Event::ArrowLeft) {
        if (active_row == 0) {
          bars_focus = (bars_focus + 3) % 4;
        } else {
          slice_focus = (slice_focus + 6) % 7;
        }
        return true;
      }
      if (event == ftxui::Event::ArrowRight) {
        if (active_row == 0) {
          bars_focus = (bars_focus + 1) % 4;
        } else {
          slice_focus = (slice_focus + 1) % 7;
        }
        return true;
      }
      if (event == ftxui::Event::Return) {
        if (active_row == 0) {
          bars_selected = bars_focus;
        } else {
          slice_selected = slice_focus;
        }
        return true;
      }
    }
    if (!show_modal && event == ftxui::Event::Escape && !loaded_file.empty()) {
      if (active_row == 0) {
        bars_selected = -1;
      } else {
        slice_selected = -1;
      }
      if (player.IsPlaying()) {
        player.Stop();
      }
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
  app = ftxui::Modal(app, help, &show_help);

  screen.Loop(app);
  return 0;
}

}  // namespace jack

int main() {
  return jack::RunApp();
}