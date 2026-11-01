// ssx - native replacements for guest functions the recompiler can't handle.

#include <rex/hook.h>

// Reads VSCR via mfvscr (unimplemented in codegen) and returns its NJ bit.
// Xenon boots with NJ set and the title never clears it.
static uint32_t VscrNonJavaMode() { return 1; }
REX_HOOK(sub_83046110, VscrNonJavaMode)
