#ifndef JACK_THE_SLICER_FILE_BROWSER_HPP
#define JACK_THE_SLICER_FILE_BROWSER_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

namespace jack {

// A modal "open file" dialog. Includes an editable path bar (Tab completes,
// Enter opens) and a folder/file list (arrows to move, Enter to open).
class FileBrowser : public ftxui::ComponentBase {
 public:
  using OnOpen = std::function<void(const std::filesystem::path&)>;
  using OnCancel = std::function<void()>;

  FileBrowser(OnOpen on_open, OnCancel on_cancel);

  void Open(const std::filesystem::path& directory);
  void Close();
  bool IsOpen() const { return open_; }

  ftxui::Element Render() override;
  bool OnEvent(ftxui::Event event) override;
  ftxui::Component ActiveChild() override;
  void SetActiveChild(ftxui::ComponentBase* child) override;
  bool Focusable() const override;

 private:
  void Refresh();
  void EnterDirectory(const std::filesystem::path& directory);
  void SetPathText(const std::filesystem::path& directory);
  bool EnterPath(const std::string& text);
  bool Complete();
  bool IsCurrentDirectory() const;
  void OnPathChanged();
  void OnPathEnter();
  void OnMenuEnter();
  std::string ExpandTilde(const std::string& text) const;
  bool IsWav(const std::filesystem::path& path) const;

  OnOpen on_open_;
  OnCancel on_cancel_;
  bool open_ = false;
  bool focus_input_ = true;

  std::filesystem::path current_dir_;
  std::vector<std::string> labels_;
  std::vector<std::filesystem::path> entries_;
  std::string path_text_;
  std::string status_;
  int menu_selected_ = 0;
  int menu_focused_ = 0;

  ftxui::Component input_;
  ftxui::Component menu_;
};

}  // namespace jack

#endif  // JACK_THE_SLICER_FILE_BROWSER_HPP