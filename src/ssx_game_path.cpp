// ssx - find the user's SSX game data: an extracted disc folder or the .iso.
//
// Order: the saved/--game_data_root path, a "game" folder next to the exe, an
// .iso next to the exe, then a file picker. The pick is saved to ssx.toml.

#include "ssx_game_path.h"

#include <algorithm>
#include <cwctype>
#include <string>

#include <windows.h>
#include <shobjidl.h>

#include <rex/cvar.h>

#include "ssx_settings.h"

#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace {

bool IsIso(const fs::path& p) {
  std::wstring ext = p.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  return ext == L".iso" && fs::is_regular_file(p);
}

// Normalizes a candidate to a game folder or .iso, or returns empty if invalid.
fs::path Validate(fs::path p) {
  std::error_code ec;
  if (p.empty() || !fs::exists(p, ec)) return {};
  if (fs::is_regular_file(p, ec) && p.filename() == "default.xex") p = p.parent_path();
  if (fs::is_directory(p, ec) && fs::exists(p / "default.xex", ec)) return p;
  if (IsIso(p)) return p;
  return {};
}

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

}  // namespace

std::optional<fs::path> SsxLocateGame(const fs::path& exe_dir, const fs::path& config_path) {
  fs::path found = Validate(rex::cvar::GetFlagByName("game_data_root"));
  if (!found.empty()) return found;  // from ssx.toml or --game_data_root; nothing to save
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
  while (found.empty()) {
    fs::path picked = PickWithDialog();
    if (picked.empty()) return std::nullopt;  // cancelled
    found = Validate(picked);
    if (found.empty()) {
      MessageBoxW(nullptr,
                  L"That doesn't look like SSX. Pick default.xex from the extracted disc, "
                  L"or the disc's .iso file.",
                  L"SSX", MB_OK | MB_ICONWARNING);
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
  MessageBoxW(nullptr,
              L"This SSX build is made for the title update, but the update's files are missing "
              L"from the \"update\" folder next to ssx.exe. Run SSX-Installer.exe again to fix it.",
              L"SSX", MB_OK | MB_ICONERROR);
  return std::nullopt;
}
