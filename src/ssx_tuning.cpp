// ssx - the game's tuning update, without EA's servers.
//
// EA raised the rider level cap (10 to 15, with the gear slots and stats that go with it) through
// "tuning updates": a small archive of database changes the game downloaded at sign-in. The game keeps
// the last one in the profile save, as a version number and a 150 KB slot. At startup it asks the
// server which version is current; offline that fails and it applies the save's copy, if there is
// one. A new save has none, so its riders stop at level 10.
//
// The port ships nothing of this. It keeps the update aside the first time a save that has one is
// loaded (<user data>/tuning/update.bin), and also takes it from any save file dropped into that
// folder. A save without one (or with an older one) is then handed that copy, and from there the game
// keeps it in the save like one it downloaded. Title update builds only: the last tuning update was
// made for it.

#include "ssx_tuning.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

namespace {
std::filesystem::path tuning_dir;
}

void SsxSetTuningDir(const std::filesystem::path& dir) {
  tuning_dir = dir;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
}

#ifdef SSX_TITLE_UPDATE
namespace {

constexpr uint32_t kSlotBytes = 0x25800;  // the game's buffer for the archive

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// The size of the tuning archive at `p` (an "EB" v3 archive holding updates.vlt), 0 if it isn't one.
uint32_t ArchiveSize(const uint8_t* p, size_t available) {
  if (available < 0x30 || std::memcmp(p, "EB\0\3", 4) != 0 || Be32(p + 0x18) != 0) return 0;
  uint32_t size = Be32(p + 0x1C);
  if (size < 0x30 || size > kSlotBytes || size > available) return 0;
  std::string_view index(reinterpret_cast<const char*>(p), std::min<size_t>(size, 0x1000));
  return index.find("updates.vlt") != index.npos ? size : 0;
}

struct Tuning {
  uint32_t version = 0;
  std::vector<uint8_t> archive;
};

// The newest tuning update in the files of the tuning folder: our own copy, or a profile save that
// carries one (the version is the 32-bit number right before the archive in both).
// ponytail: reads each file as plain bytes. A save still inside its Xbox 360 package is found too as
// long as the archive isn't split by one of the package's hash blocks (a single-file save package
// keeps it whole); unpack the package properly if one ever isn't found.
Tuning FindTuning() {
  Tuning best;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(tuning_dir, ec)) {
    if (!entry.is_regular_file(ec) || entry.file_size(ec) > (64u << 20)) continue;
    std::ifstream file(entry.path(), std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    static constexpr uint8_t kMagic[4] = {'E', 'B', 0, 3};
    for (auto it = data.begin();; ++it) {
      it = std::search(it, data.end(), std::begin(kMagic), std::end(kMagic));
      if (it == data.end()) break;
      size_t at = size_t(it - data.begin());
      uint32_t size = at >= 4 ? ArchiveSize(&data[at], data.size() - at) : 0;
      uint32_t version = size ? Be32(&data[at - 4]) : 0;
      if (version > best.version && version < 0x10000) {
        best.version = version;
        best.archive.assign(it, it + size);
      }
    }
  }
  return best;
}

// Keeps the loaded save's tuning update (version, then the archive) as <tuning folder>/update.bin.
void KeepTuning(uint32_t version, const uint8_t* slot) {
  uint32_t size = ArchiveSize(slot, kSlotBytes);
  if (!size) return;
  std::ofstream file(tuning_dir / "update.bin", std::ios::binary | std::ios::trunc);
  const uint8_t be_version[4] = {uint8_t(version >> 24), uint8_t(version >> 16), uint8_t(version >> 8),
                                 uint8_t(version)};
  file.write(reinterpret_cast<const char*>(be_version), 4);
  file.write(reinterpret_cast<const char*>(slot), size);
  REXLOG_INFO("Tuning: kept this save's tuning update (version {}) in {}", version,
              (tuning_dir / "update.bin").string());
}

}  // namespace

// The step of the game's tuning check that picks between the server's version and the save's. Its
// object holds the save's tuning slot at +28: the version at +8, the archive buffer at +12.
REX_EXTERN(SSX_FN_TUNING_PICK_IMP);
REX_HOOK_RAW(SSX_FN_TUNING_PICK) {
  uint32_t slot = REX_LOAD_U32(ctx.r3.u32 + 28);
  uint32_t buffer = slot ? REX_LOAD_U32(slot + 12) : 0;
  if (buffer && !tuning_dir.empty()) {
    uint32_t version = REX_LOAD_U32(slot + 8);
    Tuning newest = FindTuning();
    if (newest.version > version) {
      std::memcpy(base + buffer, newest.archive.data(), newest.archive.size());
      REX_STORE_U32(slot + 8, newest.version);
      REXLOG_INFO("Tuning: this save has {}, using version {} from {}",
                  version ? fmt::format("tuning update version {}", version) : "no tuning update",
                  newest.version, tuning_dir.string());
    } else if (version > newest.version) {
      KeepTuning(version, base + buffer);
    }
  }
  SSX_FN_TUNING_PICK_IMP(ctx, base);
}
#endif
