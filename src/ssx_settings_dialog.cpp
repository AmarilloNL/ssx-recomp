// ssx - in-game settings menu (F1) and optional FPS counter.
//
// Every control edits a cvar and the config is saved immediately, so the next
// launch starts with the same settings. The menu has its own look (fonts and
// colours are pushed only around its windows, not the SDK overlays).

#include "ssx_settings_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#define NOMINMAX
#include <windows.h>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include "ssx_frame_stats.h"
#include "ssx_input_lock.h"
#include "ssx_settings.h"

REXCVAR_DEFINE_BOOL(ssx_show_fps, false, "SSX", "Show the frame rate in the top-left corner");

namespace {

using Choice = SsxSettingsDialog::Choice;

// Fonts are rasterized for a 1440p screen and scaled to the actual height.
constexpr float kFontDesignHeight = 1440.0f;
ImFont* body_font = nullptr;
ImFont* title_font = nullptr;

const ImVec4 kAccent(0.36f, 0.82f, 0.96f, 1.0f);
const ImVec4 kAccentDim(0.20f, 0.52f, 0.66f, 1.0f);
const ImVec4 kText(0.90f, 0.95f, 0.98f, 1.0f);
const ImVec4 kTextDim(0.55f, 0.65f, 0.72f, 1.0f);
const ImVec4 kWarn(1.0f, 0.78f, 0.30f, 1.0f);

const Choice kFramerates[] = {{"30  (original)", "30"}, {"60", "60"},   {"120", "120"},
                              {"144", "144"},           {"165", "165"}, {"240", "240"},
                              {"300", "300"},           {"Match main display", "0"}};
const Choice kScales[] = {{"1x   (fast)", "1"},
                          {"2x   (sharp, heavier)", "2"},
                          {"3x   (about 4K, very heavy)", "3"},
                          {"4x   (beyond 4K, extreme)", "4"}};
const Choice kUpscaling[] = {{"Bilinear  (soft)", "bilinear"},
                             {"AMD CAS  (sharpen)", "cas"},
                             {"AMD FSR 1  (upscale + sharpen)", "fsr"}};
const Choice kRenderSizes[] = {{"Original  1120 x 584", "original"}, {"High  1200 x 640", "high"}};
const Choice kAntiAliasing[] = {{"Off", "none"}, {"FXAA", "fxaa"}, {"FXAA  (strong)", "fxaa_extreme"}};
const Choice kAniso[] = {{"Game default", "-1"}, {"Off", "0"}, {"2x", "2"},
                         {"4x", "3"},            {"8x", "4"},  {"16x", "5"}};

struct Bind {
  const char* label;
  const char* cvar;
};

const Bind kBinds[] = {
    {"Steer up", "keybind_lstick_up"},       {"Steer down", "keybind_lstick_down"},
    {"Steer left", "keybind_lstick_left"},   {"Steer right", "keybind_lstick_right"},
    {"Trick stick up", "keybind_rstick_up"}, {"Trick stick down", "keybind_rstick_down"},
    {"Trick stick left", "keybind_rstick_left"},
    {"Trick stick right", "keybind_rstick_right"},
    {"A", "keybind_a"},                      {"B", "keybind_b"},
    {"X", "keybind_x"},                      {"Y", "keybind_y"},
    {"Left trigger", "keybind_left_trigger"},
    {"Right trigger", "keybind_right_trigger"},
    {"Left bumper", "keybind_left_shoulder"},
    {"Right bumper", "keybind_right_shoulder"},
    {"Left stick click", "keybind_lstick_press"},
    {"Right stick click", "keybind_rstick_press"},
    {"D-pad up", "keybind_dpad_up"},         {"D-pad down", "keybind_dpad_down"},
    {"D-pad left", "keybind_dpad_left"},     {"D-pad right", "keybind_dpad_right"},
    {"Start", "keybind_start"},              {"Back", "keybind_back"},
};

const char* const kTabs[] = {"Display", "Graphics", "Audio", "Controls"};

// X_INPUT_GAMEPAD button bits -> ImGui gamepad keys (ImGui's nav: A activates, B cancels).
const struct {
  uint16_t mask;
  ImGuiKey key;
} kPadKeys[] = {
    {0x0001, ImGuiKey_GamepadDpadUp},   {0x0002, ImGuiKey_GamepadDpadDown},
    {0x0004, ImGuiKey_GamepadDpadLeft}, {0x0008, ImGuiKey_GamepadDpadRight},
    {0x1000, ImGuiKey_GamepadFaceDown}, {0x2000, ImGuiKey_GamepadFaceRight},
    {0x4000, ImGuiKey_GamepadFaceLeft}, {0x8000, ImGuiKey_GamepadFaceUp},
};
constexpr uint16_t kPadStart = 0x0010, kPadBack = 0x0020, kPadLb = 0x0100, kPadRb = 0x0200,
                   kPadB = 0x2000;

bool IsModifier(int vk) { return (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU); }

float UiScale(const ImGuiIO& io) { return std::clamp(io.DisplaySize.y / kFontDesignHeight, 0.5f, 2.0f); }

// Pushes the menu's look; PopStyle() undoes it.
void PushStyle(float s) {
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.06f, 0.09f, 0.97f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(kAccentDim.x, kAccentDim.y, kAccentDim.z, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_Text, kText);
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, kTextDim);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.14f, 0.20f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.21f, 0.29f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.27f, 0.37f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.07f, 0.11f, 0.16f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.18f, 0.40f, 0.52f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.14f, 0.34f, 0.45f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.28f, 0.38f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.42f, 0.55f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.34f, 0.45f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.42f, 0.55f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, kAccentDim);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, kAccent);
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, kAccentDim);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kAccent);
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(kAccentDim.x, kAccentDim.y, kAccentDim.z, 0.35f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.05f, 0.08f, 0.12f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.14f, 0.34f, 0.45f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.18f, 0.42f, 0.55f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, kAccentDim);
  ImGui::PushStyleColor(ImGuiCol_NavCursor, kAccent);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 6.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f * s, 26.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * s, 9.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f * s, 12.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f * s, 9.0f * s));
}

constexpr int kStyleColors = 27;
constexpr int kStyleVars = 10;

void PopStyle() {
  ImGui::PopStyleVar(kStyleVars);
  ImGui::PopStyleColor(kStyleColors);
}

}  // namespace

void SsxSettingsDialog::ConfigureFonts(ImFontAtlas* atlas) {
  // Segoe UI ships with Windows; fall back to ImGui's built-in font without it.
  const char* body = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* title = "C:\\Windows\\Fonts\\segoeuib.ttf";
  if (GetFileAttributesA(body) != INVALID_FILE_ATTRIBUTES) {
    body_font = atlas->AddFontFromFileTTF(body, 30.0f);
  }
  if (GetFileAttributesA(title) != INVALID_FILE_ATTRIBUTES) {
    title_font = atlas->AddFontFromFileTTF(title, 46.0f);
  }
}

SsxSettingsDialog::SsxSettingsDialog(rex::ui::ImGuiDrawer* drawer, rex::input::InputSystem* input,
                                     std::filesystem::path config_path, std::function<void()> restart)
    : ImGuiDialog(drawer),
      input_(input),
      config_path_(std::move(config_path)),
      restart_(std::move(restart)) {}

SsxSettingsDialog::~SsxSettingsDialog() {
  if (open_) Toggle();
}

void SsxSettingsDialog::Toggle() {
  open_ = !open_;
  capturing_.clear();
  // While the menu is open the game must not see the keys and pad used to drive it.
  if (input_) {
    open_ ? input_->AddUIInputBlocker() : input_->RemoveUIInputBlocker();
  }
}

void SsxSettingsDialog::Open(const std::string& tab) {
  if (!open_) Toggle();
  select_tab_ = tab;
}

void SsxSettingsDialog::Set(const char* cvar, const std::string& value) {
  rex::cvar::SetFlagByName(cvar, value);
  SsxSaveConfig(config_path_);
  // Keyboard support is created with the input system at startup.
  const auto* info = rex::cvar::GetFlagInfo(cvar);
  if ((info && info->lifecycle != rex::cvar::Lifecycle::kHotReload) ||
      std::string_view(cvar) == "mnk_mode") {
    restart_needed_ = true;
  }
}

// Starts a two-column settings row: name and short explanation on the left,
// the control (full width) on the right.
void SsxSettingsDialog::Row(const char* label, const char* help) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (help) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::SetWindowFontScale(0.82f * font_scale_);
    ImGui::TextUnformatted(help);
    ImGui::SetWindowFontScale(font_scale_);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
}

bool SsxSettingsDialog::BeginRows(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.52f);
  ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 0.48f);
  return true;
}

bool SsxSettingsDialog::Combo(const char* label, const char* cvar, const Choice* choices, size_t count) {
  std::string current = rex::cvar::GetFlagByName(cvar);
  const char* preview = current.c_str();
  for (size_t i = 0; i < count; ++i) {
    if (current == choices[i].value) preview = choices[i].label;
  }
  bool changed = false;
  if (ImGui::BeginCombo(label, preview)) {
    for (size_t i = 0; i < count; ++i) {
      if (ImGui::Selectable(choices[i].label, current == choices[i].value)) {
        Set(cvar, choices[i].value);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

void SsxSettingsDialog::Checkbox(const char* label, const char* cvar) {
  bool value = rex::cvar::GetFlagByName(cvar) == "true";
  if (ImGui::Checkbox(label, &value)) Set(cvar, value ? "true" : "false");
}

void SsxSettingsDialog::DrawFps() {
  const ImGuiIO& io = ImGui::GetIO();
  if (body_font) ImGui::PushFont(body_font);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.08f, 0.6f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
  ImGui::SetNextWindowPos(ImVec2(12, 12));
  ImGui::Begin("##ssx_fps", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoNav);
  ImGui::SetWindowFontScale(body_font ? 0.8f * UiScale(io) : std::max(1.0f, io.DisplaySize.y / 800.0f));
  ImGui::TextColored(kAccent, "%.0f", SsxCurrentFps());
  ImGui::SameLine();
  ImGui::TextColored(kTextDim, "FPS");
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  if (body_font) ImGui::PopFont();
}

void SsxSettingsDialog::DrawBinds() {
  if (!ImGui::BeginTable("##binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    return;
  }
  ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 0.40f);
  ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthStretch, 0.32f);
  ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthStretch, 0.28f);
  for (const Bind& bind : kBinds) {
    ImGui::PushID(bind.cvar);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(bind.label);
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (capturing_ == bind.cvar) {
      ImGui::TextColored(kWarn, "Press a key...");
    } else {
      std::string keys = rex::cvar::GetFlagByName(bind.cvar);
      ImGui::TextColored(kAccent, "%s", keys.empty() ? "-" : keys.c_str());
    }
    ImGui::TableNextColumn();
    if (ImGui::Button("Change")) {
      capturing_ = bind.cvar;
      for (int vk = 8; vk < 255; ++vk) GetAsyncKeyState(vk);  // drop stale "pressed" bits
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      if (const auto* info = rex::cvar::GetFlagInfo(bind.cvar)) Set(bind.cvar, info->default_value);
    }
    ImGui::PopID();
  }
  ImGui::EndTable();

  if (capturing_.empty()) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    capturing_.clear();
    return;
  }
  for (int vk = 8; vk < 255; ++vk) {
    if (vk == VK_ESCAPE || IsModifier(vk) || !(GetAsyncKeyState(vk) & 1)) continue;
    std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
    if (!name.empty()) {
      Set(capturing_.c_str(), name);
      capturing_.clear();
      return;
    }
  }
}

void SsxSettingsDialog::DrawDisplayTab() {
  if (!BeginRows("##display")) return;
  Row("Fullscreen", nullptr);
  Checkbox("##fullscreen", "fullscreen");
  Row("Frame rate", "Gameplay keeps real-time speed at any rate.");
  Combo("##framerate", "ssx_framerate", kFramerates, std::size(kFramerates));
  Row("Keep 16:9 aspect ratio", "Black bars instead of stretching on other window shapes.");
  Checkbox("##letterbox", "present_letterbox");
  Row("Show FPS counter", nullptr);
  Checkbox("##fps", "ssx_show_fps");
  ImGui::EndTable();
}

void SsxSettingsDialog::DrawGraphicsTab() {
  if (!BeginRows("##graphics")) return;
  Row("Resolution scale", "Nx draws N x N times as many pixels: sharper, but much heavier on the GPU.");
  Combo("##scale", "resolution_scale", kScales, std::size(kScales));

  Row("Upscaling", "How the image is scaled to your screen. FSR is sharpest at 1x.");
  Combo("##upscaling", "present_effect", kUpscaling, std::size(kUpscaling));
  std::string effect = rex::cvar::GetFlagByName("present_effect");
  if (effect == "fsr" || effect == "cas") {
    // FSR: "sharpness reduction" 0 (sharpest) .. 2 stops. CAS: additional sharpness 0 .. 1.
    const char* cvar = effect == "fsr" ? "present_fsr_sharpness_reduction" : "present_cas_additional_sharpness";
    float raw = std::strtof(rex::cvar::GetFlagByName(cvar).c_str(), nullptr);
    int sharpness = effect == "fsr" ? int(std::lround((1.0f - raw / 2.0f) * 100.0f)) : int(std::lround(raw * 100.0f));
    Row("Sharpness", nullptr);
    if (ImGui::SliderInt("##sharpness", &sharpness, 0, 100, "%d%%")) {
      float value = effect == "fsr" ? (1.0f - sharpness / 100.0f) * 2.0f : sharpness / 100.0f;
      Set(cvar, std::to_string(value));
    }
  }

  Row("Anti-aliasing", "Smooths jagged edges after rendering.");
  Combo("##aa", "swap_post_effect", kAntiAliasing, std::size(kAntiAliasing));
  Row("Texture filtering", "Sharper textures at steep angles.");
  Combo("##aniso", "anisotropic_override", kAniso, std::size(kAniso));
  Row("3D render size", "High is the sharpest size the game's renderer supports without glitches.");
  Combo("##render", "ssx_render_resolution", kRenderSizes, std::size(kRenderSizes));
  ImGui::EndTable();
}

void SsxSettingsDialog::DrawAudioTab() {
  if (!BeginRows("##audio")) return;
  Row("Master volume", nullptr);
  int volume = REXCVAR_GET(ssx_volume);
  if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
    Set("ssx_volume", std::to_string(volume));
  }
  ImGui::EndTable();
}

void SsxSettingsDialog::DrawControlsTab() {
  if (BeginRows("##controls")) {
    Row("Keyboard controls", "Play without a controller.");
    Checkbox("##mnk", "mnk_mode");
    Row("Controller vibration", nullptr);
    Checkbox("##vibration", "vibration");
    Row("Steering deadzone", "Ignore small stick movements.");
    float deadzone =
        std::strtof(rex::cvar::GetFlagByName("left_stick_deadzone_percentage").c_str(), nullptr) * 100.0f;
    if (ImGui::SliderFloat("##deadzone", &deadzone, 0.0f, 50.0f, "%.0f%%")) {
      Set("left_stick_deadzone_percentage", std::to_string(deadzone / 100.0f));
    }
    ImGui::EndTable();
  }
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::SeparatorText("KEYBOARD BINDINGS");
  ImGui::PopStyleColor();
  // Long list: scroll inside the tab so the window keeps its size.
  ImGui::BeginChild("##bindscroll", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 11.0f), false);
  DrawBinds();
  ImGui::EndChild();
}

// Controller: Back + Start toggles the menu; while it is open the pad drives
// ImGui's gamepad navigation, LB/RB switch tabs and B closes it.
void SsxSettingsDialog::PollPad(ImGuiIO& io) {
  rex::input::X_INPUT_STATE state = {};
  uint16_t buttons = input_ && SsxReadPadForUI(input_, &state) ? uint16_t(state.gamepad.buttons) : 0;
  uint16_t pressed = buttons & ~pad_buttons_;
  pad_buttons_ = buttons;
  if ((buttons & (kPadStart | kPadBack)) == (kPadStart | kPadBack) && (pressed & (kPadStart | kPadBack))) {
    Toggle();
    return;
  }
  if (!open_) return;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  for (const auto& k : kPadKeys) io.AddKeyEvent(k.key, (buttons & k.mask) != 0);
  auto stick = [&](ImGuiKey negative, ImGuiKey positive, int16_t raw) {
    float v = std::clamp(raw / 32767.0f, -1.0f, 1.0f);
    float n = std::max(0.0f, (-v - 0.3f) / 0.7f), p = std::max(0.0f, (v - 0.3f) / 0.7f);
    io.AddKeyAnalogEvent(negative, n > 0.0f, n);
    io.AddKeyAnalogEvent(positive, p > 0.0f, p);
  };
  stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, int16_t(state.gamepad.thumb_lx));
  stick(ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, int16_t(state.gamepad.thumb_ly));

  if (pressed & (kPadLb | kPadRb)) {
    int n = int(std::size(kTabs));
    select_tab_ = kTabs[(current_tab_ + ((pressed & kPadRb) ? 1 : n - 1)) % n];
  }
  if ((pressed & kPadB) && capturing_.empty() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
    Toggle();
  }
}

int SsxSettingsDialog::TabFlags(const char* name) {
  if (select_tab_.empty() || select_tab_ != name) return 0;
  select_tab_.clear();
  return ImGuiTabItemFlags_SetSelected;
}

void SsxSettingsDialog::OnDraw(ImGuiIO& io) {
  if (REXCVAR_GET(ssx_show_fps)) DrawFps();
  PollPad(io);
  if (!open_) return;

  const float s = UiScale(io);
  font_scale_ = body_font ? s : std::max(1.0f, io.DisplaySize.y / 800.0f);
  if (body_font) ImGui::PushFont(body_font);
  PushStyle(s);

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(1180.0f * s, 0.0f), ImGuiCond_Always);
  ImGui::Begin("##ssx_settings", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::SetWindowFontScale(font_scale_);

  // Header: title left, close hint right.
  if (title_font) ImGui::PushFont(title_font);
  ImGui::TextColored(kAccent, "SSX");
  ImGui::SameLine();
  ImGui::TextUnformatted("SETTINGS");
  if (title_font) ImGui::PopFont();
  ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("F1 / Esc  -  Back + Start").x);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("F1 / Esc  -  Back + Start");
  ImGui::Spacing();

  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("  Display  ", nullptr, TabFlags("Display"))) {
      current_tab_ = 0;
      DrawDisplayTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("  Graphics  ", nullptr, TabFlags("Graphics"))) {
      current_tab_ = 1;
      DrawGraphicsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("  Audio  ", nullptr, TabFlags("Audio"))) {
      current_tab_ = 2;
      DrawAudioTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("  Controls  ", nullptr, TabFlags("Controls"))) {
      current_tab_ = 3;
      DrawControlsTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  // Footer.
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (restart_needed_) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kWarn, "Some changes apply after a restart.");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("Restart now").x -
                    ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::Button("Restart now")) restart_();
  } else {
    ImGui::TextDisabled("Changes are saved automatically.");
  }

  bool close = capturing_.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape);
  ImGui::End();
  PopStyle();
  if (body_font) ImGui::PopFont();
  if (close) Toggle();
}
