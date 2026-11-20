// ssx - find the user's SSX game data: an extracted disc folder or the .iso.
//
// Order: the saved/--game_data_root path, a "game" folder next to the exe, an
// .iso next to the exe, then a file picker. The pick is saved to ssx.toml. A folder counts only
// as the whole extracted disc (default.xex and data0.big).

#include "ssx_game_path.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#else
#include <cstdio>
#include <cstdlib>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "ssx_settings.h"

namespace fs = std::filesystem;

// The standard option for launchers, scripts and shortcuts (the Recomp Standards' --game <path>).
REXCVAR_DEFINE_STRING(game, "", "SSX",
                      "Your copy of SSX for this start: the disc image (.iso) or the extracted disc's folder");

#ifndef _WIN32
namespace {

bool Has(const char* tool) {
  return std::system((std::string("command -v ") + tool + " >/dev/null 2>&1").c_str()) == 0;
}

// Runs a command, its first line of output (without the newline).
std::string FirstLine(const std::string& command) {
  std::string line;
  if (FILE* out = popen(command.c_str(), "r")) {
    char buffer[4096];
    if (std::fgets(buffer, sizeof(buffer), out)) line = buffer;
    pclose(out);
  }
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  return line;
}

}  // namespace

// The titles and patterns are the port's own constants, never user text, so plain single quotes do.
std::string SsxPickFile(const char* title, const char* filter_name, const char* patterns) {
  const char* home = std::getenv("HOME");
  std::string start = home ? home : "/";
  if (Has("kdialog")) {
    return FirstLine(std::string("kdialog --title '") + title + "' --getopenfilename '" + start + "' '" +
                     patterns + "|" + filter_name + "' 2>/dev/null");
  }
  if (Has("zenity")) {
    return FirstLine(std::string("zenity --file-selection --title='") + title + "' --file-filter='" +
                     filter_name + " | " + patterns + "' 2>/dev/null");
  }
  std::fprintf(stderr, "SSX: no file picker found (install kdialog or zenity).\n");
  return {};
}

void SsxMessage(const char* text, bool error) {
  std::fprintf(stderr, "SSX: %s\n", text);
  std::string quoted = "'";
  for (const char* c = text; *c; ++c) quoted += *c == '\'' ? std::string("'\\''") : std::string(1, *c);
  quoted += "'";
  if (Has("kdialog")) {
    std::system((std::string("kdialog --title SSX ") + (error ? "--error " : "--sorry ") + quoted + " 2>/dev/null").c_str());
  } else if (Has("zenity")) {
    std::system((std::string("zenity --title=SSX ") + (error ? "--error" : "--warning") + " --text=" + quoted + " 2>/dev/null").c_str());
  }
}
#endif

namespace {

bool IsIso(const fs::path& p) {
  std::wstring ext = p.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  return ext == L".iso" && fs::is_regular_file(p);
}

// A folder with default.xex in it, or that default.xex: the folder (empty if neither).
fs::path XexFolder(fs::path p) {
  std::error_code ec;
  if (fs::is_regular_file(p, ec) && p.filename() == "default.xex") p = p.parent_path();
  return fs::is_directory(p, ec) && fs::exists(p / "default.xex", ec) ? p : fs::path();
}

// An extracted disc is the whole disc: default.xex with the game's archives next to it (data0.big is the
// first one the game opens). A folder with only default.xex, like the setup's own copy in
// source\game-dump\files, has none of the game's data: started from it, the game crashes at once (issue #23).
bool IsExtractedDisc(const fs::path& folder) {
  std::error_code ec;
  return fs::exists(folder / "data0.big", ec);
}

// Normalizes a candidate to a game folder or .iso, or returns empty if invalid.
fs::path Validate(fs::path p) {
  std::error_code ec;
  if (p.empty() || !fs::exists(p, ec)) return {};
  if (const fs::path folder = XexFolder(p); !folder.empty()) return IsExtractedDisc(folder) ? folder : fs::path();
  if (IsIso(p)) return p;
  return {};
}

#ifdef _WIN32
fs::path PickWithDialog() {
  fs::path result;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    return result;
  }
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    const COMDLG_FILTERSPEC filters[] = {{L"SSX game (default.xex, *.iso)", L"default.xex;*.iso"}};
    dialog->SetFileTypes(1, filters);
    dialog->SetTitle(L"Select your SSX game: default.xex in the extracted disc, or the .iso");
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
    dialog->Release();
  }
  CoUninitialize();
  return result;
}

void ShowMessage(const wchar_t* text, bool error) {
  MessageBoxW(nullptr, text, L"SSX", MB_OK | (error ? MB_ICONERROR : MB_ICONWARNING));
}
#define SSX_TEXT(s) L##s
#else
fs::path PickWithDialog() {
  return SsxPickFile("Select your SSX game: default.xex in the extracted disc, or the .iso",
                     "SSX game (default.xex, .iso)", "*.iso *.ISO default.xex");
}

void ShowMessage(const char* text, bool error) { SsxMessage(text, error); }
#define SSX_TEXT(s) s
#endif

}  // namespace

void SsxExit(int code, const char* why) {
  REXLOG_ERROR("{} (exit code {})", why, code);
  rex::FlushLogging();
  std::_Exit(code);
}

std::optional<fs::path> SsxLocateGame(const fs::path& exe_dir, const fs::path& config_path) {
  if (!REXCVAR_GET(game).empty()) {
    if (fs::path given = Validate(REXCVAR_GET(game)); !given.empty()) return given;  // not saved
    REXLOG_WARN("--game {}: not an SSX disc image or a whole extracted disc (default.xex and data0.big)", REXCVAR_GET(game));
  }
  const std::string saved = rex::cvar::GetFlagByName("game_data_root");
  fs::path found = Validate(saved);
  if (!found.empty()) return found;  // from ssx.toml or --game_data_root; nothing to save
  const bool saved_incomplete = !saved.empty() && !XexFolder(saved).empty();
  found = Validate(exe_dir / "game");
  if (found.empty()) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(exe_dir, ec)) {
      if (IsIso(entry.path())) {
        found = entry.path();
        break;
      }
    }
  }
  if (found.empty() && saved_incomplete) {
    ShowMessage(SSX_TEXT("SSX is set to read the game from a folder that has only default.xex, not the rest of "
                         "the disc (data0.big and the other files). Pick default.xex in your whole extracted "
                         "disc, or the disc's .iso file."),
                false);
  }
  while (found.empty()) {
    fs::path picked = PickWithDialog();
    if (picked.empty()) return std::nullopt;  // cancelled
    found = Validate(picked);
    if (found.empty() && !XexFolder(picked).empty()) {
      ShowMessage(SSX_TEXT("That folder has default.xex but not the rest of the disc (data0.big and the other "
                           "files). Pick default.xex in your whole extracted disc, or the disc's .iso file."),
                  false);
    } else if (found.empty()) {
      ShowMessage(SSX_TEXT("That doesn't look like SSX. Pick default.xex from the extracted disc, "
                           "or the disc's .iso file."),
                  false);
    }
  }
  rex::cvar::SetFlagByName("game_data_root", found.string());
  SsxSaveConfig(config_path);
  return found;
}

std::optional<fs::path> SsxLocateUpdate(const fs::path& exe_dir) {
  fs::path folder = exe_dir / "update";
  std::error_code ec;
  if (fs::exists(folder / "default.xexp", ec) && fs::exists(folder / "patch0.big", ec)) {
    return folder;
  }
  ShowMessage(SSX_TEXT("This SSX build is made for the title update, but the update's files are missing "
                       "from the \"update\" folder next to the game. Start ssx-recomp.exe and click Add DLC / rebuild to repair it."),
              true);
  return std::nullopt;
}
