#include "help_dialog.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

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
{"Open-file dialog",
        {{"Tab", "Complete the path / focus the file list"},
         {"Up / Down", "Move through the file list"},
         {"Enter", "Open the selected entry"},
         {"Esc", "Close the dialog"}}},
       {"Loop-length selector",
        {{"Up / Down", "Move between the loop-length and slice-length rows"},
         {"Left / Right", "Choose an option in the active row"},
         {"Enter", "Select the active option"},
         {"Esc", "Clear the row selection / stop playback"}}},
{"Slice column",
        {{"Up / Down", "Move through the slices (one per line)"},
         {"Enter", "Open the effect menu for a slice"},
         {"Up / Down", "Choose an effect in the open menu"},
         {"Enter", "Apply the highlighted effect"},
         {"Esc", "Hide the menu, or clear a slice effect"},
         {"Ctrl+R", "Re-randomize every slice effect"},
         {"Effects", "None, Shuffle, Reverse, Stretch, Squish"}}},
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
    scroll_ = 0;
    if (on_close_) {
      on_close_();
    }
    return true;
  }
  if (event == ftxui::Event::ArrowDown && scroll_ < max_scroll_) {
    scroll_++;
    return true;
  }
  if (event == ftxui::Event::ArrowUp && scroll_ > 0) {
    scroll_--;
    return true;
  }
  return false;
}

ftxui::Element HelpDialog::Render() {
  ftxui::Elements content;
  for (const BindingSection& section : Sections()) {
    if (!content.empty()) {
      content.push_back(ftxui::separator());
    }
    content.push_back(ftxui::text(" " + section.title + " ") | ftxui::bold |
                      ftxui::color(ftxui::Color::Yellow));
    for (const Binding& binding : section.bindings) {
      content.push_back(ftxui::hbox({
          ftxui::text("   " + binding.key) | ftxui::bold |
              ftxui::color(ftxui::Color::Cyan) |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 20),
          ftxui::text(binding.description),
      }));
    }
  }
  content.push_back(ftxui::separator());
  content.push_back(
      ftxui::text("  ?, Esc, q, Enter: close    Up/Down: scroll") |
      ftxui::dim);

  const int avail = std::max(1, ftxui::Terminal::Size().dimy - 4);
  max_scroll_ = std::max(0, static_cast<int>(content.size()) - avail);
  scroll_ = std::clamp(scroll_, 0, max_scroll_);

  ftxui::Elements visible;
  for (int k = scroll_;
       k < scroll_ + avail && k < static_cast<int>(content.size()); ++k) {
    visible.push_back(std::move(content[k]));
  }

  return ftxui::window(ftxui::text(" Keyboard shortcuts ") | ftxui::bold,
                       ftxui::vbox(std::move(visible))) |
         ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 46) |
         ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN,
                     ftxui::Terminal::Size().dimy - 2);
}

}  // namespace jack