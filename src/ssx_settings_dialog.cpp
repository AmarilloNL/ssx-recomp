// ssx - in-game settings menu (F1) and optional FPS counter.
//
// Every control edits a cvar and the config is saved immediately, so the next
// launch starts with the same settings. The menu has its own look (fonts and
// colours are pushed only around its windows, not the SDK overlays).

#include "ssx_settings_dialog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#else
#include "ssx_game_path.h"
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include "ssx_frame_stats.h"
#include "ssx_input_lock.h"
#include "ssx_settings.h"
#include "ssx_snowflakes.h"

void SsxCheatsGiveCredits();  // ssx_cheats.cpp

REXCVAR_DEFINE_BOOL(ssx_show_fps, false, "SSX", "Show the frame rate in the top-left corner");

namespace {

using Choice = SsxSettingsDialog::Choice;

// Fonts are rasterized for a 1440p screen and scaled to the actual height.
constexpr float kFontDesignHeight = 1440.0f;
ImFont* body_font = nullptr;
ImFont* title_font = nullptr;

// The launcher's colours: deep navy, a blue for what's on and selected, a light blue for labels.
const ImVec4 kBg(0.051f, 0.078f, 0.173f, 0.98f);
const ImVec4 kRail(0.035f, 0.055f, 0.133f, 1.0f);
const ImVec4 kSurface(0.075f, 0.110f, 0.235f, 1.0f);
const ImVec4 kSurface2(0.106f, 0.149f, 0.306f, 1.0f);
const ImVec4 kSurface3(0.141f, 0.196f, 0.384f, 1.0f);
const ImVec4 kField(0.043f, 0.067f, 0.153f, 1.0f);
const ImVec4 kBorder(0.145f, 0.196f, 0.369f, 1.0f);
const ImVec4 kBlue(0.204f, 0.471f, 0.965f, 1.0f);
const ImVec4 kBlueHover(0.329f, 0.569f, 1.0f, 1.0f);
const ImVec4 kAccent(0.463f, 0.675f, 1.0f, 1.0f);
const ImVec4 kText(0.941f, 0.957f, 1.0f, 1.0f);
const ImVec4 kTextDim(0.667f, 0.710f, 0.824f, 1.0f);
const ImVec4 kFaint(0.424f, 0.471f, 0.612f, 1.0f);
const ImVec4 kWarn(1.0f, 0.718f, 0.302f, 1.0f);
const ImVec4 kGood(0.180f, 0.831f, 0.655f, 1.0f);

ImU32 Col(const ImVec4& c, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

const Choice kFramerates[] = {{"30 fps  (original)", "30"}, {"60 fps", "60"},   {"70 fps", "70"},
                              {"72 fps  (half of 144)", "72"}, {"90 fps", "90"}, {"120 fps", "120"},
                              {"144 fps", "144"},           {"165 fps", "165"}, {"240 fps", "240"},
                              {"300 fps", "300"},           {"Match main display", "0"}};
// 3x: only rates a fast GPU holds there (see DrawScaleWarning).
const Choice kFramerates3x[] = {{"30 fps  (original)", "30"}, {"40 fps", "40"}, {"50 fps", "50"},
                                {"55 fps", "55"},             {"60 fps", "60"}};
const Choice kScales[] = {{"1x  (fastest)", "1"},
                          {"2x  (sharp, recommended)", "2"},
                          {"3x  (about 4K, 60 fps at most)", "3"}};
// ReGPU draws at any size (regpu_resolution in the plugin), changed while the game runs.
const Choice kResolutionsReGpu[] = {{"Match my screen", "screen"},
                                    {"1280 x 720  (720p)", "1280x720"},
                                    {"1920 x 1080  (1080p)", "1920x1080"},
                                    {"2560 x 1440  (1440p / 2K)", "2560x1440"},
                                    {"3840 x 2160  (4K)", "3840x2160"}};
const Choice kRenderers[] = {{"ReGPU  (recommended)", "regpu"}, {"Standard", "standard"}};
const Choice kUpscaling[] = {{"Bilinear  (soft)", "bilinear"},
                             {"AMD CAS  (sharpen)", "cas"},
                             {"AMD FSR 1  (upscale + sharpen)", "fsr"}};
const Choice kRenderSizes[] = {{"Original  1120 x 584", "original"}, {"High  1200 x 640", "high"}};
const Choice kAntiAliasing[] = {{"Off", "none"}, {"FXAA", "fxaa"}, {"FXAA  (strong)", "fxaa_extreme"}};
const Choice kAniso[] = {{"Game default", "-1"}, {"Off", "0"}, {"2x", "2"},
                         {"4x", "3"},            {"8x", "4"},  {"16x", "5"}};
const Choice kTrickyCheats[] = {{"Normal", "0"}, {"Always Tricky", "1"}, {"Always Super Tricky", "2"}, {"Never", "3"}};

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

const char* const kTabs[] = {"Display", "Graphics", "Audio", "Controls", "Profile", "Snowflakes", "Cheats"};

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

#ifdef _WIN32
bool IsModifier(int vk) { return (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU); }

// Clears the "pressed since last call" bits, so only keys pressed from now on count.
void ForgetKeyPresses() {
  for (int vk = 8; vk < 255; ++vk) GetAsyncKeyState(vk);
}

// A key pressed since the last call (not Escape or a modifier), as a virtual key; 0 if none.
int PressedKey() {
  for (int vk = 8; vk < 255; ++vk) {
    if (vk == VK_ESCAPE || IsModifier(vk) || !(GetAsyncKeyState(vk) & 1)) continue;
    return vk;
  }
  return 0;
}
#else
void ForgetKeyPresses() {}

// The same from ImGui's keys (Linux has no key state to poll): letters, digits, arrows and the
// usual others, as Windows virtual keys (what the keybind cvars name).
int PressedKey() {
  for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) return 'A' + (k - ImGuiKey_A);
  }
  for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k) {
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) return '0' + (k - ImGuiKey_0);
  }
  for (int k = ImGuiKey_Keypad0; k <= ImGuiKey_Keypad9; ++k) {
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) return 0x60 + (k - ImGuiKey_Keypad0);
  }
  for (int k = ImGuiKey_F2; k <= ImGuiKey_F12; ++k) {
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) return 0x71 + (k - ImGuiKey_F2);
  }
  const struct {
    ImGuiKey key;
    int vk;
  } kOthers[] = {{ImGuiKey_Space, 0x20},     {ImGuiKey_Enter, 0x0D},      {ImGuiKey_Tab, 0x09},
                 {ImGuiKey_Backspace, 0x08}, {ImGuiKey_LeftArrow, 0x25},  {ImGuiKey_UpArrow, 0x26},
                 {ImGuiKey_RightArrow, 0x27}, {ImGuiKey_DownArrow, 0x28}, {ImGuiKey_Insert, 0x2D},
                 {ImGuiKey_Delete, 0x2E},    {ImGuiKey_Home, 0x24},       {ImGuiKey_End, 0x23},
                 {ImGuiKey_PageUp, 0x21},    {ImGuiKey_PageDown, 0x22}};
  for (const auto& other : kOthers) {
    if (ImGui::IsKeyPressed(other.key, false)) return other.vk;
  }
  return 0;
}
#endif

// Xbox gamertag characters: letters, digits and spaces.
int NameCharFilter(ImGuiInputTextCallbackData* data) {
  ImWchar c = data->EventChar;
  return c < 128 && (std::isalnum(c) || c == ' ') ? 0 : 1;
}

// The system's file picker; the chosen image as UTF-8, or empty if cancelled.
std::string ChoosePicture() {
#ifndef _WIN32
  return SsxPickFile("Choose a picture", "Images (PNG, JPG, BMP, GIF)", "*.png *.jpg *.jpeg *.bmp *.gif");
#else
  wchar_t file[MAX_PATH] = L"";
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetForegroundWindow();
  ofn.lpstrFilter = L"Images (PNG, JPG, BMP, GIF)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return {};
  std::string utf8(WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr), '\0');
  WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8.data(), int(utf8.size()), nullptr, nullptr);
  utf8.pop_back();  // terminator
  return utf8;
#endif
}

float UiScale(const ImGuiIO& io) { return std::clamp(io.DisplaySize.y / kFontDesignHeight, 0.5f, 2.0f); }

// Pushes the menu's look; PopStyle() undoes it.
int pushed_colors = 0, pushed_vars = 0;

void PushStyle(float s) {
  const struct {
    ImGuiCol idx;
    ImVec4 color;
  } colors[] = {
      {ImGuiCol_WindowBg, kBg},
      {ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)},
      {ImGuiCol_Border, kBorder},
      {ImGuiCol_Text, kText},
      {ImGuiCol_TextDisabled, kFaint},
      {ImGuiCol_FrameBg, kField},
      {ImGuiCol_FrameBgHovered, kSurface2},
      {ImGuiCol_FrameBgActive, kSurface3},
      {ImGuiCol_Button, kSurface2},
      {ImGuiCol_ButtonHovered, kSurface3},
      {ImGuiCol_ButtonActive, kBlue},
      {ImGuiCol_Header, kSurface3},
      {ImGuiCol_HeaderHovered, ImVec4(kBlue.x, kBlue.y, kBlue.z, 0.55f)},
      {ImGuiCol_HeaderActive, kBlue},
      {ImGuiCol_CheckMark, ImVec4(1, 1, 1, 1)},
      {ImGuiCol_SliderGrab, kBlue},
      {ImGuiCol_SliderGrabActive, kBlueHover},
      {ImGuiCol_Separator, kBorder},
      {ImGuiCol_PopupBg, ImVec4(0.059f, 0.090f, 0.200f, 0.99f)},
      {ImGuiCol_ModalWindowDimBg, ImVec4(0.02f, 0.03f, 0.08f, 0.6f)},
      {ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.025f)},
      {ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0)},
      {ImGuiCol_ScrollbarGrab, kSurface3},
      {ImGuiCol_ScrollbarGrabHovered, kBlue},
      {ImGuiCol_ScrollbarGrabActive, kBlueHover},
      {ImGuiCol_NavCursor, kBlueHover},
  };
  for (const auto& c : colors) ImGui::PushStyleColor(c.idx, c.color);
  pushed_colors = int(std::size(colors));
  const struct {
    ImGuiStyleVar idx;
    float value;
  } floats[] = {
      {ImGuiStyleVar_WindowRounding, 16.0f * s}, {ImGuiStyleVar_ChildRounding, 12.0f * s},
      {ImGuiStyleVar_FrameRounding, 8.0f * s},   {ImGuiStyleVar_PopupRounding, 8.0f * s},
      {ImGuiStyleVar_GrabRounding, 8.0f * s},    {ImGuiStyleVar_WindowBorderSize, 1.0f * s},
      {ImGuiStyleVar_ChildBorderSize, 1.0f * s}, {ImGuiStyleVar_FrameBorderSize, 1.0f * s},
      {ImGuiStyleVar_PopupBorderSize, 1.0f * s}, {ImGuiStyleVar_ScrollbarSize, 10.0f * s},
      {ImGuiStyleVar_ScrollbarRounding, 8.0f * s}, {ImGuiStyleVar_GrabMinSize, 18.0f * s},
  };
  for (const auto& v : floats) ImGui::PushStyleVar(v.idx, v.value);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26.0f * s, 22.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * s, 9.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f * s, 12.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f * s, 10.0f * s));
  pushed_vars = int(std::size(floats)) + 4;
}

void PopStyle() {
  ImGui::PopStyleVar(pushed_vars);
  ImGui::PopStyleColor(pushed_colors);
}

// The sidebar's line icons, one per page (kTabs order), in a square of side 2r around c.
void DrawPageGlyph(ImDrawList* d, int page, ImVec2 c, float r, ImU32 col, float t) {
  auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
  switch (page) {
    case 0:  // Display: a screen on a stand
      d->AddRect(P(-0.9f, -0.75f), P(0.9f, 0.45f), col, 0.15f * r, 0, t);
      d->AddLine(P(0.0f, 0.45f), P(0.0f, 0.8f), col, t);
      d->AddLine(P(-0.45f, 0.82f), P(0.45f, 0.82f), col, t);
      break;
    case 1: {  // Graphics: peaks
      ImVec2 pts[] = {P(-0.95f, 0.7f), P(-0.3f, -0.55f), P(0.1f, 0.15f), P(0.4f, -0.2f), P(0.95f, 0.7f)};
      d->AddPolyline(pts, 5, col, ImDrawFlags_Closed, t);
      d->AddCircle(P(0.55f, -0.62f), 0.18f * r, col, 0, t);
      break;
    }
    case 2: {  // Audio: a speaker and two waves
      ImVec2 pts[] = {P(-0.9f, -0.3f), P(-0.5f, -0.3f), P(-0.05f, -0.75f), P(-0.05f, 0.75f), P(-0.5f, 0.3f), P(-0.9f, 0.3f)};
      d->AddPolyline(pts, 6, col, ImDrawFlags_Closed, t);
      d->PathArcTo(P(0.05f, 0.0f), 0.42f * r, -0.9f, 0.9f);
      d->PathStroke(col, 0, t);
      d->PathArcTo(P(0.05f, 0.0f), 0.8f * r, -0.9f, 0.9f);
      d->PathStroke(col, 0, t);
      break;
    }
    case 3:  // Controls: a pad
      d->AddRect(P(-0.95f, -0.5f), P(0.95f, 0.55f), col, 0.45f * r, 0, t);
      d->AddLine(P(-0.5f, -0.18f), P(-0.5f, 0.22f), col, t);
      d->AddLine(P(-0.7f, 0.02f), P(-0.3f, 0.02f), col, t);
      d->AddCircleFilled(P(0.38f, -0.1f), 0.11f * r, col);
      d->AddCircleFilled(P(0.6f, 0.14f), 0.11f * r, col);
      break;
    case 4:  // Profile: head and shoulders
      d->AddCircle(P(0.0f, -0.38f), 0.36f * r, col, 0, t);
      d->PathArcTo(P(0.0f, 0.95f), 0.8f * r, 3.14159f * 1.05f, 3.14159f * 1.95f);
      d->PathStroke(col, 0, t);
      break;
    case 5:  // Snowflakes
      for (int i = 0; i < 3; ++i) {
        float a = i * 3.14159f / 3.0f;
        d->AddLine(P(-0.85f * std::cos(a), -0.85f * std::sin(a)), P(0.85f * std::cos(a), 0.85f * std::sin(a)), col, t);
      }
      break;
    default: {  // Cheats: a star
      ImVec2 pts[10];
      for (int i = 0; i < 10; ++i) {
        float a = -3.14159f / 2.0f + i * 3.14159f / 5.0f, k = (i % 2) ? 0.42f : 0.95f;
        pts[i] = P(k * std::cos(a), k * std::sin(a) + 0.08f);
      }
      d->AddPolyline(pts, 10, col, ImDrawFlags_Closed, t);
      break;
    }
  }
}

}  // namespace

void SsxSettingsDialog::ConfigureFonts(ImFontAtlas* atlas) {
  // Segoe UI ships with Windows, Noto Sans with most Linux desktops; ImGui's built-in font without them.
#ifdef _WIN32
  const char* body = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* title = "C:\\Windows\\Fonts\\segoeuib.ttf";
#else
  const char* body = "/usr/share/fonts/noto/NotoSans-Regular.ttf";
  const char* title = "/usr/share/fonts/noto/NotoSans-Bold.ttf";
#endif
  std::error_code ec;
  if (std::filesystem::exists(body, ec)) {
    body_font = atlas->AddFontFromFileTTF(body, 30.0f);
  }
  if (std::filesystem::exists(title, ec)) {
    title_font = atlas->AddFontFromFileTTF(title, 46.0f);
  }
}

SsxSettingsDialog::SsxSettingsDialog(rex::ui::ImGuiDrawer* drawer, rex::input::InputSystem* input,
                                     std::filesystem::path config_path, std::function<void()> restart,
                                     std::function<void()> quit)
    : ImGuiDialog(drawer),
      input_(input),
      config_path_(std::move(config_path)),
      restart_(std::move(restart)),
      quit_(std::move(quit)) {}

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
  // (ReGPU switches VSync at once; the standard renderer makes its swap chain at startup only.)
  bool live = std::string_view(cvar) == "ssx_vsync" && SsxUsingReGpu();
  if ((info && info->lifecycle != rex::cvar::Lifecycle::kHotReload && !live) ||
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
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.56f);
  ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 0.44f);
  return true;
}

// A rounded card with a small title, as tall as what's in it. With rows, it starts the settings
// table too and returns whether to draw the rows (then end them with ImGui::EndTable()); without,
// whether to draw the content. EndCard() in both cases.
// The card is a one-column table in the page (its width keeps what's in it inside the card), with the
// background drawn behind it once its height is known. Not a child window: a controller can't move
// into child windows nested in the page, so their settings couldn't be reached.
bool SsxSettingsDialog::BeginCard(const char* title, bool rows) {
  const float s = ui_scale_;
  const ImVec2 pad(24.0f * s, 18.0f * s);
  ImGui::PushID(title);
  card_start_ = ImGui::GetCursorScreenPos();
  card_width_ = ImGui::GetContentRegionAvail().x - 12.0f * s;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->ChannelsSplit(2);
  draw->ChannelsSetCurrent(1);  // the content; the background goes to channel 0 in EndCard()
  ImGui::SetCursorScreenPos(ImVec2(card_start_.x + pad.x, card_start_.y + pad.y));
  card_table_ = ImGui::BeginTable("##card", 1, ImGuiTableFlags_None, ImVec2(card_width_ - 2.0f * pad.x, 0.0f));
  if (!card_table_) return false;
  ImGui::TableNextColumn();
  if (title_font) ImGui::PushFont(title_font);
  ImGui::SetWindowFontScale(font_scale_ * (title_font ? 0.5f : 0.85f));
  ImGui::TextColored(kAccent, "%s", title);
  ImGui::SetWindowFontScale(font_scale_);
  if (title_font) ImGui::PopFont();
  ImGui::Dummy(ImVec2(0.0f, 2.0f * s));
  return !rows || BeginRows("##rows");
}

void SsxSettingsDialog::EndCard() {
  const float s = ui_scale_;
  float bottom = card_start_.y;
  if (card_table_) {
    ImGui::EndTable();
    bottom = ImGui::GetItemRectMax().y + 18.0f * s;
  }
  card_table_ = false;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->ChannelsSetCurrent(0);
  const ImVec2 a = card_start_, b(card_start_.x + card_width_, bottom);
  const float round = ImGui::GetStyle().ChildRounding;
  draw->AddRectFilled(a, b, Col(kSurface), round);
  draw->AddRect(a, b, Col(kBorder), round, 0, 1.0f * s);
  draw->ChannelsMerge();
  ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
  ImGui::Dummy(ImVec2(card_width_, 4.0f * s));
  ImGui::PopID();
}

// An on/off switch (the menu's checkbox): a pill that turns blue with the knob on the right.
bool SsxSettingsDialog::Switch(const char* id, bool* value) {
  const float s = ui_scale_, frame = ImGui::GetFrameHeight();
  const float h = frame * 0.8f, w = h * 1.85f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  // (EnableNav: an invisible button is left out of controller navigation unless asked.)
  bool pressed = ImGui::InvisibleButton(id, ImVec2(w, frame), ImGuiButtonFlags_EnableNav);
  if (pressed) *value = !*value;
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* d = ImGui::GetWindowDrawList();
  ImVec2 a(p.x, p.y + (frame - h) * 0.5f), b(a.x + w, a.y + h);
  if (*value) {
    d->AddRectFilled(a, b, Col(hovered ? kBlueHover : kBlue), h * 0.5f);
  } else {
    d->AddRectFilled(a, b, Col(hovered ? kSurface3 : kSurface2), h * 0.5f);
    d->AddRect(a, b, Col(kBorder), h * 0.5f, 0, 1.0f * s);
  }
  float r = h * 0.5f - 4.0f * s;
  ImVec2 knob(*value ? b.x - h * 0.5f : a.x + h * 0.5f, a.y + h * 0.5f);
  d->AddCircleFilled(knob, r, *value ? IM_COL32_WHITE : Col(kTextDim));
  if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) {
    d->AddRect(ImVec2(a.x - 3.0f * s, a.y - 3.0f * s), ImVec2(b.x + 3.0f * s, b.y + 3.0f * s), Col(kBlueHover),
               h * 0.5f + 3.0f * s, 0, 2.0f * s);
  }
  return pressed;
}

// A button in the blue of the main action, or in the card colour.
bool SsxSettingsDialog::Button(const char* label, bool primary) {
  if (primary) {
    ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlueHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kBlueHover);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.72f, 1.0f, 0.6f));
  }
  bool pressed = ImGui::Button(label);
  if (primary) ImGui::PopStyleColor(4);
  return pressed;
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

bool SsxSettingsDialog::Checkbox(const char* label, const char* cvar) {
  bool value = rex::cvar::GetFlagByName(cvar) == "true";
  if (!Switch(label, &value)) return false;
  Set(cvar, value ? "true" : "false");
  return true;
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
    if (Button("Change", false)) {
      capturing_ = bind.cvar;
      ForgetKeyPresses();
    }
    ImGui::SameLine();
    if (Button("Reset", false)) {
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
  if (int vk = PressedKey()) {
    std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
    if (!name.empty()) {
      Set(capturing_.c_str(), name);
      capturing_.clear();
    }
  }
}

void SsxSettingsDialog::DrawDisplayTab() {
  if (BeginCard("FRAME RATE")) {
    // 3x has its own choices (and setting), see DrawScaleWarning.
    int scale = std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str());
    if (scale >= 3 && !SsxUsingReGpu()) {
      Row("Frame rate", "Locked to 30-60 fps at 3x resolution scale (Graphics page).");
      Combo("##framerate", "ssx_framerate_3x", kFramerates3x, std::size(kFramerates3x));
    } else {
      Row("Frame rate", "Gameplay keeps real-time speed at any rate.");
      Combo("##framerate", "ssx_framerate", kFramerates, std::size(kFramerates));
    }
    ImGui::EndTable();
  }
  EndCard();
  if (BeginCard("SCREEN")) {
    Row("Fullscreen", "Off: play in a window.");
    Checkbox("##fullscreen", "fullscreen");
    DrawScreenRows();
    ImGui::EndTable();
  }
  EndCard();
  if (BeginCard("ON SCREEN")) {
    Row("Show FPS counter", "In the top-left corner.");
    Checkbox("##fps", "ssx_show_fps");
    Row("Boost meter in freeride", "Shows the score panel with the boost and tricky meters in freeride. From the next ride.");
    Checkbox("##freeride_hud", "ssx_freeride_hud");
    Row("Gear by type", "View More lists your gear by type (wingsuits, armour, ...), then the highest level first.");
    Checkbox("##sort_gear", "ssx_sort_gear");
    ImGui::EndTable();
  }
  EndCard();
}

// The Screen card's rows after Fullscreen.
void SsxSettingsDialog::DrawScreenRows() {
  Row("VSync", SsxUsingReGpu()
                    ? "No tearing on screens without G-Sync/FreeSync, like handhelds. Works with any frame rate."
                    : "No tearing on screens without G-Sync/FreeSync, like handhelds. Applies after a restart.");
  Checkbox("##vsync", "ssx_vsync");
  if (!SsxUsingReGpu()) {  // (ReXGlue's presenter setting: not used by ReGPU)
    Row("Keep 16:9 aspect ratio", "Black bars instead of stretching on other window shapes.");
    Checkbox("##letterbox", "present_letterbox");
  }
}

void SsxSettingsDialog::DrawGraphicsTab() {
  if (BeginCard("RENDERER")) {
    DrawRendererRows();
    ImGui::EndTable();
  }
  EndCard();
  if (BeginCard("IMAGE")) {
    DrawImageRows();
    ImGui::EndTable();
  }
  EndCard();
}

void SsxSettingsDialog::DrawRendererRows() {
#ifdef _WIN32
  const bool scaling = !SsxUsingReGpu();
#else
  const bool scaling = false;  // Linux: the standard renderer draws at 1x, scaled bilinear (ssx_settings.cpp)
#endif
  Row("Renderer", SsxUsingReGpu()
                      ? "ReGPU draws the game faster and sharper, up to 4K. If something looks wrong, "
                        "Standard is the previous renderer (and please report it). Applies after a restart."
                      : "We recommend ReGPU: faster and sharper, up to 4K. Standard is the previous "
                        "renderer. Applies after a restart.");
  Combo("##renderer", "ssx_renderer", kRenderers, std::size(kRenderers));
  if (SsxUsingReGpu()) {
    Row("Resolution", "How many pixels the game is drawn with: higher is sharper, but heavier on the GPU.");
    Combo("##resolution", "regpu_resolution", kResolutionsReGpu, std::size(kResolutionsReGpu));
  } else if (scaling) {
    Row("Resolution scale", "N x N times as many pixels: sharper, but heavier on the GPU.");
  }
  bool heavy = scaling && std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()) >= 3;
  if (scaling && Combo("##scale", "resolution_scale", kScales, std::size(kScales)) && !heavy &&
      std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()) >= 3) {
    ImGui::OpenPopup("##scale_warning");
  }
  if (heavy) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("Frame rate locked. We recommend 2x.");
    ImGui::PopStyleColor();
  }
  DrawScaleWarning();
}

void SsxSettingsDialog::DrawImageRows() {
#ifdef _WIN32
  const bool scaling = !SsxUsingReGpu();
#else
  const bool scaling = false;
#endif
  std::string effect = rex::cvar::GetFlagByName("present_effect");
  if (scaling) {
    Row("Upscaling", "How the image is scaled to your screen. FSR is sharpest at 1x.");
    Combo("##upscaling", "present_effect", kUpscaling, std::size(kUpscaling));
  }
  if (scaling && (effect == "fsr" || effect == "cas")) {
    // FSR: "sharpness reduction" 0 (sharpest) .. 2 stops. CAS: additional sharpness 0 .. 1.
    const char* cvar = effect == "fsr" ? "present_fsr_sharpness_reduction" : "present_cas_additional_sharpness";
    float raw = std::strtof(rex::cvar::GetFlagByName(cvar).c_str(), nullptr);
    int sharpness = effect == "fsr" ? int(std::lround((1.0f - raw / 2.0f) * 100.0f)) : int(std::lround(raw * 100.0f));
    Row("Sharpness", "How much the image is sharpened.");
    if (ImGui::SliderInt("##sharpness", &sharpness, 0, 100, "%d%%")) {
      float value = effect == "fsr" ? (1.0f - sharpness / 100.0f) * 2.0f : sharpness / 100.0f;
      Set(cvar, std::to_string(value));
    }
  }

  if (!SsxUsingReGpu()) {
    Row("Anti-aliasing", "Smooths jagged edges after rendering.");
    Combo("##aa", "swap_post_effect", kAntiAliasing, std::size(kAntiAliasing));
  }
  Row("Texture filtering", "Sharper textures at steep angles.");
  Combo("##aniso", "anisotropic_override", kAniso, std::size(kAniso));
  Row("3D render size", "High is the sharpest size the game's renderer supports without glitches.");
  Combo("##render", "ssx_render_resolution", kRenderSizes, std::size(kRenderSizes));
}

// Shown when 3x is picked.
void SsxSettingsDialog::DrawScaleWarning() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, 0.0f));
  if (!ImGui::BeginPopupModal("##scale_warning", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::SetWindowFontScale(font_scale_);
  ImGui::TextColored(kWarn, "3x: frame rate locked to 60 fps at most");
  ImGui::Spacing();
  ImGui::TextWrapped(
      "3x draws 3600 x 1920 pixels (about 4K). The graphics run through an emulation of the "
      "Xbox 360's graphics chip, including its special video memory, which costs extra work for "
      "every pixel. So even the fastest graphics cards can't go much above 60 fps there: an "
      "RTX 4080 SUPER holds 60 with small dips in heavy spots.");
  ImGui::Spacing();
  ImGui::TextWrapped(
      "At higher frame rates the graphics card runs at its limit all the time: the frame rate keeps "
      "jumping, which feels like stutter, and on some PCs the game can become unstable "
      "(graphics driver hangs or crashes). So the frame rate is locked to 30 to 60 fps "
      "(Display tab).");
  ImGui::Spacing();
  ImGui::TextWrapped(
      "We recommend 2x: it's smooth, and on a 1080p or 1440p screen it looks almost the same.");
  ImGui::Spacing();
  if (ImGui::Button("Use 2x  (recommended)")) {
    Set("resolution_scale", "2");
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Keep it")) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// Quit game (issue #20): asks first, then closes the game the same way as the window's close button.
void SsxSettingsDialog::DrawQuitConfirm() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("##quit_confirm", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::SetWindowFontScale(font_scale_);
  ImGui::TextUnformatted("Quit SSX?");
  ImGui::Spacing();
  ImGui::TextDisabled("Progress the game hasn't saved yet is lost, as on the console.");
  ImGui::Spacing();
  if (ImGui::Button("Quit")) {
    ImGui::CloseCurrentPopup();
    quit_();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
  ImGui::SetItemDefaultFocus();  // a controller lands on Cancel: two quick A presses don't quit
  ImGui::EndPopup();
}

void SsxSettingsDialog::DrawProfileTab() {
  if (BeginCard("PLAYER")) {
    DrawPlayerRows();
    ImGui::EndTable();
  }
  EndCard();
  if (BeginCard("BADGES")) {
    Row("Online-only badges", "Explore (ghost credits, friends' records, uncollected geotags), Gear "
        "(placing geotags) and all Global Events badges need EA's servers. On: they show as earned "
        "(your save isn't changed).");
    Checkbox("##online_badges", "ssx_online_badges");
    ImGui::EndTable();
  }
  EndCard();
}

void SsxSettingsDialog::DrawPlayerRows() {
  Row("Player name", "Shown in the menus instead of User. Letters, digits and spaces.");
  char name[16] = {};
  std::string current = rex::cvar::GetFlagByName("ssx_player_name");
  current.copy(name, sizeof(name) - 1);
  if (ImGui::InputTextWithHint("##name", "User", name, sizeof(name),
                               ImGuiInputTextFlags_CallbackCharFilter, NameCharFilter)) {
    Set("ssx_player_name", name);
  }
  Row("Gamer picture", "The square next to your name, shown at about 64 x 64 pixels: a simple, "
      "centred image looks best. Cropped to a square.");
  std::string picture = rex::cvar::GetFlagByName("ssx_player_picture");
  if (ImGui::Button("Choose...")) {
    if (std::string chosen = ChoosePicture(); !chosen.empty()) Set("ssx_player_picture", chosen);
  }
  if (!picture.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Remove")) Set("ssx_player_picture", "");
    ImGui::TextDisabled("%s", std::filesystem::path(picture).filename().string().c_str());
  }
}

void SsxSettingsDialog::DrawAudioTab() {
  if (BeginCard("SOUND")) {
    Row("Master volume", "All game sound and music.");
    int volume = REXCVAR_GET(ssx_volume);
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
      Set("ssx_volume", std::to_string(volume));
    }
    ImGui::EndTable();
  }
  EndCard();
}

// Snowflakes (offline geotags) collected per drop: the game itself never shows where they're missing.
void SsxSettingsDialog::DrawSnowflakesTab() {
  static double last_shown = -10.0;  // a fresh look-up of the names each time the tab is opened
  if (ImGui::GetTime() - last_shown > 1.0) SsxSnowflakesRefreshNames();
  last_shown = ImGui::GetTime();
  SsxSnowflakes snowflakes = SsxSnowflakeCounts();
  if (snowflakes.ranges.empty()) {
    ImGui::TextColored(kTextDim, "The game hasn't loaded its snowflakes yet.");
    return;
  }
  if (!BeginCard("COLLECTED", false)) {
    EndCard();
    return;
  }
  ImGui::Text("%d of %d snowflakes collected", snowflakes.collected, snowflakes.total);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("From the loaded profile (shown once you're past the title screen). Some drops are raced "
                     "on the same track and share the same snowflakes.");
  ImGui::PopStyleColor();
  ImGui::Spacing();
  if (!ImGui::BeginTable("##snowflakes", 2, ImGuiTableFlags_SizingStretchProp)) {
    EndCard();
    return;
  }
  ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.8f);
  ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthStretch, 0.2f);
  const float indent = ImGui::GetStyle().IndentSpacing;
  for (const auto& range : snowflakes.ranges) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (&range != &snowflakes.ranges.front()) ImGui::Spacing();
    ImGui::TextColored(kAccent, "%s", range.name.c_str());
    ImGui::TableNextColumn();
    if (&range != &snowflakes.ranges.front()) ImGui::Spacing();
    ImGui::TextColored(range.collected >= range.total ? kGood : kText, "%d / %d", range.collected, range.total);
    for (const auto& peak : range.peaks) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Indent(indent);
      ImGui::TextColored(kTextDim, "%s", peak.name.empty() ? "-" : peak.name.c_str());
      ImGui::Unindent(indent);
      for (const auto& drop : peak.drops) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Indent(indent * 2.0f);
        ImGui::TextUnformatted(drop.name.c_str());
        ImGui::Unindent(indent * 2.0f);
        ImGui::TableNextColumn();
        if (drop.total) {
          ImGui::TextColored(drop.collected == drop.total ? kGood : kText, "%d / %d", drop.collected, drop.total);
        } else {
          ImGui::Text("%d", drop.collected);
        }
      }
    }
  }
  ImGui::EndTable();
  EndCard();
}

// Cheats (issue #21, ssx_cheats.cpp). All apply at once except max gear stats, which the game reads when a
// ride starts. Only what is bought and the credits given end up in the save.
void SsxSettingsDialog::DrawCheatsTab() {
  if (BeginCard("RIDING")) {
    Row("Infinite boost",
        "Boost never runs out: the meter goes down as you boost, but never empties. Tricky starts as normal.");
    Checkbox("##cheat_boost", "ssx_cheat_infinite_boost");
    Row("Tricky", "Always Tricky stays in Tricky (never Super Tricky). Never: boost without Tricky.");
    Combo("##cheat_tricky", "ssx_cheat_tricky", kTrickyCheats, std::size(kTrickyCheats));
    Row("Infinite rewinds", "Rewinds (LB) are never used up, also where a drop gives only a few.");
    if (Checkbox("##cheat_rewinds", "ssx_cheat_infinite_rewinds") &&
        rex::cvar::GetFlagByName("ssx_cheat_infinite_rewinds") == "true") {
      Set("ssx_cheat_no_rewinds", "false");
      Set("ssx_cheat_slow_motion", "false");
    }
    Row("No rewinds", "As if every drop's rewinds were used up: LB still slows time down, but doesn't rewind.");
    if (Checkbox("##cheat_no_rewinds", "ssx_cheat_no_rewinds") &&
        rex::cvar::GetFlagByName("ssx_cheat_no_rewinds") == "true") {
      Set("ssx_cheat_infinite_rewinds", "false");
    }
    Row("Slow motion", "Hold LB to slow time down for as long as you hold it. LB doesn't rewind then.");
    if (Checkbox("##cheat_slow_motion", "ssx_cheat_slow_motion") &&
        rex::cvar::GetFlagByName("ssx_cheat_slow_motion") == "true") {
      Set("ssx_cheat_infinite_rewinds", "false");
    }
    Row("Never lose combo", "A wipeout banks your combo (with its bonus) instead of losing it.");
    Checkbox("##cheat_combo", "ssx_cheat_keep_combo");
    Row("Never lose multiplier", "The trick multiplier only goes up during a ride.");
    Checkbox("##cheat_multiplier", "ssx_cheat_keep_multiplier");
    Row("Ride uphill",
        "Ride back up the mountain as far as you like, past the start or off the sides of the course, without "
        "the invisible push back or the slowdown.");
    Checkbox("##cheat_uphill", "ssx_cheat_ride_uphill");
    Row("No reverse camera",
        "The avalanche drops use the normal camera behind you, instead of the one looking back up the "
        "mountain. Turned off during a ride, it applies from the next one.");
    Checkbox("##cheat_no_reverse", "ssx_cheat_no_reverse_camera");
    ImGui::EndTable();
  }
  EndCard();

  if (BeginCard("CREDITS")) {
    Row("Everything is free", "Buying costs nothing. Prices still show; your credits stay as they are.");
    Checkbox("##cheat_credits", "ssx_cheat_infinite_credits");
    Row("Give credits", "Adds 1,000,000 to your own credits, kept like a reward.");
    static int given = 0;
    if (Button("+1,000,000", false)) {
      SsxCheatsGiveCredits();
      ++given;
    }
    if (given) {
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kGood, "added %dx", given);
    }
    ImGui::EndTable();
  }
  EndCard();

  if (BeginCard("GEAR")) {
    Row("Infinite lift (wingsuits)", "A wingsuit flight keeps its lift, and the wingsuit meter stays full.");
    Checkbox("##cheat_lift", "ssx_cheat_infinite_lift");
    Row("Infinite battery life (headlamps)", "The headlamp never goes dark.");
    Checkbox("##cheat_headlamp", "ssx_cheat_infinite_headlamp");
    Row("Infinite battery life (pulse goggles)", "The pulse goggles never run out.");
    Checkbox("##cheat_goggles", "ssx_cheat_infinite_goggles");
    Row("Infinite durability (wingsuit)", "The wingsuit never wears out.");
    Checkbox("##cheat_wingsuit", "ssx_cheat_infinite_wingsuit");
    Row("Infinite durability (armour)", "Armour never wears out.");
    Checkbox("##cheat_armour", "ssx_cheat_infinite_armour");
    Row("Infinite durability (ice axes)", "Ice axes never wear out.");
    Checkbox("##cheat_ice_axes", "ssx_cheat_infinite_ice_axes");
    Row("Infinite tank size (oxygen tanks)", "Oxygen and the oxygen tanks never run out.");
    Checkbox("##cheat_oxygen", "ssx_cheat_infinite_oxygen");
    Row("Infinite cold protection (solar panels)", "The cold never lowers your core temperature.");
    Checkbox("##cheat_warmth", "ssx_cheat_infinite_warmth");
    Row("Max gear stats", "From your next ride: board, wingsuit and other gear at their best.");
    Checkbox("##cheat_max_gear", "ssx_cheat_max_gear");
    ImGui::EndTable();
  }
  EndCard();

  if (BeginCard("JUST FOR FUN")) {
    Row("Big head mode", "Like the suits with that perk.");
    Checkbox("##cheat_big_head", "ssx_cheat_big_head");
    Row("Big hands mode", "Like the suits with that perk.");
    Checkbox("##cheat_big_hands", "ssx_cheat_big_hands");
    Row("Glowing suit", "Your suit's glowing parts light up.");
    Checkbox("##cheat_glowing", "ssx_cheat_glowing");
    ImGui::EndTable();
  }
  EndCard();
  ImGui::TextDisabled("Turn a cheat off and the game plays as before. Your own credits are never spent.");
  ImGui::TextDisabled("Scores, medals and what you buy with a cheat on are kept like any others.");
}

void SsxSettingsDialog::DrawControlsTab() {
  if (BeginCard("CONTROLS")) {
    Row("Keyboard controls", "Play without a controller.");
    Checkbox("##mnk", "mnk_mode");
    Row("Controller vibration", "Rumble in the controller.");
    Checkbox("##vibration", "vibration");
    Row("Steering deadzone", "Ignore small stick movements.");
    float deadzone =
        std::strtof(rex::cvar::GetFlagByName("left_stick_deadzone_percentage").c_str(), nullptr) * 100.0f;
    if (ImGui::SliderFloat("##deadzone", &deadzone, 0.0f, 50.0f, "%.0f%%")) {
      Set("left_stick_deadzone_percentage", std::to_string(deadzone / 100.0f));
    }
    ImGui::EndTable();
  }
  EndCard();
  if (BeginCard("KEYBOARD BINDINGS", false)) DrawBinds();
  EndCard();
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

// The left column: the logo, a button per page, and the keys that drive the menu.
void SsxSettingsDialog::DrawSidebar(float width) {
  const float s = ui_scale_;
  ImDrawList* wd = ImGui::GetWindowDrawList();
  ImVec2 wp = ImGui::GetWindowPos();
  wd->AddRectFilled(wp, ImVec2(wp.x + width, wp.y + ImGui::GetWindowHeight()), Col(kRail), 16.0f * s,
                    ImDrawFlags_RoundCornersLeft);
  wd->AddLine(ImVec2(wp.x + width - 1.0f, wp.y), ImVec2(wp.x + width - 1.0f, wp.y + ImGui::GetWindowHeight()),
              Col(kBorder, 0.8f), 1.0f * s);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * s, 24.0f * s));
  ImGui::BeginChild("##side", ImVec2(width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::SetWindowFontScale(font_scale_);
  ImDrawList* d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetWindowPos();
  const float w = ImGui::GetWindowWidth(), h = ImGui::GetWindowHeight();

  // Logo: a blue tile with peaks, the name, and SETTINGS.
  const float tile = 60.0f * s;
  const ImVec2 tc(o.x + w * 0.5f, o.y + 28.0f * s + tile * 0.5f);
  d->AddCircleFilled(tc, tile * 0.95f, Col(kBlue, 0.10f));
  d->AddRectFilled(ImVec2(tc.x - tile * 0.5f, tc.y - tile * 0.5f), ImVec2(tc.x + tile * 0.5f, tc.y + tile * 0.5f),
                   Col(kBlue), 15.0f * s);
  d->AddRect(ImVec2(tc.x - tile * 0.5f, tc.y - tile * 0.5f), ImVec2(tc.x + tile * 0.5f, tc.y + tile * 0.5f),
             Col(ImVec4(0.6f, 0.76f, 1.0f, 1.0f), 0.6f), 15.0f * s, 0, 1.0f * s);
  DrawPageGlyph(d, 1, tc, tile * 0.3f, IM_COL32_WHITE, 2.4f * s);
  ImFont* big = title_font ? title_font : ImGui::GetFont();
  const float big_size = title_font ? 46.0f * s * 0.78f : ImGui::GetFontSize() * 1.4f;
  ImVec2 ts = big->CalcTextSizeA(big_size, FLT_MAX, 0.0f, "SSX");
  d->AddText(big, big_size, ImVec2(tc.x - ts.x * 0.5f, tc.y + tile * 0.5f + 12.0f * s), Col(kText), "SSX");
  const float caps_size = ImGui::GetFontSize() * 0.6f;
  const char* settings = "S E T T I N G S";
  ImVec2 ss = ImGui::GetFont()->CalcTextSizeA(caps_size, FLT_MAX, 0.0f, settings);
  d->AddText(ImGui::GetFont(), caps_size, ImVec2(tc.x - ss.x * 0.5f, tc.y + tile * 0.5f + 16.0f * s + ts.y), Col(kAccent),
             settings);

  // The pages.
  ImGui::SetCursorScreenPos(ImVec2(o.x + 18.0f * s, o.y + 196.0f * s));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f * s));
  const float iw = w - 36.0f * s, ih = 50.0f * s, round = 12.0f * s;
  for (int i = 0; i < int(std::size(kTabs)); ++i) {
    ImGui::PushID(i);
    const bool selected = i == current_tab_;
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);  // a controller moves through the page; LB / RB switch pages
    if (ImGui::Selectable("##page", selected, ImGuiSelectableFlags_None, ImVec2(iw, ih))) current_tab_ = i;
    ImGui::PopItemFlag();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (selected) {
      for (int k = 3; k >= 1; --k) {
        float g = k * 2.0f * s;
        d->AddRectFilled(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), Col(kBlue, 0.07f), round + g);
      }
      d->AddRectFilled(a, b, Col(kBlue), round);
      d->AddRect(a, b, Col(ImVec4(0.6f, 0.76f, 1.0f, 1.0f), 0.55f), round, 0, 1.0f * s);
    } else if (hovered) {
      d->AddRectFilled(a, b, Col(kSurface), round);
    }
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) {
      d->AddRect(ImVec2(a.x - 3.0f * s, a.y - 3.0f * s), ImVec2(b.x + 3.0f * s, b.y + 3.0f * s), Col(kBlueHover),
                 round + 3.0f * s, 0, 2.0f * s);
    }
    const ImU32 c = selected ? IM_COL32_WHITE : hovered ? Col(kText) : Col(kTextDim);
    const float mid = (a.y + b.y) * 0.5f, fs = ImGui::GetFontSize() * 0.92f;
    DrawPageGlyph(d, i, ImVec2(a.x + 30.0f * s, mid), 11.0f * s, c, 2.0f * s);
    d->AddText(ImGui::GetFont(), fs, ImVec2(a.x + 56.0f * s, mid - fs * 0.5f), c, kTabs[i]);
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  // The keys, in a small card at the bottom.
  const struct {
    const char* keys;
    const char* what;
  } hints[] = {{"F1 / Esc", "close"}, {"Back + Start", "on a controller"}, {"LB / RB", "pages"}};
  const float line = ImGui::GetFontSize() * 0.95f, hs = ImGui::GetFontSize() * 0.7f;
  const ImVec2 ha(o.x + 18.0f * s, o.y + h - 24.0f * s - line * 3.0f - 28.0f * s), hb(o.x + w - 18.0f * s, o.y + h - 24.0f * s);
  d->AddRectFilled(ha, hb, Col(kSurface), round);
  d->AddRect(ha, hb, Col(kBorder), round, 0, 1.0f * s);
  for (int i = 0; i < int(std::size(hints)); ++i) {
    ImVec2 at(ha.x + 16.0f * s, ha.y + 14.0f * s + i * line);
    d->AddText(ImGui::GetFont(), hs, at, Col(kText), hints[i].keys);
    float kw = ImGui::GetFont()->CalcTextSizeA(hs, FLT_MAX, 0.0f, hints[i].keys).x;
    d->AddText(ImGui::GetFont(), hs, ImVec2(at.x + kw + 8.0f * s, at.y), Col(kFaint), hints[i].what);
  }
  ImGui::EndChild();
}

// The right side: the page's title, its cards (scrolling), and the footer.
void SsxSettingsDialog::DrawMain() {
  const float s = ui_scale_;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.0f * s, 22.0f * s));
  ImGui::BeginChild("##main", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::SetWindowFontScale(font_scale_);

  // Header: a small line, the page's name, and Close on the right.
  const ImVec2 top = ImGui::GetCursorPos();
  ImGui::SetWindowFontScale(font_scale_ * 0.7f);
  ImGui::TextColored(kFaint, "SSX  SETTINGS");
  ImGui::SetWindowFontScale(font_scale_);
  if (title_font) ImGui::PushFont(title_font);
  ImGui::SetWindowFontScale(font_scale_ * (title_font ? 0.92f : 1.5f));
  ImGui::TextUnformatted(kTabs[current_tab_]);
  ImGui::SetWindowFontScale(font_scale_);
  if (title_font) ImGui::PopFont();
  const ImVec2 below = ImGui::GetCursorPos();
  const float close_w = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
  ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - close_w, top.y + 6.0f * s));
  if (Button("Close", false)) toggle_after_draw_ = true;
  ImGui::SetCursorPos(below);
  ImGui::Dummy(ImVec2(0.0f, 4.0f * s));

  // The page: its own scroll position each.
  const ImGuiStyle& style = ImGui::GetStyle();
  const float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + 14.0f * s;
  ImGui::PushID(current_tab_);
  ImGui::BeginChild("##page", ImVec2(0.0f, -footer), ImGuiChildFlags_NavFlattened);
  ImGui::SetWindowFontScale(font_scale_);
  switch (current_tab_) {
    case 0: DrawDisplayTab(); break;
    case 1: DrawGraphicsTab(); break;
    case 2: DrawAudioTab(); break;
    case 3: DrawControlsTab(); break;
    case 4: DrawProfileTab(); break;
    case 5: DrawSnowflakesTab(); break;
    default: DrawCheatsTab(); break;
  }
  ImGui::EndChild();
  ImGui::PopID();

  // Footer: the note left, the buttons right (Quit game always, Restart now when needed).
  ImVec2 line = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(line.x, line.y + 4.0f * s),
                                      ImVec2(line.x + ImGui::GetContentRegionAvail().x, line.y + 4.0f * s), Col(kBorder),
                                      1.0f * s);
  ImGui::Dummy(ImVec2(0.0f, 10.0f * s));
  ImGui::AlignTextToFramePadding();
  if (restart_needed_) {
    ImGui::TextColored(kWarn, "Some changes apply after a restart.");
  } else {
    ImGui::TextColored(kFaint, "Changes are saved automatically.");
  }
  const float pad = style.FramePadding.x * 2.0f;
  float buttons = ImGui::CalcTextSize("Quit game").x + pad;
  if (restart_needed_) buttons += ImGui::CalcTextSize("Restart now").x + pad + style.ItemSpacing.x;
  ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
  if (restart_needed_) {
    if (Button("Restart now", true)) restart_();
    ImGui::SameLine();
  }
  if (Button("Quit game", false)) ImGui::OpenPopup("##quit_confirm");
  DrawQuitConfirm();
  ImGui::EndChild();
}

void SsxSettingsDialog::OnDraw(ImGuiIO& io) {
  if (REXCVAR_GET(ssx_show_fps)) DrawFps();
  PollPad(io);
  if (!open_) return;

  const float s = UiScale(io);
  ui_scale_ = s;
  font_scale_ = body_font ? s : std::max(1.0f, io.DisplaySize.y / 800.0f);
  if (!select_tab_.empty()) {
    for (int i = 0; i < int(std::size(kTabs)); ++i) {
      if (select_tab_ == kTabs[i]) current_tab_ = i;
    }
    select_tab_.clear();
  }
  if (body_font) ImGui::PushFont(body_font);
  PushStyle(s);

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(1300.0f * s, io.DisplaySize.x - 24.0f), std::min(840.0f * s, io.DisplaySize.y - 24.0f)),
                           ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("##ssx_settings", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::SetWindowFontScale(font_scale_);

  DrawSidebar(270.0f * s);
  ImGui::SameLine(0.0f, 0.0f);
  DrawMain();

  bool close = toggle_after_draw_ || (capturing_.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape));
  toggle_after_draw_ = false;
  ImGui::End();
  PopStyle();
  if (body_font) ImGui::PopFont();
  if (close) Toggle();
}
