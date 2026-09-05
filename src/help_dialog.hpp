#ifndef JACK_THE_SLICER_HELP_DIALOG_HPP
#define JACK_THE_SLICER_HELP_DIALOG_HPP

#include <functional>

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

namespace jack {

// A modal dialog listing the keyboard shortcuts. Closes on '?', 'q', Esc or
// Enter. Extend the binding list in help_dialog.cpp as features are added.
class HelpDialog : public ftxui::ComponentBase {
 public:
  using OnClose = std::function<void()>;

  explicit HelpDialog(OnClose on_close);

  ftxui::Element Render() override;
  bool OnEvent(ftxui::Event event) override;
  bool Focusable() const override { return true; }

 private:
  OnClose on_close_;
  int scroll_ = 0;
  int max_scroll_ = 0;
};

}  // namespace jack

#endif  // JACK_THE_SLICER_HELP_DIALOG_HPP