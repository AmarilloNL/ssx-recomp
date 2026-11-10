// ssx - ReXGlue Recompiled Project
//
// App lifecycle: locate the game (folder or .iso), apply PC settings, install
// dropped DLC, and host the settings menu.

#pragma once

#include <cstdlib>
#include <fstream>
#include <memory>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#include "ssx_dlc.h"
#include "ssx_game_path.h"
#include "ssx_restart.h"
#include "ssx_settings.h"
#include "ssx_settings_dialog.h"

class SsxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SsxApp>(new SsxApp(ctx, "ssx",
        PPCImageConfig));
  }

 protected:
  // Find the game; an .iso is mounted later (OnLoadXexImage), so the runtime
  // gets an empty placeholder folder for its host mount.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)>) override {
    config_path_ = defaults.config_path;
    SsxWaitForPreviousInstance();
    auto game = SsxLocateGame(rex::filesystem::GetExecutableFolder(), config_path_);
    if (!game) {
      app_context().QuitFromUIThread();
      return std::nullopt;
    }
    rex::PathConfig paths = defaults;
#ifdef SSX_TITLE_UPDATE
    // Built for the title update: the runtime applies its default.xexp from update: to the
    // disc's default.xex as it loads (the data must match the recompiled code), and the game
    // reads patch0.big from update:.
    auto update = SsxLocateUpdate(rex::filesystem::GetExecutableFolder());
    if (!update) {
      app_context().QuitFromUIThread();
      return std::nullopt;
    }
    paths.update_data_root = *update;
#endif
    if (std::filesystem::is_directory(*game)) {
      paths.game_data_root = *game;
    } else {
      iso_path_ = *game;
      paths.game_data_root = paths.user_data_root / "iso-mount";
      std::filesystem::create_directories(paths.game_data_root);
      // ReXApp checks for the entrypoint on the host mount before loading it
      // through the VFS (which by then points at the disc); an empty stand-in
      // satisfies the check and is never read.
      std::ofstream(paths.game_data_root / "default.xex", std::ios::app);
    }
    return paths;
  }

  void OnLoadXexImage(std::string&) override {
    if (iso_path_.empty()) return;
    auto* vfs = runtime()->file_system();
    auto disc = std::make_unique<rex::filesystem::DiscImageDevice>("\\Device\\Cdrom0", iso_path_);
    if (!disc->Initialize()) {
      REXLOG_ERROR("Failed to open disc image {}", iso_path_.string());
      return;
    }
    vfs->RegisterDevice(std::move(disc));
    for (const char* link : {"game:", "d:"}) {
      vfs->UnregisterSymbolicLink(link);
      vfs->RegisterSymbolicLink(link, "\\Device\\Cdrom0");
    }
    REXLOG_INFO("Mounted disc image {}", iso_path_.string());
  }

  // SSX_DUMP_IMAGE=<path> writes the loaded guest image there, for offline
  // analysis of function pointer tables the codegen missed.
  void OnPostLoadXexImage() override {
    const char* path = std::getenv("SSX_DUMP_IMAGE");
    if (!path) return;
    auto* mem = runtime()->memory();
    std::ofstream(path, std::ios::binary)
        .write(mem->TranslateVirtual<const char*>(0x82000000), 0x1A00000);
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    // Load the plugin now (ReXApp would right after this hook) so its cvars,
    // e.g. resolution_scale, exist for SsxApplyStartupSettings.
    if (!config.graphics) config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
    // Keyboard play on by default; an explicit choice in ssx.toml wins.
    if (rex::cvar::GetFlagSource("mnk_mode") == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("mnk_mode", "true");
    }
    SsxApplyStartupSettings();
  }

  void OnPostSetup() override {
    // SSX binds two texture fetch constants per frame with type 0 ("invalid")
    // but otherwise sane contents; real hardware samples them, so do the same.
    // GPU cvars only exist once the plugin has loaded, hence here.
    rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
    InstallDroppedDlc(runtime(), user_data_root());

    window()->SetTitle("SSX");
    settings_ = std::make_unique<SsxSettingsDialog>(
        imgui_drawer(), static_cast<rex::input::InputSystem*>(runtime()->input_system()),
        config_path_, [this] {
          // Same path as the window close button: ends the title and exits the process.
          if (SsxSpawnRestart()) window()->RequestClose();
        });
    rex::ui::RegisterBind("bind_ssx_settings", "F1", "SSX settings menu",
                          [this] { settings_->Toggle(); });
    // Testing aid: SSX_OPEN_SETTINGS=<tab name> opens the menu at startup on that tab.
    if (const char* tab = std::getenv("SSX_OPEN_SETTINGS")) settings_->Open(tab);
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { SsxSettingsDialog::ConfigureFonts(atlas); }

  void OnShutdown() override { settings_.reset(); }

 private:
  std::filesystem::path config_path_;
  std::filesystem::path iso_path_;
  std::unique_ptr<SsxSettingsDialog> settings_;
};
