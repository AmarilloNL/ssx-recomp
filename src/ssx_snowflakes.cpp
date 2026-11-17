// ssx - snowflake tracker (issue #9).
//
// Snowflakes are EA's offline geotags: each drop that has them comes with a list (a CSV in the game
// data, <drop>_track_geotagdb.csv: Id,Quality,Level,XPos,YPos,ZPos after a header line). At startup
// the game counts them all (599) through one function per list; hooking it gives every drop's list.
// The profile keeps the collected ones as a set of 64-bit ids,
//   id = (Id << 32) + fnv1_32("<drop>_track") * 10^10   (mod 2^64),
// an rb-tree (root at +12, node: right +0, left +4, key +16) at +24 of the object the counting function
// gets in r3, its size at +44. Nothing here writes to the game.

#include "ssx_snowflakes.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <rex/hook.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

namespace {

struct Drop {
  std::string code;  // e.g. rockies0705
  uint64_t base;     // fnv1_32("<code>_track") * 10^10
  std::unordered_set<uint32_t> ids;
};

std::mutex mutex;
std::vector<Drop> drops;
uint32_t owner = 0;
uint8_t* guest = nullptr;

uint32_t Fnv1(std::string_view s) {
  uint32_t h = 0x811C9DC5u;
  for (unsigned char c : s) h = (h * 0x01000193u) ^ c;
  return h;
}

void AddDrop(std::string_view file, std::string_view csv) {
  size_t end = file.find("_track");
  if (end == std::string_view::npos) return;
  Drop drop{std::string(file.substr(0, end)), uint64_t(Fnv1(file.substr(0, end + 6))) * 10000000000ull, {}};
  bool header = true;
  while (!csv.empty()) {
    size_t eol = csv.find('\n');
    std::string_view line = csv.substr(0, eol);
    csv = eol == std::string_view::npos ? std::string_view() : csv.substr(eol + 1);
    if (header) { header = false; continue; }
    if (!line.empty() && line[0] >= '0' && line[0] <= '9') drop.ids.insert(uint32_t(std::atoi(std::string(line).c_str())));
  }
  if (drop.ids.empty()) return;
  for (Drop& d : drops) {
    if (d.code == drop.code) { d = std::move(drop); return; }
  }
  drops.push_back(std::move(drop));
  // In game order: the code ends in two digits for the range and two for the drop.
  auto digits = [](const Drop& d) { return d.code.substr(d.code.size() - std::min<size_t>(4, d.code.size())); };
  std::sort(drops.begin(), drops.end(), [&](const Drop& a, const Drop& b) { return digits(a) < digits(b); });
}

// Range names by the drop codes' prefix.
const struct { std::string_view prefix, name; } kRanges[] = {
    {"alps", "Alps"},        {"patagon", "Patagonia"}, {"africa", "Africa"},
    {"siberia", "Siberia"},  {"alaska", "Alaska"},     {"himwest", "Himalayas"},
    {"rockies", "Rockies"},  {"antarc", "Antarctica"}, {"newzeal", "New Zealand"},
};

}  // namespace

// The game's count of one snowflake list: r3 the geotag lists (owner of the collected set), r4/r5
// the list's text; the caller's frame holds the file name at +352 (both game versions).
REX_EXTERN(SSX_FN_GEOTAG_COUNT_IMP);
REX_HOOK_RAW(SSX_FN_GEOTAG_COUNT) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    guest = base;
    owner = ctx.r3.u32;
    std::string_view file(reinterpret_cast<const char*>(base + ctx.r1.u32 + 352));
    AddDrop(file, std::string_view(reinterpret_cast<const char*>(base + ctx.r4.u32), ctx.r5.u32));
  }
  SSX_FN_GEOTAG_COUNT_IMP(ctx, base);
}

std::vector<SsxSnowflakeDrop> SsxSnowflakeCounts() {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<SsxSnowflakeDrop> out;
  if (drops.empty() || !guest) return out;
  uint8_t* base = guest;

  // The collected set. The game may change it meanwhile (a pickup); a bounded walk over guest
  // heap addresses at worst miscounts until the next look.
  std::unordered_map<uint32_t, size_t> by_low;  // low 32 bits of a drop's base -> drop
  for (size_t i = 0; i < drops.size(); ++i) by_low[uint32_t(drops[i].base)] = i;
  std::vector<int> collected(drops.size());
  int unmatched = 0;
  const uint32_t set = owner + 24, anchor = set + 4;
  std::vector<uint32_t> stack{REX_LOAD_U32(set + 12)};
  for (int visited = 0; !stack.empty() && visited < 4096; ++visited) {
    uint32_t node = stack.back();
    stack.pop_back();
    if (node < 0x40000000u || node >= 0xE0000000u || node == anchor) continue;
    uint64_t id = REX_LOAD_U64(node + 16);
    auto it = by_low.find(uint32_t(id));
    if (it != by_low.end() && drops[it->second].ids.count(uint32_t((id - drops[it->second].base) >> 32))) {
      ++collected[it->second];
    } else {
      ++unmatched;
    }
    stack.push_back(REX_LOAD_U32(node + 0));
    stack.push_back(REX_LOAD_U32(node + 4));
  }

  for (size_t i = 0; i < drops.size(); ++i) {
    const Drop& d = drops[i];
    SsxSnowflakeDrop row;
    row.range = d.code;
    for (const auto& r : kRanges) {
      if (d.code.rfind(r.prefix, 0) == 0) row.range = r.name;
    }
    row.drop = "Drop " + (d.code.size() >= 2 ? d.code.substr(d.code.size() - 2) : d.code);
    row.collected = collected[i];
    row.total = int(d.ids.size());
    out.push_back(std::move(row));
  }
  // Collected ones on drops without a list (the profile has some on rockies0706, which the game's
  // own count of 599 leaves out but its "N collected" includes).
  if (unmatched) out.push_back({"Other", "", unmatched, 0});
  return out;
}
