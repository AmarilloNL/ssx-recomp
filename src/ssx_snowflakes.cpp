// ssx - snowflake tracker (issue #9).
//
// Snowflakes are EA's offline geotags: each drop that has them comes with a list (a CSV in the game
// data, <drop>_track_geotagdb.csv: Id,Quality,Level,XPos,YPos,ZPos after a header line). At startup
// the game counts them all (599) through one function per list; hooking it gives every drop's list.
// The profile keeps the collected ones as a set of 64-bit ids,
//   id = (Id << 32) + fnv1_32("<drop>_track") * 10^10   (mod 2^64),
// an rb-tree (root at +12, node: right +0, left +4, key +16) at +24 of the object the counting function
// gets in r3, its size at +44. Nothing here writes to the game.
//
// Names: each drop's database record holds fnv1_32("<drop>_track") at +4 and, at +0x24, a pointer to the
// key of its name (TXT_NM_DRP_<RANGE>_<peak>_<drop>; the peak's is TXT_NM_PEA_<RANGE>_<peak>). The
// records are found by a scan of the game's heap, the names by the game's own localize.

#include "ssx_snowflakes.h"

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"
#include "ssx_guest_memory.h"

REX_EXTERN(SSX_FN_LOCALIZE);

namespace {

struct Drop {
  std::string code;  // e.g. rockies0705
  uint32_t key;      // fnv1_32("<code>_track")
  uint64_t base;     // key * 10^10
  std::unordered_set<uint32_t> ids;
  bool listed = true;  // in the game's lists (its 599); else only known from collected ones
  std::vector<std::string> name_keys;  // of the drops (events) on this track
  std::string peak, name;  // in the game's language, once found
};

std::mutex mutex;
std::vector<Drop> drops;
int name_attempts = 0;  // of the name lookup; back to 0 when a drop is added
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
  uint32_t key = Fnv1(file.substr(0, end + 6));
  Drop drop{std::string(file.substr(0, end)), key, uint64_t(key) * 10000000000ull, {}, true, {}, {}, {}};
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

// The range's place in the game's order (a code's first two digits), kRanges' size if unknown.
size_t RangeIndex(const std::string& code) {
  for (size_t i = 0; i < std::size(kRanges); ++i) {
    if (code.rfind(kRanges[i].prefix, 0) == 0) return i;
  }
  return std::size(kRanges);
}

// Every drop code there could be (<range prefix><range number><drop number>), by the low 32 bits of
// its ids, for collected ones on drops without a list.
const std::unordered_map<uint32_t, std::string>& CandidateCodes() {
  static const std::unordered_map<uint32_t, std::string> codes = [] {
    std::unordered_map<uint32_t, std::string> out;
    for (size_t r = 0; r < std::size(kRanges); ++r) {
      for (int n = 1; n <= 40; ++n) {
        char code[32];
        std::snprintf(code, sizeof(code), "%s%02d%02d", std::string(kRanges[r].prefix).c_str(), int(r + 1), n);
        out[uint32_t(uint64_t(Fnv1(std::string(code) + "_track")) * 10000000000ull)] = code;
      }
    }
    return out;
  }();
  return codes;
}

// The game's localize, into a string object made here. The game's copy of the text is never freed
// (a few hundred bytes, once). Runs guest code: game thread only.
std::string Localize(PPCContext& ctx, uint8_t* base, uint32_t loc, const std::string& key) {
  if (key.empty() || key.size() > 60) return {};
  const PPCContext saved = ctx;
  ctx.r1.u32 = saved.r1.u32 - 512;  // a frame of our own below the caller's
  const uint32_t s = ctx.r1.u32 + 128, empty = s + 16, k = s + 32;
  REX_STORE_U8(empty, 0);
  for (size_t i = 0; i <= key.size(); ++i) REX_STORE_U8(k + i, i < key.size() ? uint8_t(key[i]) : 0);
  REX_STORE_U32(s, empty);  // eastl string: begin, end, capacity end, allocator
  REX_STORE_U32(s + 4, empty);
  REX_STORE_U32(s + 8, empty + 1);
  REX_STORE_U32(s + 12, 0);
  ctx.r3.u64 = loc;
  ctx.r4.u64 = s;
  ctx.r5.u64 = k;
  ctx.r6.u64 = 1;
  SSX_FN_LOCALIZE(ctx, base);
  uint32_t b = REX_LOAD_U32(s), e = REX_LOAD_U32(s + 4);
  std::string text = e > b && e - b < 256 ? std::string(reinterpret_cast<const char*>(base + b), e - b) : "";
  ctx = saved;
  return text == key ? "" : text;  // (a key without text comes back as itself)
}

// "BROKEN PASS" -> "Broken Pass" (the game's text is in capitals; its menus show it in another font).
// Words with a digit stay as they are ("JT2").
std::string TitleCase(std::string s) {
  size_t word = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i < s.size() && s[i] != ' ' && s[i] != '-' && s[i] != '/') continue;
    if (std::none_of(s.begin() + word, s.begin() + i, [](char c) { return c >= '0' && c <= '9'; })) {
      for (size_t k = word + 1; k < i; ++k) {
        if (s[k] >= 'A' && s[k] <= 'Z') s[k] = char(s[k] - 'A' + 'a');
      }
    }
    word = i + 1;
  }
  return s;
}

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

}  // namespace

void SsxSnowflakesTick(PPCContext& ctx, uint8_t* base) {
  using clock = std::chrono::steady_clock;
  static clock::time_point next;
  if (name_attempts >= 24 || clock::now() < next) return;  // every 5 s, for 2 minutes at most
  next = clock::now() + std::chrono::seconds(5);
  std::lock_guard<std::mutex> lock(mutex);
  uint32_t loc = REX_LOAD_U32(SSX_LOC_MANAGER_PTR);
  if (drops.empty() || !loc) return;
  ++name_attempts;

  // The drop records of each track: the track's key, and 0x20 on a pointer to the name's key. A track
  // can have several (World Tour and Explore drops, unused ones).
  std::unordered_map<uint32_t, Drop*> by_key;
  for (Drop& d : drops) {
    if (!d.name.empty()) continue;
    d.name_keys.clear();
    by_key[d.key] = &d;
  }
  rex::memory::Memory* memory = rex::system::kernel_memory();
  for (uint32_t heap : {0xA0000000u, 0x40000000u}) {
    SsxScanGuestHeap(memory, heap, 0x24, [&](uint32_t, const uint8_t* chunk, uint32_t n) {
      for (uint32_t at = 0; at + 0x24 <= n && at < (1u << 20); at += 4) {
        auto it = by_key.find(Be32(chunk + at));
        if (it == by_key.end()) continue;
        char text[64] = {};
        uint32_t address = Be32(chunk + at + 0x20);
        std::vector<std::string>& keys = it->second->name_keys;
        if (SsxGuestReadable(memory, address, sizeof(text) - 1) &&
            SsxCopyHost(text, memory->TranslateVirtual<const char*>(address), sizeof(text) - 1) &&
            std::strncmp(text, "TXT_NM_DRP_", 11) == 0 && std::find(keys.begin(), keys.end(), text) == keys.end()) {
          keys.push_back(text);
        }
      }
    });
  }
  for (const Drop& d : drops) {
    if (d.name.empty() && d.name_keys.empty() && name_attempts < 24) return;  // database not (all) loaded yet
  }
  name_attempts = 24;
  for (Drop& d : drops) {
    if (!d.name.empty()) continue;
    for (const std::string& key : d.name_keys) {
      std::string name = TitleCase(Localize(ctx, base, loc, key));
      if (name.empty() || name == "Not Used" || d.name.find(name) != std::string::npos) continue;
      d.name += (d.name.empty() ? "" : " / ") + name;
      // The peak: TXT_NM_PEA_<RANGE>_<peak>, from TXT_NM_DRP_<RANGE>_<peak>_<drop>.
      if (d.peak.empty()) {
        std::string peak = key.substr(0, key.rfind('_'));
        d.peak = TitleCase(Localize(ctx, base, loc, peak.replace(0, 11, "TXT_NM_PEA_")));
      }
    }
    REXLOG_DEBUG("Snowflakes: {} = {} / {}", d.code, d.peak, d.name);
  }
}

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
  std::vector<uint64_t> ids;
  const uint32_t set = owner + 24, anchor = set + 4;
  std::vector<uint32_t> stack{REX_LOAD_U32(set + 12)};
  for (int visited = 0; !stack.empty() && visited < 4096; ++visited) {
    uint32_t node = stack.back();
    stack.pop_back();
    if (node < 0x40000000u || node >= 0xE0000000u || node == anchor) continue;
    ids.push_back(REX_LOAD_U64(node + 16));
    stack.push_back(REX_LOAD_U32(node + 0));
    stack.push_back(REX_LOAD_U32(node + 4));
  }

  // Collected ones on drops without a list (a save can hold some on rockies0706, which the game's
  // own 599 leaves out but its "N collected" counts) get a drop of their own, found by code.
  for (uint64_t id : ids) {
    bool known = std::any_of(drops.begin(), drops.end(), [&](const Drop& d) { return uint32_t(d.base) == uint32_t(id); });
    auto code = CandidateCodes().find(uint32_t(id));
    if (known || code == CandidateCodes().end()) continue;
    uint32_t key = Fnv1(code->second + "_track");
    drops.push_back({code->second, key, uint64_t(key) * 10000000000ull, {}, false, {}, {}, {}});
    name_attempts = 0;
  }

  for (const Drop& d : drops) {
    SsxSnowflakeDrop row;
    size_t range = RangeIndex(d.code);
    row.range = range < std::size(kRanges) ? std::string(kRanges[range].name) : d.code;
    row.peak = d.peak;
    row.drop = !d.name.empty() ? d.name : "Drop " + d.code.substr(d.code.size() - std::min<size_t>(2, d.code.size()));
    row.listed = d.listed;
    row.total = int(d.ids.size());
    for (uint64_t id : ids) {
      if (uint32_t(id) == uint32_t(d.base) && (!d.listed || d.ids.count(uint32_t((id - d.base) >> 32)))) ++row.collected;
    }
    out.push_back(std::move(row));
  }
  // Range in the game's order, then peak and drop by name.
  auto range_order = [](const std::string& name) {
    for (size_t i = 0; i < std::size(kRanges); ++i) {
      if (kRanges[i].name == name) return i;
    }
    return std::size(kRanges);
  };
  std::stable_sort(out.begin(), out.end(), [&](const SsxSnowflakeDrop& a, const SsxSnowflakeDrop& b) {
    return std::make_tuple(range_order(a.range), a.peak, a.drop) < std::make_tuple(range_order(b.range), b.peak, b.drop);
  });
  return out;
}
