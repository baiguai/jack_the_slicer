#include "help_dialog.hpp"

#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace jack {

namespace {

struct Binding {
  std::string key;
  std::string description;
};

struct BindingSection {
  std::string title;
  std::vector<Binding> bindings;
};

const std::vector<BindingSection>& Sections() {
  static const std::vector<BindingSection> sections = {
      {"Main screen",
       {{"?", "Show this help"},
        {"Ctrl+O", "Open a .wav file"},
        {"p", "Play the loaded .wav"},
        {"P", "Play the loaded .wav on loop"},
        {"Esc", "Stop playback"},
        {"q", "Quit"}}},
      {"Loop-length selector",
       {{"Left / Right", "Choose the loop length (1-4 bars of 4/4)"},
        {"Enter", "Select the loop length"},
        {"Esc", "Clear the loop-length selection"}}},
      {"Open-file dialog",
       {{"Tab", "Complete the path / focus the file list"},
        {"Up / Down", "Move through the file list"},
        {"Enter", "Open the selected entry"},
        {"Esc", "Close the dialog"}}},
  };
  return sections;
}

}  // namespace

HelpDialog::HelpDialog(OnClose on_close) : on_close_(std::move(on_close)) {}

bool HelpDialog::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::Escape || event == ftxui::Event::Return ||
      event == ftxui::Event::Character('q') ||
      event == ftxui::Event::Character('Q') ||
      event == ftxui::Event::Character('?')) {
    if (on_close_) {
      on_close_();
    }
    return true;
  }
  return false;
}

ftxui::Element HelpDialog::Render() {
  ftxui::Elements lines;
  for (const BindingSection& section : Sections()) {
    if (!lines.empty()) {
      lines.push_back(ftxui::separator());
    }
    lines.push_back(ftxui::text(" " + section.title + " ") | ftxui::bold |
                    ftxui::color(ftxui::Color::Yellow));
    for (const Binding& binding : section.bindings) {
      lines.push_back(ftxui::hbox({
          ftxui::text("   " + binding.key) | ftxui::bold |
              ftxui::color(ftxui::Color::Cyan) |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 20),
          ftxui::text(binding.description),
      }));
    }
  }
  lines.push_back(ftxui::separator());
  lines.push_back(ftxui::text("  ?, Esc, q or Enter: close this help") |
                  ftxui::dim);

  return ftxui::window(ftxui::text(" Keyboard shortcuts ") | ftxui::bold,
                       ftxui::vbox(std::move(lines))) |
         ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 46) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 12);
}

}  // namespace jack