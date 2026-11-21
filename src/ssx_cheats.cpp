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
// These values aren't saved: turning a cheat off hands them back to the game.

#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_boost, false, "SSX", "Cheat: boost never runs out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_credits, false, "SSX", "Cheat: buying costs nothing, the stores show 9,999,999");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_rewinds, false, "SSX", "Cheat: rewinds are never used up");
REXCVAR_DEFINE_BOOL(ssx_cheat_no_rewinds, false, "SSX", "Cheat: rewinds can't be used");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_lift, false, "SSX", "Cheat: a wingsuit's lift never fades during a flight");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_headlamp, false, "SSX", "Cheat: the headlamp's battery never runs out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_goggles, false, "SSX", "Cheat: the pulse goggles' battery never runs out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_wingsuit, false, "SSX", "Cheat: the wingsuit never wears out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_armour, false, "SSX", "Cheat: armour never wears out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_ice_axes, false, "SSX", "Cheat: ice axes never wear out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_oxygen, false, "SSX", "Cheat: oxygen (and the oxygen tanks) never run out");
REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_warmth, false, "SSX", "Cheat: the cold never lowers the core temperature");
REXCVAR_DEFINE_BOOL(ssx_cheat_keep_combo, false, "SSX", "Cheat: a wipeout banks the combo instead of losing it");
REXCVAR_DEFINE_BOOL(ssx_cheat_keep_multiplier, false, "SSX", "Cheat: the trick multiplier never goes down");
REXCVAR_DEFINE_BOOL(ssx_cheat_max_gear, false, "SSX", "Cheat: every gear stat at its best");
REXCVAR_DEFINE_BOOL(ssx_cheat_big_head, false, "SSX", "Cheat: a big head");
REXCVAR_DEFINE_BOOL(ssx_cheat_big_hands, false, "SSX", "Cheat: big hands");
REXCVAR_DEFINE_BOOL(ssx_cheat_glowing, false, "SSX", "Cheat: a glowing suit");
REXCVAR_DEFINE_BOOL(ssx_cheat_ride_uphill, false, "SSX", "Cheat: no push back when leaving the course");
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

float LoadFloat(uint8_t* base, uint32_t field) {
  const uint32_t bits = REX_LOAD_U32(field);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

uint32_t FloatBits(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  return bits;
}

constexpr float kBoostFloor = 0.05f;  // infinite boost: the least boost left, a part of full
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
    // Boost is used and filled as normal (so tricks and flare start Tricky as usual), but never drops
    // below a sliver: boosting never stops, and the end of Tricky doesn't leave a full meter.
    const uint32_t tuning = REX_LOAD_U32(self + 76), raw = REX_LOAD_U32(self + 148), meter = REX_LOAD_U32(self + 152);
    if (tuning && raw && meter) {
      const float floor = kBoostFloor * LoadFloat(base, tuning + 540);
      if (LoadFloat(base, raw) < floor) {
        StoreFloat(base, raw, FloatBits(floor));
        StoreFloat(base, meter, FloatBits(kBoostFloor));  // the HUD's meter, 0 to 1
      }
    }
  }
}

// Infinite credits. Every change to the profile's credits goes through one function (a purchase adds a
// negative amount), which also updates the credits the menus show: with the cheat on, a purchase adds
// nothing. The menus and stores read the credits through a getter that reports at least 9,999,999, so
// everything is affordable; adding reads the profile itself, so the saved credits stay the real ones
// (and rewards still add to them).
//
// +1,000,000 credits (a button in F1 > Cheats) is a gift that stays: the next time the game reads the credits
// (on its own thread), the profile gets it through the game's own add, like a reward, and the menus their
// new credits. It isn't counted as earned.
namespace {
constexpr int32_t kCheatCredits = 9999999;
constexpr uint32_t kGiftCredits = 1000000;
std::atomic<int> credit_gifts{0};
}  // namespace

void SsxCheatsGiveCredits() { credit_gifts.fetch_add(1); }

REX_EXTERN(SSX_FN_CHANGE_CREDITS_IMP);
REX_HOOK_RAW(SSX_FN_CHANGE_CREDITS) {
  if (REXCVAR_GET(ssx_cheat_infinite_credits) && int32_t(ctx.r4.u32) < 0) ctx.r4.u64 = 0;
  SSX_FN_CHANGE_CREDITS_IMP(ctx, base);
}

REX_EXTERN(SSX_FN_GET_CREDITS_IMP);
REX_EXTERN(SSX_FN_ADD_CREDITS);
REX_EXTERN(SSX_FN_MENU_SET_CREDITS);
REX_HOOK_RAW(SSX_FN_GET_CREDITS) {
  const int gifts = credit_gifts.exchange(0);
  const PPCContext saved = ctx;
  ctx.r1.u32 = saved.r1.u32 - 256;  // a frame of our own below the caller's
  for (int i = 0; i < gifts; ++i) {
    ctx.r3.u64 = saved.r3.u32;  // the profile
    ctx.r4.u64 = kGiftCredits;
    ctx.r5.u64 = 0;  // not earned
    SSX_FN_ADD_CREDITS(ctx, base);
  }
  ctx = saved;
  SSX_FN_GET_CREDITS_IMP(ctx, base);
  if (REXCVAR_GET(ssx_cheat_infinite_credits) && int32_t(ctx.r3.u32) < kCheatCredits) ctx.r3.u64 = kCheatCredits;
  if (const uint32_t menus = gifts ? REX_LOAD_U32(SSX_MENU_DATA) : 0) {
    const PPCContext got = ctx;
    ctx.r1.u32 = got.r1.u32 - 256;
    const uint32_t value = ctx.r1.u32 + 128;
    REX_STORE_U32(value, got.r3.u32);
    ctx.r3.u64 = menus;
    ctx.r4.u64 = SSX_MENU_ID_CREDITS;
    ctx.r5.u64 = value;
    SSX_FN_MENU_SET_CREDITS(ctx, base);
    ctx = got;
  }
}

// Infinite rewinds: a rewind is never taken off the count (the drops that limit them, like Survive It,
// keep the count they start with).
REX_EXTERN(SSX_FN_USE_REWIND_IMP);
REX_HOOK_RAW(SSX_FN_USE_REWIND) {
  if (REXCVAR_GET(ssx_cheat_infinite_rewinds)) return;
  SSX_FN_USE_REWIND_IMP(ctx, base);
}

// No rewinds: the game's "can the rider rewind now?" check always answers no, as it does when a drop's
// rewinds are used up. LB still starts the slow-down that comes before a rewind (time dilation); only the
// rewind itself doesn't follow.
REX_EXTERN(SSX_FN_CAN_REWIND_IMP);
REX_HOOK_RAW(SSX_FN_CAN_REWIND) {
  SSX_FN_CAN_REWIND_IMP(ctx, base);
  if (REXCVAR_GET(ssx_cheat_no_rewinds)) ctx.r3.u64 = 0;
}

// Infinite gear: the rider's armour, oxygen (and the tank's), the wingsuit's and ice axes' effectiveness and
// the headlamp and pulse goggles batteries are stats from 0 to 1 that the game takes off directly (a hit, a
// frame of breathing, flying, digging in or light); right before each frame's HUD update the ones with their
// cheat on are full again, so the meters never show the wear of the frame before. -1 means not in use
// (between rides): left alone.
// With infinite lift (below) the wingsuit meter (232, read only by the HUD and the rewind snapshot) stays at 0;
// it's also cleared here before the HUD shows it, so the bar stays full.
namespace {
void TopUpGear(uint8_t* base, uint32_t stats) {
  const struct {
    uint32_t id;
    bool on;
  } gear[] = {
      {148 /* FLT_Armour */, REXCVAR_GET(ssx_cheat_infinite_armour)},
      {166 /* FLT_HeadLampBatteryLevel */, REXCVAR_GET(ssx_cheat_infinite_headlamp)},
      {169 /* FLT_IceAxeEffectiveness */, REXCVAR_GET(ssx_cheat_infinite_ice_axes)},
      {190 /* FLT_OxygenLevel */, REXCVAR_GET(ssx_cheat_infinite_oxygen)},
      {191 /* FLT_OxygenTankLevel */, REXCVAR_GET(ssx_cheat_infinite_oxygen)},
      {196 /* FLT_PulseGogglesBatteryLevel */, REXCVAR_GET(ssx_cheat_infinite_goggles)},
      {231 /* FLT_WingsuitEffectiveness */, REXCVAR_GET(ssx_cheat_infinite_wingsuit)},
  };
  for (const auto& [id, on] : gear) {
    if (!on) continue;
    const uint32_t value = REX_LOAD_U32(stats + (id + 1) * 4);
    if (!value) continue;
    const float level = LoadFloat(base, value);
    if (level >= 0.0f && level < 1.0f) REX_STORE_U32(value, 0x3F800000u);  // 1.0f
  }
}
}  // namespace

REX_EXTERN(SSX_FN_HUD_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_HUD_UPDATE) {
  if (const uint32_t stats = REX_LOAD_U32(ctx.r3.u32 + 144)) {
    TopUpGear(base, stats);
    if (const uint32_t meter = REX_LOAD_U32(stats + (232 + 1) * 4);  // FLT_WingsuitUsePercentage
        meter && REXCVAR_GET(ssx_cheat_infinite_lift)) {
      const float used = LoadFloat(base, meter);
      if (used > 0.0f && used <= 1.0f) REX_STORE_U32(meter, 0);  // 0.0f
    }
  }
  SSX_FN_HUD_UPDATE_IMP(ctx, base);
}

// ...and the cold: the rider's core temperature can't go above a maximum that drops with the cold taken
// so far, so right after each frame's cold update that cold is back to none and the temperature full.
REX_EXTERN(SSX_FN_COLD_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_COLD_UPDATE) {
  const uint32_t self = ctx.r3.u32;
  SSX_FN_COLD_UPDATE_IMP(ctx, base);
  if (!REXCVAR_GET(ssx_cheat_infinite_warmth)) return;
  auto level = [&](uint32_t value) {
    const uint32_t bits = REX_LOAD_U32(value);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  };
  // Like the stats above, only values in use (0 to 1) are changed.
  const uint32_t cold = REX_LOAD_U32(self + 0x2019C), max = REX_LOAD_U32(self + 0x20194),
                 temperature = REX_LOAD_U32(self + 0x20190);
  if (cold && level(cold) > 0.0f) REX_STORE_U32(cold, 0);  // 0.0f: no cold taken
  for (uint32_t value : {max, temperature}) {
    if (value && level(value) >= 0.0f && level(value) < 1.0f) REX_STORE_U32(value, 0x3F800000u);  // 1.0f
  }
}

// Never lose a combo: when a wipeout would lose the combo (the game's own test, with tricks in it), the
// combo is banked first through the game's own end of combo (the bonus, the HUD's "banked"); the wipeout
// then has nothing left to lose, and its report of that empty combo is left out. The rest of the wipeout
// (Tricky ends, boost) is as normal.
namespace {
thread_local bool combo_banked = false;
}

REX_EXTERN(SSX_FN_SCORE_ADD);
REX_EXTERN(SSX_FN_SCORE_WIPEOUT_IMP);
REX_HOOK_RAW(SSX_FN_SCORE_WIPEOUT) {
  const uint32_t self = ctx.r3.u32;
  auto flag = [&](uint32_t field) {
    const uint32_t p = REX_LOAD_U32(self + field);
    return p && REX_LOAD_U8(p);
  };
  const uint32_t count = REX_LOAD_U32(self + 264);
  if (REXCVAR_GET(ssx_cheat_keep_combo) && flag(300) && flag(308) && count && int32_t(REX_LOAD_U32(count)) > 0) {
    const PPCContext saved = ctx;
    ctx.f1.f64 = 0.0;  // no points of its own
    ctx.r5.u64 = 1;    // the combo's time is up
    SSX_FN_SCORE_ADD(ctx, base);
    ctx = saved;
    combo_banked = true;
  }
  SSX_FN_SCORE_WIPEOUT_IMP(ctx, base);
  combo_banked = false;
}

REX_EXTERN(SSX_FN_COMBO_ENDED_IMP);
REX_HOOK_RAW(SSX_FN_COMBO_ENDED) {
  if (combo_banked && ctx.r5.u32 == 0) return;  // the wipeout's report of the combo just banked
  SSX_FN_COMBO_ENDED_IMP(ctx, base);
}

// Never lose the multiplier: after the game's flow update a level above 0 can only go up (a new ride
// starts from 0 elsewhere, as normal).
REX_EXTERN(SSX_FN_FLOW_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_FLOW_UPDATE) {
  const uint32_t level = REX_LOAD_U32(ctx.r3.u32 + 596);
  const int32_t before = level ? int32_t(REX_LOAD_U32(level)) : 0;
  SSX_FN_FLOW_UPDATE_IMP(ctx, base);
  if (REXCVAR_GET(ssx_cheat_keep_multiplier) && before > 0 && int32_t(REX_LOAD_U32(level)) < before) {
    REX_STORE_U32(level, uint32_t(before));
  }
}

// Max gear stats, big head and hands, glowing: the outfit's points for a gear stat are read through one
// function; with a cheat on it answers the full points (a stat's maximum) for those stats. Nothing of the
// outfit or the save is changed.
namespace {
constexpr uint32_t kFullPoints = 100;
enum GearStat : uint32_t { kLastGearStat = 25, kGlowing = 30, kBigHead = 34, kBigHands = 35, kBigHeadAndHands = 36 };
}  // namespace

REX_EXTERN(SSX_FN_GEAR_STAT_IMP);
REX_HOOK_RAW(SSX_FN_GEAR_STAT) {
  const uint32_t stat = ctx.r4.u32;
  SSX_FN_GEAR_STAT_IMP(ctx, base);
  const bool head = REXCVAR_GET(ssx_cheat_big_head), hands = REXCVAR_GET(ssx_cheat_big_hands);
  const bool full = (stat <= kLastGearStat && REXCVAR_GET(ssx_cheat_max_gear)) ||
                    (stat == kGlowing && REXCVAR_GET(ssx_cheat_glowing)) || (stat == kBigHead && head) ||
                    (stat == kBigHands && hands) || (stat == kBigHeadAndHands && head && hands);
  if (full && (ctx.r3.u32 & 0xFF) < kFullPoints) ctx.r3.u64 = kFullPoints;
}

// Infinite lift: the flight node reads its three time curves at the share of the flight flown so far (from 0
// at the start; the flight ends near 1), and publishes that share as the wingsuit meter. With the cheat on the
// curves are read at the start of a flight every frame, so the meter stays at 0 too (as when the time flown was
// held at 0 before, see the HUD update above for the bar), and the time flown itself is held at half the
// flight (scaled back each frame by the share the node had worked out), so the flight never ends either way.
// Holding the time at 0, as before, also stopped the steering.
namespace {
constexpr float kLiftHold = 0.5f;  // the share of the flight the time flown is held at

struct Flight {
  uint32_t time_port = 0;
  uint32_t curves[3] = {};
  uint32_t time = 0;     // the time flown's address, once the node has set it this frame
  float share = -1.0f;   // the share of the flight the node worked out this frame
};
thread_local Flight* flight = nullptr;  // while the flight node runs with the cheat on
}  // namespace

REX_EXTERN(SSX_FN_WING_FLIGHT_IMP);
REX_HOOK_RAW(SSX_FN_WING_FLIGHT) {
  const uint32_t ports = REX_LOAD_U32(ctx.r4.u32 + 48);
  if (!ports || !REXCVAR_GET(ssx_cheat_infinite_lift)) {
    SSX_FN_WING_FLIGHT_IMP(ctx, base);
    return;
  }
  Flight f;
  f.time_port = REX_LOAD_U32(ports + 216);
  for (uint32_t i = 0; i < 3; ++i) f.curves[i] = REX_LOAD_U32(ports + 192 + i * 4);
  flight = &f;
  SSX_FN_WING_FLIGHT_IMP(ctx, base);
  flight = nullptr;
  if (f.time && f.share > kLiftHold) {
    StoreFloat(base, f.time, FloatBits(LoadFloat(base, f.time) * (kLiftHold / f.share)));
  }
}

REX_EXTERN(SSX_FN_GRAPH_SET_FLOAT_IMP);
REX_HOOK_RAW(SSX_FN_GRAPH_SET_FLOAT) {
  const uint32_t port = ctx.r3.u32;
  SSX_FN_GRAPH_SET_FLOAT_IMP(ctx, base);
  if (flight && port && port == flight->time_port) flight->time = ctx.r3.u32;
}

REX_EXTERN(SSX_FN_CURVE_EVAL_IMP);
REX_HOOK_RAW(SSX_FN_CURVE_EVAL) {
  if (flight && ctx.r5.u32) {
    const uint32_t curve = ctx.r4.u32;
    if (curve && (curve == flight->curves[0] || curve == flight->curves[1] || curve == flight->curves[2])) {
      const float share = LoadFloat(base, ctx.r5.u32);
      if (share > flight->share) flight->share = share;
      for (uint32_t i = 0; i < 4; ++i) REX_STORE_U32(ctx.r5.u32 + i * 4, 0);  // the point: the flight's start
    }
  }
  SSX_FN_CURVE_EVAL_IMP(ctx, base);
}

// Ride uphill: a rider who leaves the drop's course (riding back up past where it starts, or off its sides) is
// pushed back by the game's out-of-bounds correction, which looks the rider's place up in a map of the drop.
// With the cheat on, that map is skipped, as when the game's own switch for it is on (stat 23,
// BLN_ForceSpatialDataOutOfBoundsCorrectionOff; [rider+0x20380] points at it), turned on only for the
// correction's own call. Corrections the game forces for its own reasons still happen.
REX_EXTERN(SSX_FN_OOB_CORRECTION_IMP);
REX_HOOK_RAW(SSX_FN_OOB_CORRECTION) {
  ScopedSwitch off;
  if (REXCVAR_GET(ssx_cheat_ride_uphill)) off.On(base, REX_LOAD_U32(ctx.r3.u32 + 0x20380));
  SSX_FN_OOB_CORRECTION_IMP(ctx, base);
}
