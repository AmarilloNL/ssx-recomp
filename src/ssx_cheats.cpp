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

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_boost, false, "SSX", "Cheat: boost never runs out");
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
