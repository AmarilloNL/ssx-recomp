// ssx - native replacements for guest functions the recompiler can't handle.

#include <rex/hook.h>

#include "ssx_guest.h"

// Reads VSCR via mfvscr (unimplemented in codegen) and returns its NJ bit.
// Xenon boots with NJ set and the title never clears it.
static uint32_t VscrNonJavaMode() { return 1; }
REX_HOOK(SSX_FN_VSCR_NJ, VscrNonJavaMode)
