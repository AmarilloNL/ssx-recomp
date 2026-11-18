// ssx - native replacements for guest functions the recompiler can't handle, and small gameplay options.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"
#include "ssx_runtime_export.h"

REXCVAR_DEFINE_BOOL(ssx_freeride_hud, false, "SSX", "Show the boost and tricky meters in freeride");

// Reads VSCR via mfvscr (unimplemented in codegen) and returns its NJ bit.
// Xenon boots with NJ set and the title never clears it.
static uint32_t VscrNonJavaMode() { return 1; }
REX_HOOK(SSX_FN_VSCR_NJ, VscrNonJavaMode)

// Freeride shows no scoring panel, so no boost or tricky meter (issue #10). The game's setter is
// watched for the main panels. A ride whose HUD setup leaves them all off is a ride without a goal,
// freeride (the game also sets its "HUD off" flag then, and keeps setting the panels off during the
// ride); with the option on, the scoring panel stays on for that ride, through the same setter so the
// HUD screens hear about it. The next ride's setup decides again.
namespace {
const uint32_t kMainPanels[] = {SSX_HUD_SCORE_ACTIVE, SSX_HUD_TIME_ACTIVE, SSX_HUD_SPEED_ACTIVE,
                                SSX_HUD_DESCENT_ACTIVE};
bool main_panel_on[4];  // as the game last set them
bool freeride = false;  // the current ride gets the scoring panel
}  // namespace

REX_EXTERN(SSX_FN_HUD_SET_BOOL_IMP);
REX_HOOK_RAW(SSX_FN_HUD_SET_BOOL) {
  for (int i = 0; i < 4; ++i) {
    if (ctx.r4.u32 == kMainPanels[i]) main_panel_on[i] = REX_LOAD_U8(ctx.r5.u32) != 0;
  }
  // (r5 points at the caller's own bool, a value it just set up for this call.)
  if (freeride && ctx.r4.u32 == SSX_HUD_SCORE_ACTIVE) REX_STORE_U8(ctx.r5.u32, 1);
  SSX_FN_HUD_SET_BOOL_IMP(ctx, base);
}

REX_EXTERN(SSX_FN_HUD_MODE_SETUP_IMP);
REX_HOOK_RAW(SSX_FN_HUD_MODE_SETUP) {
  freeride = false;
  SSX_FN_HUD_MODE_SETUP_IMP(ctx, base);
  uint32_t provider = REX_LOAD_U32(SSX_HUD_PROVIDER_PTR);
  if (!REXCVAR_GET(ssx_freeride_hud) || !provider) return;
  for (bool on : main_panel_on) {
    if (on) return;
  }
  freeride = true;
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

// No network (issue #19). The game's online code polls the console's network address every frame;
// with an address but no Xbox Live it waits about 10 s for Live (the "Loading" screen after the
// save loads) before it says "You must be signed in to Xbox LIVE and the EA Servers". The port has
// neither, so it reports no network, like a console without a cable: the game gives up at once.
// The address itself is filled in as before.
extern "C" REX_FUNC(__imp__NetDll_XNetGetTitleXnAddr) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__NetDll_XNetGetTitleXnAddr");
  original(ctx, base);
  ctx.r3.u64 = 0x00000001;  // XNET_GET_XNADDR_NONE
}
