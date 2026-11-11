#pragma once

// Guest functions and data the port hooks or reads, for the game version being built: the disc
// release (v0.0.0.2) or the last title update (v0.0.3.2, built when game-dump/files/default.xexp is
// present; see CMakeLists.txt). The update moves most code; each pair was matched by comparing
// the functions' instructions.
#ifdef SSX_TITLE_UPDATE
#define SSX_GUEST(disc, update) update
#else
#define SSX_GUEST(disc, update) disc
#endif

// Reads VSCR (mfvscr) and returns its NJ bit.
#define SSX_FN_VSCR_NJ SSX_GUEST(sub_83046110, sub_83020DC8)
// Ticks a periodic task (the main loop) once its period has elapsed.
#define SSX_FN_TASK_TICK SSX_GUEST(sub_83047248, sub_83021F00)
#define SSX_FN_TASK_TICK_IMP SSX_GUEST(__imp__sub_83047248, __imp__sub_83021F00)
// The scheduler's clock, (milliseconds + 1) * 1e6.
#define SSX_FN_SCHEDULER_CLOCK SSX_GUEST(sub_83047368, sub_83022020)
// The per-frame throttle.
#define SSX_FN_FRAME_THROTTLE SSX_GUEST(sub_8233D770, sub_8234D770)
#define SSX_FN_FRAME_THROTTLE_IMP SSX_GUEST(__imp__sub_8233D770, __imp__sub_8234D770)
// D3D: blocks on a GPU fence.
#define SSX_FN_FENCE_BLOCK SSX_GUEST(sub_8232C258, sub_8233C258)
#define SSX_FN_FENCE_BLOCK_IMP SSX_GUEST(__imp__sub_8232C258, __imp__sub_8233C258)
// Picks the render size for a quality mode.
#define SSX_FN_RENDER_SIZE SSX_GUEST(sub_82FB6120, sub_82F91568)
#define SSX_FN_RENDER_SIZE_IMP SSX_GUEST(__imp__sub_82FB6120, __imp__sub_82F91568)

// The tuning update check's step that picks the server's version or the save's (title update only).
#define SSX_FN_TUNING_PICK sub_824F6FA0
#define SSX_FN_TUNING_PICK_IMP __imp__sub_824F6FA0
// Loads a database file pair (.vlt + .bin) and builds its collections (title update only).
#define SSX_FN_LOAD_DATABASE sub_8312AB50
#define SSX_FN_LOAD_DATABASE_IMP __imp__sub_8312AB50

// Vtable of the main loop's periodic task.
#define SSX_VTABLE_MAIN_LOOP_TASK SSX_GUEST(0x82007738u, 0x82007840u)
