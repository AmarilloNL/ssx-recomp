#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>

struct ImFontAtlas;

namespace rex::input {
class InputSystem;
}

class SsxSettingsDialog : public rex::ui::ImGuiDialog {
 public:
  SsxSettingsDialog(rex::ui::ImGuiDrawer* drawer, rex::input::InputSystem* input,
                    std::filesystem::path config_path, std::function<void()> restart,
                    std::function<void()> quit);
  ~SsxSettingsDialog() override;

  // Adds the menu's fonts; call from ReXApp::OnConfigureFonts.
  static void ConfigureFonts(ImFontAtlas* atlas);

  void Toggle();
  // Opens the menu on the named page ("Display", "Graphics", "Audio", "Controls").
  void Open(const std::string& tab);

  struct Choice {
    const char* label;
    const char* value;
  };

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Set(const char* cvar, const std::string& value);
  bool BeginRows(const char* id);
  void Row(const char* label, const char* help);
  bool Combo(const char* label, const char* cvar, const Choice* choices, size_t count);
  bool Checkbox(const char* label, const char* cvar);
  bool Switch(const char* id, bool* value);
  bool Button(const char* label, bool primary);
  bool BeginCard(const char* title, bool rows = true);
  void EndCard();
  ImVec2 card_start_ = {};   // the card being drawn: its top left corner,
  float card_width_ = 0.0f;  // its width,
  bool card_table_ = false;  // and whether its table is open
  void DrawSidebar(float width);
  void DrawMain();
  void PollPad(ImGuiIO& io);
  void DrawFps();
  void DrawBinds();
  void DrawDisplayTab();
  void DrawScreenRows();
  void DrawGraphicsTab();
  void DrawRendererRows();
  void DrawImageRows();
  void DrawAudioTab();
  void DrawControlsTab();
  void DrawProfileTab();
  void DrawPlayerRows();
  void DrawSnowflakesTab();
  void DrawScaleWarning();
  void DrawQuitConfirm();
  void DrawCheatsTab();

  rex::input::InputSystem* input_;
  std::filesystem::path config_path_;
  std::function<void()> restart_;
  std::function<void()> quit_;  // the window close button's path (Quit game, issue #20)
  bool open_ = false;
  bool restart_needed_ = false;
  float font_scale_ = 1.0f;
  float ui_scale_ = 1.0f;  // the menu's size for this screen (1 at 1440p)
  bool toggle_after_draw_ = false;  // the Close button: closes once the frame is drawn
  int current_tab_ = 0;
  uint16_t pad_buttons_ = 0;  // previous frame, for edge detection
  std::string select_tab_;  // page to select on the next frame
  std::string capturing_;  // cvar of the key binding waiting for a key press
};
