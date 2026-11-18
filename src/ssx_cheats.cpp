// ssx - cheats (issue #21), in F1 > Cheats.
//
// The rider's boost and Tricky state is updated once a frame by one function, whose object holds
// pointers to the values (the same values the HUD shows):
//   +76  the tuning record: +540 the boost maximum, +556 the Tricky duration, +560 Super Tricky's
//   +128 the Tricky timer (Tricky is on while it is above 0), +136 the Super Tricky timer
//   +144 the Super Tricky meter (Super Tricky once it reaches 1 during Tricky)
//   +148 the boost, +152 the boost from 0 to 1 (the HUD's meter)
// The cheats set these after the game's own update, so the game reads them the next frame. Nothing is
// written to the save: turning a cheat off hands the values back to the game.

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

REXCVAR_DEFINE_BOOL(ssx_cheat_infinite_boost, false, "SSX", "Cheat: boost never runs out");
REXCVAR_DEFINE_BOOL(ssx_cheat_super_tricky, false, "SSX", "Cheat: always in Super Tricky, with unlimited boost");

REX_EXTERN(SSX_FN_BOOST_UPDATE_IMP);
REX_HOOK_RAW(SSX_FN_BOOST_UPDATE) {
  const uint32_t self = ctx.r3.u32;
  SSX_FN_BOOST_UPDATE_IMP(ctx, base);
  const bool boost = REXCVAR_GET(ssx_cheat_infinite_boost), super = REXCVAR_GET(ssx_cheat_super_tricky);
  if (!boost && !super) return;
  const uint32_t tuning = REX_LOAD_U32(self + 76), raw = REX_LOAD_U32(self + 148), meter = REX_LOAD_U32(self + 152);
  if (!tuning || !raw) return;
  REX_STORE_U32(raw, REX_LOAD_U32(tuning + 540));  // full boost
  if (meter) REX_STORE_U32(meter, 0x3F800000u);    // 1.0f: the HUD's meter full too
  if (!super) return;
  // Tricky and Super Tricky stay on: both timers full, the Super Tricky meter at its threshold.
  if (uint32_t timer = REX_LOAD_U32(self + 128)) REX_STORE_U32(timer, REX_LOAD_U32(tuning + 556));
  if (uint32_t timer = REX_LOAD_U32(self + 136)) REX_STORE_U32(timer, REX_LOAD_U32(tuning + 560));
  if (uint32_t super_meter = REX_LOAD_U32(self + 144)) REX_STORE_U32(super_meter, 0x3F800000u);
}
