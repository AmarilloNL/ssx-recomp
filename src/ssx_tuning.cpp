// ssx - rider levels 11 to 15, without EA's servers.
//
// EA raised the rider level cap (10 to 15, with the gear slots and stats that go with it) through
// "tuning updates": a small archive of database changes the game downloaded at sign-in. The game keeps
// the last one in the profile save, as a version number and a 150 KB slot. At startup it asks the
// server which version is current; offline that fails and it applies the save's copy, if there is
// one. A new save has none, so its riders stop at level 10.
//
// The port ships nothing of EA's. It makes the seven per-level tables of the game's database as long
// as that update made them, every time the game loads the database, so every save gets levels 11 to
// 15. A save that carries a tuning update still uses it on top (same level tables, plus EA's other
// tuning); the port also keeps such an update aside (<user data>/tuning/update.bin, or any save file
// put in that folder) and hands it to saves without one, which then keep it like one they downloaded.
// Title update builds only: the higher levels were made for it.

#include "ssx_tuning.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

namespace {
std::filesystem::path tuning_dir;
}

void SsxSetTuningDir(const std::filesystem::path& dir) { tuning_dir = dir; }

#ifdef SSX_TITLE_UPDATE
namespace {

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint32_t Be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }

// ---- A tuning update from a save ------------------------------------------------------------------

constexpr uint32_t kSlotBytes = 0x25800;  // the game's buffer for the archive

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
  std::error_code ec;
  std::filesystem::create_directories(tuning_dir, ec);
  std::ofstream file(tuning_dir / "update.bin", std::ios::binary | std::ios::trunc);
  const uint8_t be_version[4] = {uint8_t(version >> 24), uint8_t(version >> 16), uint8_t(version >> 8),
                                 uint8_t(version)};
  file.write(reinterpret_cast<const char*>(be_version), 4);
  file.write(reinterpret_cast<const char*>(slot), size);
  REXLOG_INFO("Tuning: kept this save's tuning update (version {}) in {}", version,
              (tuning_dir / "update.bin").string());
}

// ---- The port's own level tables ------------------------------------------------------------------
//
// Seven tables in one collection of the game's database have an entry per level, ten on the disc. A
// table is an attribute of the collection: a 12-byte node (key, pointer, type) in the collection's
// hash table, pointing at an array (16-bit capacity, count, entry size and type, then the entries).
// Each gets a longer copy with the entries the last tuning update had after the tenth.

std::vector<uint8_t> Words(std::initializer_list<uint32_t> words) {
  std::vector<uint8_t> bytes;
  for (uint32_t w : words) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(w >> shift));
  }
  return bytes;
}

struct LevelTable {
  uint32_t key;
  uint32_t entry_bytes;
  std::vector<uint8_t> more;  // the entries after the disc's ten, as the game stores them
};

const std::vector<LevelTable>& LevelTables() {
  static const std::vector<LevelTable> tables = {
      // XP that completes a level (the first is level 1's: 6,000). Levels 11 to 15.
      {0x69BBAD40, 4, Words({7500000, 10000000, 12500000, 15000000, 17500000})},
      // Credits to buy access to a level (free up to 10). Levels 11 to 15.
      {0x665FA517, 4, Words({1000000, 2500000, 5000000, 7500000, 10000000})},
      // Slots a rider has at a level, a byte per kind of slot (the last two: mods and geotags).
      {0x446DD69D, 5, {1, 1, 2, 3, 3, 1, 1, 2, 3, 4, 1, 1, 2, 4, 4, 1, 1, 3, 4, 4, 1, 1, 3, 4, 4}},
      // Two ranges (lowest, highest) for the stats of gear at a level.
      {0x1FEF6756, 8, Words({230, 240, 240, 250, 250, 260, 260, 270, 270, 290})},
      {0xE27AB77B, 8, Words({80, 85, 80, 85, 80, 85, 80, 85, 80, 85})},
      // A price range and a value with one more step than the disc's ten.
      {0xA1A6A9D3, 8, Words({1600000, 4000000})},
      {0x59E3CBF1, 4, Words({6000})},
  };
  return tables;
}

constexpr uint32_t kDiscLevels = 10;

// Whether `size` bytes at a guest address can be read.
bool Readable(rex::memory::Memory* memory, uint32_t address, uint32_t size) {
  rex::memory::BaseHeap* heap = address ? memory->LookupHeap(address) : nullptr;
  rex::memory::HeapAllocationInfo info{};
  return heap && heap->QueryRegionInfo(address, &info) && (info.state & rex::memory::kMemoryAllocationCommit) &&
         (info.protect & rex::memory::kMemoryProtectRead) &&
         uint64_t(address) + size <= uint64_t(info.base_address) + info.region_size;
}

// The array a table's node points at, if it is the disc's: ten entries of the table's size.
const uint8_t* DiscArray(rex::memory::Memory* memory, const uint8_t* node, const LevelTable& table) {
  uint32_t address = Be32(node + 4);
  uint32_t bytes = 8 + kDiscLevels * table.entry_bytes;
  if ((address & 1) || !Readable(memory, address, bytes)) return nullptr;
  const uint8_t* array = memory->TranslateVirtual<const uint8_t*>(address);
  return Be16(array) == kDiscLevels && Be16(array + 2) == kDiscLevels && Be16(array + 4) == table.entry_bytes
             ? array
             : nullptr;
}

// The XP table's nodes that still point at the disc's array: the key, then a pointer to ten 32-bit
// values that start with 6,000. (Normally one: the collection the database just built.)
// ponytail: a scan of the game's heap after each load of the database (a few hundred MB, about a
// tenth of a second, during loading). Take the node table from the game's collection object instead
// if that ever shows.
std::vector<uint8_t*> FindXpNodes(rex::memory::Memory* memory, bool* longer_seen) {
  std::vector<uint8_t*> found;
  const LevelTable& xp = LevelTables()[0];
  uint8_t key[4] = {uint8_t(xp.key >> 24), uint8_t(xp.key >> 16), uint8_t(xp.key >> 8), uint8_t(xp.key)};
  for (uint32_t start : {0xA0000000u, 0x40000000u}) {
    rex::memory::BaseHeap* heap = memory->LookupHeap(start);
    if (!heap) continue;
    uint64_t end = uint64_t(heap->heap_base()) + heap->heap_size();
    for (uint64_t address = heap->heap_base(); address < end;) {
      rex::memory::HeapAllocationInfo info{};
      if (!heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) {
        address += heap->page_size();
        continue;
      }
      if ((info.state & rex::memory::kMemoryAllocationCommit) && (info.protect & rex::memory::kMemoryProtectRead)) {
        uint8_t* region = memory->TranslateVirtual<uint8_t*>(info.base_address);
        for (uint32_t at = 0; at + 12 <= info.region_size; at += 4) {
          if (std::memcmp(region + at, key, 4) != 0) continue;
          const uint8_t* array = DiscArray(memory, region + at, xp);
          if (array && Be32(array + 8) == 6000) {
            found.push_back(region + at);
          } else if (uint32_t longer = Be32(region + at + 4); !(longer & 1) && Readable(memory, longer, 16)) {
            // A table that is long already: a tuning update's, or one we made longer before.
            const uint8_t* other = memory->TranslateVirtual<const uint8_t*>(longer);
            if (Be16(other + 2) > kDiscLevels && Be16(other + 4) == xp.entry_bytes && Be32(other + 8) == 6000) {
              *longer_seen = true;
            }
          }
        }
      }
      address = uint64_t(info.base_address) + info.region_size;
    }
    if (!found.empty()) break;
  }
  return found;
}

// Gives the collection the database just built the longer level tables.
void ExtendLevelTables() {
  rex::memory::Memory* memory = rex::system::kernel_memory();
  const auto& tables = LevelTables();
  // One block for the seven longer arrays, kept for the session and written again for every load
  // (the arrays' type field comes from the load).
  static uint32_t block = 0;
  static uint32_t offsets[8] = {};
  if (!block) {
    uint32_t total = 0;
    for (size_t i = 0; i < tables.size(); ++i) {
      offsets[i] = total;
      total += (8 + kDiscLevels * tables[i].entry_bytes + uint32_t(tables[i].more.size()) + 15) & ~15u;
    }
    block = memory->SystemHeapAlloc(total);
    if (!block) {
      REXLOG_WARN("Levels: no memory for the level tables; riders stop at level 10");
      return;
    }
  }
  auto start = std::chrono::steady_clock::now();
  bool longer_seen = false;
  std::vector<uint8_t*> xp_nodes = FindXpNodes(memory, &longer_seen);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  if (xp_nodes.empty()) {
    // (A tuning update's own database file has the tables too, already long: nothing to do then.)
    if (!longer_seen) REXLOG_WARN("Levels: the game's level tables were not found; riders stop at level 10");
    return;
  }
  for (uint8_t* xp_node : xp_nodes) {
    // The other tables' nodes are in the same hash table: 12-byte nodes around the XP table's.
    // All seven first, so a collection's tables are changed together or not at all.
    std::vector<uint8_t*> nodes;
    for (const LevelTable& table : tables) {
      uint8_t* found = nullptr;
      for (int step = -256; step <= 256 && !found; ++step) {
        uint8_t* node = xp_node + step * 12;
        uint32_t address = memory->HostToGuestVirtual(node);
        if (Readable(memory, address, 12) && Be32(node) == table.key && DiscArray(memory, node, table)) found = node;
      }
      if (!found) break;
      nodes.push_back(found);
    }
    if (nodes.size() != tables.size()) {
      REXLOG_WARN("Levels: one of the game's level tables was not found; riders stop at level 10");
      continue;
    }
    for (size_t i = 0; i < tables.size(); ++i) {
      const LevelTable& table = tables[i];
      const uint8_t* old_array = DiscArray(memory, nodes[i], table);
      uint32_t old_bytes = 8 + kDiscLevels * table.entry_bytes;
      uint32_t count = kDiscLevels + uint32_t(table.more.size()) / table.entry_bytes;
      uint32_t address = block + offsets[i];
      uint8_t* array = memory->TranslateVirtual<uint8_t*>(address);
      std::memcpy(array, old_array, old_bytes);
      std::memcpy(array + old_bytes, table.more.data(), table.more.size());
      array[0] = array[2] = uint8_t(count >> 8);
      array[1] = array[3] = uint8_t(count);
      // The node now points at the longer array (the disc's array stays where it is).
      const uint8_t pointer[4] = {uint8_t(address >> 24), uint8_t(address >> 16), uint8_t(address >> 8),
                                  uint8_t(address)};
      std::memcpy(nodes[i] + 4, pointer, 4);
    }
    REXLOG_INFO("Levels: the game's level tables now go to level 15 (found in {} ms)", ms);
  }
}

// Whether a database file (.vlt) being loaded is the one with the level tables.
bool HoldsLevelTables(uint32_t data, uint32_t size) {
  rex::memory::Memory* memory = rex::system::kernel_memory();
  if (size < 16 || size > (16u << 20) || !Readable(memory, data, size)) return false;
  const uint8_t* file = memory->TranslateVirtual<const uint8_t*>(data);
  uint32_t key = LevelTables()[0].key;
  const uint8_t bytes[4] = {uint8_t(key >> 24), uint8_t(key >> 16), uint8_t(key >> 8), uint8_t(key)};
  return std::search(file, file + size, std::begin(bytes), std::end(bytes)) != file + size;
}

}  // namespace

// The step of the game's tuning check that picks between the server's version and the save's. Its
// object holds the save's tuning slot at +28: the version at +8, the archive buffer at +12.
REX_EXTERN(SSX_FN_TUNING_PICK_IMP);
REX_HOOK_RAW(SSX_FN_TUNING_PICK) {
  uint32_t slot = REX_LOAD_U32(ctx.r3.u32 + 28);
  uint32_t buffer = slot ? REX_LOAD_U32(slot + 12) : 0;
  if (buffer) {
    uint8_t* archive = rex::system::kernel_memory()->TranslateVirtual<uint8_t*>(buffer);
    uint32_t version = REX_LOAD_U32(slot + 8);
    Tuning newest = tuning_dir.empty() ? Tuning{} : FindTuning();
    if (newest.version > version) {
      std::memcpy(archive, newest.archive.data(), newest.archive.size());
      REX_STORE_U32(slot + 8, newest.version);
      REXLOG_INFO("Tuning: this save has {}, using version {} from {}",
                  version ? fmt::format("tuning update version {}", version) : "no tuning update",
                  newest.version, tuning_dir.string());
    } else if (version > newest.version && !tuning_dir.empty()) {
      KeepTuning(version, archive);
    }
  }
  SSX_FN_TUNING_PICK_IMP(ctx, base);
}

// The game's loader for a database file pair (r6, r7: the .vlt file in memory and its size). When it
// returns, the file's collections are built; the one with the level tables gets the longer ones.
REX_EXTERN(SSX_FN_LOAD_DATABASE_IMP);
REX_HOOK_RAW(SSX_FN_LOAD_DATABASE) {
  bool levels = HoldsLevelTables(ctx.r6.u32, ctx.r7.u32);
  SSX_FN_LOAD_DATABASE_IMP(ctx, base);
  if (levels && ctx.r3.u32) ExtendLevelTables();
}
#endif
