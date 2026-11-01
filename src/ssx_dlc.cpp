// ssx - install DLC packages dropped into <user data>/dlc.
//
// Each file there is an Xbox 360 STFS content package (e.g. the files under
// 4541096D/00000002/ from a DLC download). The SDK's ContentManager extracts
// it to <user data>/0000000000000000/<title>/00000002/<file name>/, where the
// title's content enumeration finds it. Already-installed packages are skipped.

#include "ssx_dlc.h"

#include <fmt/format.h>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

void InstallDroppedDlc(rex::Runtime* runtime, const std::filesystem::path& user_data_root) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path drop_dir = user_data_root / "dlc";
  fs::create_directories(drop_dir, ec);

  auto* kernel = runtime->kernel_state();
  const fs::path installed_root = user_data_root / "0000000000000000" /
                                  fmt::format("{:08X}", kernel->title_id()) / "00000002";

  for (const auto& entry : fs::directory_iterator(drop_dir, ec)) {
    if (!entry.is_regular_file() || fs::exists(installed_root / entry.path().filename())) {
      continue;
    }
    auto result = kernel->content_manager()->InstallContent(entry.path());
    if (result == 0) {
      REXLOG_INFO("DLC: installed {}", entry.path().filename().string());
    } else {
      REXLOG_ERROR("DLC: failed to install {} (X_RESULT {:#x})", entry.path().string(), result);
    }
  }
}
