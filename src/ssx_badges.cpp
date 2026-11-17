// ssx - badges that can't be earned without EA's servers or other players (issue #11).
//
// The game's script (init_badges.lua) defines every badge by name; its key is {fnv1_32(name), 1}.
// Whether the profile has a badge is asked through one function (profile stats, key, kind 2), also by
// the badge award before it sets the flag. With the setting on, that answer is yes for the badges below:
// the menus show them as earned. Nothing is written to the save, so turning it off restores the
// profile's own state, and no achievements are triggered (those are checked when a badge is awarded).

#include <cstdint>
#include <string>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

REXCVAR_DEFINE_BOOL(ssx_online_badges, false, "SSX",
                    "Show the badges that need EA's servers (Explore, Gear, Global Events) as earned");

namespace {

uint32_t Fnv1(const std::string& s) {
  uint32_t h = 0x811C9DC5u;
  for (unsigned char c : s) h = (h * 0x01000193u) ^ c;
  return h;
}

// Ghost credits, friends' records and never-collected geotags (Explore), placing geotags (Gear), and
// every Global Events badge; each in bronze, silver and gold.
const std::unordered_set<uint32_t>& OnlineBadges() {
  static const std::unordered_set<uint32_t> keys = [] {
    std::unordered_set<uint32_t> out;
    for (const char* badge : {"explore_badge_7", "explore_badge_8", "explore_badge_9", "explore_badge_10",
                              "gear_badge_2", "globalevents_badge_1", "globalevents_badge_2",
                              "globalevents_badge_3", "globalevents_badge_4", "globalevents_badge_5",
                              "globalevents_badge_6", "globalevents_badge_7", "globalevents_badge_8",
                              "globalevents_badge_9", "globalevents_badge_10", "globalevents_badge_12"}) {
      for (const char* tier : {"_bronze", "_silver", "_gold"}) out.insert(Fnv1(std::string(badge) + tier));
    }
    return out;
  }();
  return keys;
}

}  // namespace

// Has the profile this flag (r3 stats, r4 key, r5 kind; 2 = badge)? Returns a bool in r3.
REX_EXTERN(SSX_FN_PROFILE_HAS_IMP);
REX_HOOK_RAW(SSX_FN_PROFILE_HAS) {
  if (ctx.r5.u32 == 2 && REXCVAR_GET(ssx_online_badges) && OnlineBadges().count(REX_LOAD_U32(ctx.r4.u32))) {
    ctx.r3.u64 = 1;
    return;
  }
  SSX_FN_PROFILE_HAS_IMP(ctx, base);
}
