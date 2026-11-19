// ssx - cheats (issue #21), in F1 > Cheats.
//
// The rider's boost and Tricky state is updated once a frame by one function, whose object holds
// pointers to the values (the same values the HUD shows):
//   +76  the tuning record: +540 the boost maximum
//   +120 Tricky on (byte), +124 Super Tricky on (byte); both worked out at the end of the update
//   +128 the Tricky timer (Tricky is on while it is above 0), +132 the same from 0 to 1 (the HUD's)
//   +136 the Super Tricky timer, +140 the same from 0 to 1
//   +144 the Super Tricky meter (Super Tricky while it is 1 during Tricky)
//   +148 the boost, +152 the boost from 0 to 1 (the HUD's meter)
//   +496, +504, +508: switches (bytes) the update only reads: +504 keeps Tricky and Super Tricky
//   full, +496 keeps Tricky full, and +508 with it clears Super Tricky. Tricky's length depends on
//   more than the tuning record, so the always-Tricky cheats turn these on for the game's own
//   update and put them back after it.
// Nothing is written to the save: turning a cheat off hands the values back to the game.

#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_boost, false, "SSX", "Cheat: boost never runs out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_credits, false, "SSX", "Cheat: buying costs nothing, the stores show 9,999,999");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_rewinds, false, "SSX", "Cheat: rewinds are never used up");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_gear, false, "SSX",
                    "Cheat: armour, oxygen, core temperature and the headlamp and pulse goggles batteries never run out");
REXCVAR_DEFINE_INT32(ssx_cheat_tricky, 0, "SSX",
                     "Cheat: 0 Tricky as normal, 1 always Tricky, 2 always Super Tricky, 3 never Tricky");

namespace {
enum TrickyCheat { kNormal = 0, kAlways = 1, kAlwaysSuper = 2, kNever = 3 };

// Turns a switch byte on for the length of the game's update; the destructors put them back in
// reverse order, so two switches at the same address still end as they were.
struct ScopedSwitch {
  uint32_t address = 0;
  uint8_t* base = nullptr;
  uint8_t old = 0;
  void On(uint8_t* b, uint32_t a) {
    if (!a) return;
    base = b;
    address = a;
    old = REX_LOAD_U8(a);
    REX_STORE_U8(a, 1);
  }
  ~ScopedSwitch() {
    if (address) REX_STORE_U8(address, old);
  }
};

void StoreFloat(uint8_t* base, uint32_t field, uint32_t bits) {
  if (field) REX_STORE_U32(field, bits);
}
}  // namespace

REX_EXTERN(SSX_FN_BOOST_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_BOOST_UPDATE) {
  const uint32_t self = ctx.r3.u32;
  const int tricky = REXCVAR_GET(ssx_cheat_tricky);
  {
    ScopedSwitch full, no_super;
    if (tricky == kAlwaysSuper) {
      full.On(base, REX_LOAD_U32(self + 504));
    } else if (tricky == kAlways) {
      full.On(base, REX_LOAD_U32(self + 496));
      no_super.On(base, REX_LOAD_U32(self + 508));
    }
    SSX_FN_BOOST_UPDATE_IMP(ctx, base);
  }  // the switches are back as the game had them

  if (tricky == kNever) {
    // Tricky ends at once. Boost still fills up to full; only the overflow past full starts Tricky,
    // and a full boost doesn't overflow, so the game doesn't start it again on its own.
    for (uint32_t field : {128u, 132u, 136u, 140u, 144u}) StoreFloat(base, REX_LOAD_U32(self + field), 0);
    for (uint32_t field : {120u, 124u}) {
      if (uint32_t flag = REX_LOAD_U32(self + field)) REX_STORE_U8(flag, 0);
    }
  }
  if (REXCVAR_GET(ssx_cheat_infinite_boost)) {
    const uint32_t tuning = REX_LOAD_U32(self + 76);
    if (tuning) StoreFloat(base, REX_LOAD_U32(self + 148), REX_LOAD_U32(tuning + 540));  // full boost
    StoreFloat(base, REX_LOAD_U32(self + 152), 0x3F800000u);  // 1.0f: the HUD's meter full too
  }
}

// Infinite credits. Every change to the profile's credits goes through one function (a purchase adds a
// negative amount), which also updates the credits the menus show: with the cheat on, a purchase adds
// nothing. The menus and stores read the credits through a getter that reports at least 9,999,999, so
// everything is affordable; adding reads the profile itself, so the saved credits stay the real ones
// (and rewards still add to them).
namespace {
constexpr int32_t kCheatCredits = 9999999;
}

REX_EXTERN(SSX_FN_CHANGE_CREDITS_IMP);
REX_HOOK_RAW(SSX_FN_CHANGE_CREDITS) {
  if (REXCVAR_GET(ssx_cheat_infinite_credits) && int32_t(ctx.r4.u32) < 0) ctx.r4.u64 = 0;
  SSX_FN_CHANGE_CREDITS_IMP(ctx, base);
}

REX_EXTERN(SSX_FN_GET_CREDITS_IMP);
REX_HOOK_RAW(SSX_FN_GET_CREDITS) {
  SSX_FN_GET_CREDITS_IMP(ctx, base);
  if (REXCVAR_GET(ssx_cheat_infinite_credits) && int32_t(ctx.r3.u32) < kCheatCredits) ctx.r3.u64 = kCheatCredits;
}

// Infinite rewinds: a rewind is never taken off the count (the drops that limit them, like Survive It,
// keep the count they start with).
REX_EXTERN(SSX_FN_USE_REWIND_IMP);
REX_HOOK_RAW(SSX_FN_USE_REWIND) {
  if (REXCVAR_GET(ssx_cheat_infinite_rewinds)) return;
  SSX_FN_USE_REWIND_IMP(ctx, base);
}

// Infinite armour, oxygen and batteries: the rider's armour, oxygen and headlamp and pulse goggles batteries
// are stats from 0 to 1 that the game takes off directly (a hit, a frame of breathing or light); right after
// each frame's HUD update they're full again. -1 means not in use (between rides): left alone.
namespace {
constexpr uint32_t kGearStats[] = {148 /* FLT_Armour */, 166 /* FLT_HeadLampBatteryLevel */, 190 /* FLT_OxygenLevel */,
                                   196 /* FLT_PulseGogglesBatteryLevel */};
}

REX_EXTERN(SSX_FN_HUD_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_HUD_UPDATE) {
  const uint32_t hud = ctx.r3.u32;
  SSX_FN_HUD_UPDATE_IMP(ctx, base);
  if (!REXCVAR_GET(ssx_cheat_infinite_gear)) return;
  const uint32_t stats = REX_LOAD_U32(hud + 144);
  if (!stats) return;
  for (uint32_t id : kGearStats) {
    const uint32_t value = REX_LOAD_U32(stats + (id + 1) * 4);
    if (!value) continue;
    const uint32_t bits = REX_LOAD_U32(value);
    float level;
    std::memcpy(&level, &bits, 4);
    if (level >= 0.0f && level < 1.0f) REX_STORE_U32(value, 0x3F800000u);  // 1.0f
  }
}

// ...and the cold: the rider's core temperature can't go above a maximum that drops with the cold taken
// so far, so right after each frame's cold update that cold is back to none and the temperature full.
REX_EXTERN(SSX_FN_COLD_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_COLD_UPDATE) {
  const uint32_t self = ctx.r3.u32;
  SSX_FN_COLD_UPDATE_IMP(ctx, base);
  if (!REXCVAR_GET(ssx_cheat_infinite_gear)) return;
  if (const uint32_t cold = REX_LOAD_U32(self + 0x2019C)) REX_STORE_U32(cold, 0);  // 0.0f
  if (const uint32_t max = REX_LOAD_U32(self + 0x20194)) REX_STORE_U32(max, 0x3F800000u);
  if (const uint32_t temperature = REX_LOAD_U32(self + 0x20190)) REX_STORE_U32(temperature, 0x3F800000u);
}
