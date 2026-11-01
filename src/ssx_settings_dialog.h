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
                    std::filesystem::path config_path, std::function<void()> restart);
  ~SsxSettingsDialog() override;

  // Adds the menu's fonts; call from ReXApp::OnConfigureFonts.
  static void ConfigureFonts(ImFontAtlas* atlas);

  void Toggle();
  // Opens the menu on the named tab ("Display", "Graphics", "Audio", "Controls").
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
  void Checkbox(const char* label, const char* cvar);
  int TabFlags(const char* name);
  void PollPad(ImGuiIO& io);
  void DrawFps();
  void DrawBinds();
  void DrawDisplayTab();
  void DrawGraphicsTab();
  void DrawAudioTab();
  void DrawControlsTab();

  rex::input::InputSystem* input_;
  std::filesystem::path config_path_;
  std::function<void()> restart_;
  bool open_ = false;
  bool restart_needed_ = false;
  float font_scale_ = 1.0f;
  int current_tab_ = 0;
  uint16_t pad_buttons_ = 0;  // previous frame, for edge detection
  std::string select_tab_;  // tab to select on the next frame
  std::string capturing_;  // cvar of the key binding waiting for a key press
};
