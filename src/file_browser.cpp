#include "file_browser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/color.hpp>

namespace jack {
namespace {
namespace fs = std::filesystem;

bool EndsWithIgnoreCase(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  const size_t offset = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

std::string ToLower(const std::string& value) {
  std::string result = value;
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

ftxui::Element FileEntryTransform(const ftxui::EntryState& state) {
  const std::string& label = state.label;
  const bool is_parent = label == "..";
  const bool is_dir = !label.empty() && label.back() == '/';
  const bool is_wav = EndsWithIgnoreCase(label, ".wav");

  ftxui::Element element = ftxui::text((state.active ? "> " : "  ") + label);
  if (is_parent) {
    element = element | ftxui::color(ftxui::Color::Yellow);
  } else if (is_dir) {
    element = element | ftxui::color(ftxui::Color::Cyan);
  } else if (is_wav) {
    element = element | ftxui::color(ftxui::Color::Green);
  } else {
    element = element | ftxui::color(ftxui::Color::GrayDark);
  }
  if (state.focused) {
    element = element | ftxui::inverted;
  }
  if (state.active) {
    element = element | ftxui::bold;
  }
  return element;
}

}  // namespace

FileBrowser::FileBrowser(OnOpen on_open, OnCancel on_cancel)
    : on_open_(std::move(on_open)), on_cancel_(std::move(on_cancel)) {
  ftxui::InputOption input_option;
  input_option.placeholder = "/path/to/file.wav";
  input_option.multiline = false;
  input_option.on_change = [this] { OnPathChanged(); };
  input_option.on_enter = [this] { OnPathEnter(); };
  input_ = ftxui::Input(&path_text_, std::move(input_option));

  ftxui::MenuOption menu_option;
  menu_option.entries = &labels_;
  menu_option.selected = &menu_selected_;
  menu_option.focused_entry = &menu_focused_;
  menu_option.on_enter = [this] { OnMenuEnter(); };
  menu_option.entries_option.transform = FileEntryTransform;
  menu_ = ftxui::Menu(std::move(menu_option));

  Add(input_);
  Add(menu_);
}

void FileBrowser::Open(const fs::path& directory) {
  open_ = true;
  status_.clear();

  fs::path dir = directory;
  if (dir.empty()) {
    dir = fs::current_path();
  }
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    if (const char* home = std::getenv("HOME"); home && *home) {
      dir = fs::path(home);
    } else {
      dir = fs::current_path();
    }
  }

  fs::path resolved = fs::weakly_canonical(dir, ec);
  if (ec) {
    resolved = dir;
  }
  EnterDirectory(resolved);
  focus_input_ = true;
}

void FileBrowser::Close() {
  open_ = false;
}

ftxui::Element FileBrowser::Render() {
  ftxui::Element status_element =
      status_.empty()
          ? ftxui::text("Tab: complete / switch to list   |   Enter: open   |   Esc: "
                        "close") |
                ftxui::dim
          : ftxui::text(status_) | ftxui::color(ftxui::Color::Yellow);

  ftxui::Element content = ftxui::vbox({
      ftxui::text("Path:"),
      input_->Render(),
      ftxui::separator(),
      ftxui::text("Files:"),
      menu_->Render() | ftxui::vscroll_indicator | ftxui::frame |
          ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 14),
      ftxui::separator(),
      status_element,
  });

  return ftxui::window(ftxui::text(" Open .wav "), content) |
         ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 10);
}

bool FileBrowser::OnEvent(ftxui::Event event) {
  if (!open_) {
    return false;
  }
  if (event == ftxui::Event::Escape) {
    Close();
    if (on_cancel_) {
      on_cancel_();
    }
    return true;
  }
  if (event.is_character()) {
    const char first = event.character().empty() ? 0 : event.character()[0];
    if (static_cast<unsigned char>(first) >= 0x20 && !focus_input_) {
      focus_input_ = true;
    }
    return input_->OnEvent(event);
  }
  if (event == ftxui::Event::Tab) {
    if (focus_input_) {
      if (!IsCurrentDirectory() && Complete()) {
        input_->OnEvent(ftxui::Event::End);
        return true;
      }
      focus_input_ = false;
      return true;
    }
    focus_input_ = true;
    return true;
  }
  if (event == ftxui::Event::TabReverse) {
    focus_input_ = !focus_input_;
    return true;
  }
  if (event == ftxui::Event::Return) {
    if (focus_input_) {
      input_->OnEvent(event);
    } else {
      menu_->OnEvent(event);
    }
    return true;
  }
  if (event.is_mouse()) {
    return ftxui::ComponentBase::OnEvent(event);
  }
  return (focus_input_ ? input_ : menu_)->OnEvent(event);
}

ftxui::Component FileBrowser::ActiveChild() {
  return focus_input_ ? input_ : menu_;
}

void FileBrowser::SetActiveChild(ftxui::ComponentBase* child) {
  if (child == input_.get()) {
    focus_input_ = true;
  } else if (child == menu_.get()) {
    focus_input_ = false;
  }
}

bool FileBrowser::Focusable() const {
  return true;
}

void FileBrowser::Refresh() {
  labels_.clear();
  entries_.clear();

  const fs::path parent = current_dir_.parent_path();
  if (current_dir_.has_parent_path() && parent != current_dir_) {
    entries_.push_back(parent);
    labels_.push_back("..");
  }

  std::vector<std::pair<std::string, fs::path>> items;
  std::error_code ec;
  for (fs::directory_iterator it(current_dir_,
                                 fs::directory_options::skip_permission_denied,
                                 ec),
                              end;
       it != end; it.increment(ec)) {
    if (ec) {
      break;
    }
    const fs::path entry = it->path();
    std::error_code sec;
    const bool is_dir = fs::is_directory(entry, sec);
    items.emplace_back(is_dir ? entry.filename().string() + "/"
                              : entry.filename().string(),
                       entry);
  }

  std::sort(items.begin(), items.end(),
            [](const std::pair<std::string, fs::path>& a,
               const std::pair<std::string, fs::path>& b) {
              const bool dir_a = !a.first.empty() && a.first.back() == '/';
              const bool dir_b = !b.first.empty() && b.first.back() == '/';
              if (dir_a != dir_b) {
                return dir_a;
              }
              const std::string lower_a = ToLower(a.first);
              const std::string lower_b = ToLower(b.first);
              if (lower_a != lower_b) {
                return lower_a < lower_b;
              }
              return a.first < b.first;
            });

  for (auto& item : items) {
    entries_.push_back(item.second);
    labels_.push_back(std::move(item.first));
  }

  menu_selected_ = 0;
  menu_focused_ = 0;
}

void FileBrowser::EnterDirectory(const fs::path& directory) {
  current_dir_ = directory;
  status_.clear();
  Refresh();
  SetPathText(current_dir_);
  input_->OnEvent(ftxui::Event::End);
}

void FileBrowser::SetPathText(const fs::path& directory) {
  std::string text = directory.string();
  if (text.empty() || text.back() != '/') {
    text += '/';
  }
  path_text_ = text;
}

bool FileBrowser::EnterPath(const std::string& text) {
  if (text.empty()) {
    status_ = "Type a path or use the file list below.";
    return false;
  }

  const fs::path path = fs::path(ExpandTilde(text));
  const fs::path absolute = path.is_relative() ? current_dir_ / path : path;

  std::error_code ec;
  if (fs::is_directory(absolute, ec)) {
    fs::path resolved = fs::weakly_canonical(absolute, ec);
    EnterDirectory(resolved);
    return true;
  }

  if (fs::is_regular_file(absolute, ec)) {
    if (IsWav(absolute)) {
      fs::path resolved = fs::weakly_canonical(absolute, ec);
      if (on_open_) {
        on_open_(resolved);
      }
      Close();
      return true;
    }
    status_ = "Not a .wav file: " + absolute.filename().string();
    return true;
  }

  status_ = "No such file or directory: " + text;
  return false;
}

void FileBrowser::OnPathEnter() {
  if (EnterPath(path_text_)) {
    return;
  }
  focus_input_ = false;
}

void FileBrowser::OnMenuEnter() {
  if (menu_selected_ < 0 ||
      menu_selected_ >= static_cast<int>(entries_.size())) {
    return;
  }
  const fs::path& entry = entries_[static_cast<size_t>(menu_selected_)];
  std::error_code ec;
  if (fs::is_directory(entry, ec)) {
    EnterDirectory(entry);
    return;
  }
  if (IsWav(entry)) {
    fs::path resolved = fs::weakly_canonical(entry, ec);
    if (on_open_) {
      on_open_(resolved);
    }
    Close();
    return;
  }
  status_ = "Not a .wav file: " + entry.filename().string();
}

void FileBrowser::OnPathChanged() {
  if (path_text_.empty()) {
    return;
  }
  if (path_text_.back() != '/') {
    return;
  }
  const fs::path path = fs::path(ExpandTilde(path_text_));
  const fs::path absolute = path.is_relative() ? current_dir_ / path : path;
  std::error_code ec;
  if (!fs::is_directory(absolute, ec)) {
    return;
  }
  fs::path resolved = fs::weakly_canonical(absolute, ec);
  if (resolved != current_dir_) {
    EnterDirectory(resolved);
  }
}

bool FileBrowser::Complete() {
  if (path_text_.empty()) {
    return false;
  }

  const std::string raw = path_text_;

  const fs::path whole = fs::path(ExpandTilde(raw));
  const fs::path absolute_whole =
      whole.is_relative() ? current_dir_ / whole : whole;
  std::error_code ec;
  if (fs::is_directory(absolute_whole, ec)) {
    EnterDirectory(absolute_whole);
    return true;
  }

  const size_t slash = raw.find_last_of('/');
  const std::string base_raw =
      slash == std::string::npos ? std::string() : raw.substr(0, slash + 1);
  const std::string name =
      slash == std::string::npos ? raw : raw.substr(slash + 1);
  if (name.empty()) {
    return false;
  }

  fs::path dir;
  if (base_raw.empty()) {
    dir = current_dir_;
  } else {
    dir = fs::path(ExpandTilde(base_raw));
    if (dir.is_relative()) {
      dir = current_dir_ / dir;
    }
  }

  std::error_code dec;
  if (!fs::is_directory(dir, dec)) {
    status_ = "No such directory: " + base_raw;
    return false;
  }

  std::vector<std::string> matches;
  std::error_code iter_ec;
  for (fs::directory_iterator it(dir,
                                 fs::directory_options::skip_permission_denied,
                                 iter_ec),
                              end;
       it != end; it.increment(iter_ec)) {
    if (iter_ec) {
      break;
    }
    const std::string entry_name = it->path().filename().string();
    if (entry_name.rfind(name, 0) == 0) {
      matches.push_back(entry_name);
    }
  }

  if (matches.empty()) {
    status_ = "No match for \"" + name + "\"";
    return false;
  }

  std::sort(matches.begin(), matches.end());

  if (matches.size() == 1) {
    const std::string& complete = matches[0];
    const fs::path full = dir / complete;
    std::error_code fec;
    const bool is_dir = fs::is_directory(full, fec);
    path_text_ = base_raw + complete + (is_dir ? "/" : "");
    if (is_dir) {
      EnterDirectory(full);
    } else {
      status_.clear();
    }
    return true;
  }

  std::string prefix = matches[0];
  for (size_t i = 1; i < matches.size(); ++i) {
    size_t j = 0;
    while (j < prefix.size() && j < matches[i].size() &&
           prefix[j] == matches[i][j]) {
      ++j;
    }
    prefix.resize(j);
  }
  path_text_ = base_raw + prefix;
  status_.clear();
  return true;
}

bool FileBrowser::IsCurrentDirectory() const {
  const fs::path path = fs::path(ExpandTilde(path_text_));
  const fs::path absolute = path.is_relative() ? current_dir_ / path : path;
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(absolute, ec);
  return !ec && canonical == current_dir_;
}

std::string FileBrowser::ExpandTilde(const std::string& text) const {
  std::string result = text;
  if (result == "~" || result.rfind("~/", 0) == 0) {
    if (const char* home = std::getenv("HOME"); home && *home) {
      result = std::string(home) + result.substr(1);
    }
  }
  return result;
}

bool FileBrowser::IsWav(const fs::path& path) const {
  return EndsWithIgnoreCase(path.filename().string(), ".wav");
}

}  // namespace jack