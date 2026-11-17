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

// Freeride shows no scoring panel, so no boost or tricky meter (issue #10). The game's setter is
// watched for the main panels; when a ride's HUD setup leaves them all off (a ride without a goal:
// freeride), the option turns the scoring panel on, through the same setter so the HUD screens hear
// about it.
namespace {
const uint32_t kMainPanels[] = {SSX_HUD_SCORE_ACTIVE, SSX_HUD_TIME_ACTIVE, SSX_HUD_SPEED_ACTIVE,
                                SSX_HUD_DESCENT_ACTIVE};
bool main_panel_on[4];  // as the game last set them
}  // namespace

REX_EXTERN(SSX_FN_HUD_SET_BOOL_IMP);
REX_HOOK_RAW(SSX_FN_HUD_SET_BOOL) {
  for (int i = 0; i < 4; ++i) {
    if (ctx.r4.u32 == kMainPanels[i]) main_panel_on[i] = REX_LOAD_U8(ctx.r5.u32) != 0;
  }
  SSX_FN_HUD_SET_BOOL_IMP(ctx, base);
}

REX_EXTERN(SSX_FN_HUD_MODE_SETUP_IMP);
REX_HOOK_RAW(SSX_FN_HUD_MODE_SETUP) {
  SSX_FN_HUD_MODE_SETUP_IMP(ctx, base);
  if (!REXCVAR_GET(ssx_freeride_hud)) return;
  uint32_t state = REX_LOAD_U32(SSX_GAME_STATE_PTR);
  uint32_t provider = REX_LOAD_U32(SSX_HUD_PROVIDER_PTR);
  if (!state || !provider) return;
#ifdef SSX_TITLE_UPDATE
  if (REX_LOAD_U8(state + 0x10774)) return;  // the update's setup leaves the HUD off when this is set
#endif
  for (bool on : main_panel_on) {
    if (on) return;
  }
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
