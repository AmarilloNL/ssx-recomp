// ssx - native replacements for guest functions the recompiler can't handle, and small gameplay options.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"

REXCVAR_DEFINE_BOOL(ssx_freeride_hud, false, "SSX", "Show the boost and tricky meters in freeride");

// Reads VSCR via mfvscr (unimplemented in codegen) and returns its NJ bit.
// Xenon boots with NJ set and the title never clears it.
static uint32_t VscrNonJavaMode() { return 1; }
REX_HOOK(SSX_FN_VSCR_NJ, VscrNonJavaMode)

// Freeride shows no scoring panel, so no boost or tricky meter (issue #10). With the option on, the
// panel is turned on after the game's HUD setup for a freeride, through the game's own setter so the
// HUD screens hear about it.
REX_EXTERN(SSX_FN_HUD_MODE_SETUP_IMP);
REX_EXTERN(SSX_FN_HUD_SET_BOOL);
REX_HOOK_RAW(SSX_FN_HUD_MODE_SETUP) {
  SSX_FN_HUD_MODE_SETUP_IMP(ctx, base);
  if (!REXCVAR_GET(ssx_freeride_hud)) return;
  uint32_t state = REX_LOAD_U32(SSX_GAME_STATE_PTR);
  uint32_t provider = REX_LOAD_U32(SSX_HUD_PROVIDER_PTR);
  if (!state || !provider || REX_LOAD_U32(state + SSX_GAME_STATE_MODE) != REX_LOAD_U32(SSX_MODE_FREERIDE)) return;
#ifdef SSX_TITLE_UPDATE
  if (REX_LOAD_U8(state + 0x10774)) return;  // the update's setup leaves the HUD off when this is set
#endif
  // The value goes in a small frame of our own below the caller's stack pointer.
  const uint32_t sp = ctx.r1.u32;
  ctx.r1.u32 = sp - 32;
  REX_STORE_U8(ctx.r1.u32 + 16, 1);
  ctx.r3.u64 = provider;
  ctx.r4.u64 = SSX_HUD_SCORE_ACTIVE;
  ctx.r5.u64 = ctx.r1.u32 + 16;
  SSX_FN_HUD_SET_BOOL(ctx, base);
  ctx.r1.u32 = sp;
  REXLOG_INFO("Freeride: boost and tricky meters on");
}
