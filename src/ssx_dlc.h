#pragma once

#include <filesystem>

namespace rex {
class Runtime;
}

// Installs any not-yet-installed content packages from <user_data_root>/dlc.
void InstallDroppedDlc(rex::Runtime* runtime, const std::filesystem::path& user_data_root);
