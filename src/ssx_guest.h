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

// The HUD (issue #10). The HUD's data provider holds named fields the HUD screens read; the game sets
// its booleans with SetBool(provider, field id, const bool* value). At the start of a ride the HUD setup
// turns the panels on for the event: none of the main ones (score, time, speed, descent) in freeride.
#define SSX_FN_HUD_MODE_SETUP SSX_GUEST(sub_8238BB00, sub_82397658)
#define SSX_FN_HUD_MODE_SETUP_IMP SSX_GUEST(__imp__sub_8238BB00, __imp__sub_82397658)
#define SSX_FN_HUD_SET_BOOL SSX_GUEST(sub_82382588, sub_82391818)
#define SSX_FN_HUD_SET_BOOL_IMP SSX_GUEST(__imp__sub_82382588, __imp__sub_82391818)
#define SSX_HUD_PROVIDER_PTR SSX_GUEST(0x838AB660u, 0x8388F3F0u)
// Field ids: HUD_ScoreActive (the scoring panel: score, tricky and boost meters), HUD_TimeActive,
// HUD_SpeedActive, HUD_DescentActive.
#define SSX_HUD_SCORE_ACTIVE SSX_GUEST(305u, 309u)
#define SSX_HUD_TIME_ACTIVE SSX_GUEST(316u, 320u)
#define SSX_HUD_SPEED_ACTIVE SSX_GUEST(311u, 315u)
#define SSX_HUD_DESCENT_ACTIVE SSX_GUEST(234u, 238u)
// Counts the snowflakes (offline geotags) in one drop's list at startup (issue #9, ssx_snowflakes.cpp).
#define SSX_FN_GEOTAG_COUNT SSX_GUEST(sub_825057F8, sub_824CFF78)
#define SSX_FN_GEOTAG_COUNT_IMP SSX_GUEST(__imp__sub_825057F8, __imp__sub_824CFF78)

// The game's localize(manager, eastl string* out, const char* key, 1), and its manager (issue #9).
#define SSX_FN_LOCALIZE SSX_GUEST(sub_8256A878, sub_82535200)
#define SSX_LOC_MANAGER_PTR SSX_GUEST(0x838CCB4Cu, 0x838AFFFCu)
// Whether the profile has a flag (r3 profile stats, r4 key, r5 kind: 2 for a badge, key
// {fnv1_32(badge name), 1}); returns a bool. Issue #11, ssx_badges.cpp.
#define SSX_FN_PROFILE_HAS SSX_GUEST(sub_82586810, sub_825518B8)
#define SSX_FN_PROFILE_HAS_IMP SSX_GUEST(__imp__sub_82586810, __imp__sub_825518B8)

// Vtable of the main loop's periodic task.
#define SSX_VTABLE_MAIN_LOOP_TASK SSX_GUEST(0x82007738u, 0x82007840u)

// The rider's boost and Tricky update, once a frame (issue #21, ssx_cheats.cpp; this = r3, same
// field offsets in both versions).
#define SSX_FN_BOOST_UPDATE SSX_GUEST(sub_82CED2E8, sub_82CC8850)
#define SSX_FN_BOOST_UPDATE_IMP SSX_GUEST(__imp__sub_82CED2E8, __imp__sub_82CC8850)

// The profile's credits (key 'BILL', ssx_cheats.cpp): every change (amount r4, a purchase's is negative)
// goes through the first, which adds it to the profile (sub_824A9DF8 / sub_82475E38) and to the menus'
// credits; the second is the getter the menus and stores use (profile r3, credits in r3).
#define SSX_FN_CHANGE_CREDITS SSX_GUEST(sub_82402000, sub_823DB560)
#define SSX_FN_CHANGE_CREDITS_IMP SSX_GUEST(__imp__sub_82402000, __imp__sub_823DB560)
// A rewind is used up: the rider's rewind component (r3) takes one off its count of rewinds left
// ([r3+240], published as the rider stat the HUD's rewind icons show); it returns at once when the drop's
// rewinds are infinite ([r3+148] points at that flag, as in a race).
#define SSX_FN_USE_REWIND SSX_GUEST(sub_82CF30E0, sub_82CCE650)
#define SSX_FN_USE_REWIND_IMP SSX_GUEST(__imp__sub_82CF30E0, __imp__sub_82CCE650)
// ...and its check "can the rider rewind now?" (r3 the same component; answers r3 1 or 0): yes while rewinds
// are infinite or left, or a rewind is still going on. Called by the rewind update when LB has started one.
#define SSX_FN_CAN_REWIND SSX_GUEST(sub_82CF3028, sub_82CCE598)
#define SSX_FN_CAN_REWIND_IMP SSX_GUEST(__imp__sub_82CF3028, __imp__sub_82CCE598)
// The in-ride HUD's update, once a frame (this r3): [this+144] is the rider's stats object, stat N's
// value at *[stats + (N+1)*4] (N in the game's own sorted stat list, e.g. 148 FLT_Armour).
#define SSX_FN_HUD_UPDATE SSX_GUEST(sub_824C8DB0, sub_82492D28)
#define SSX_FN_HUD_UPDATE_IMP SSX_GUEST(__imp__sub_824C8DB0, __imp__sub_82492D28)
// The rider's cold, once a frame (Antarctica; this r3): pointers to the core temperature ([r3+0x20190],
// the FLT_InternalTemperature stat), its maximum ([r3+0x20194], 185) and the cold taken so far
// ([r3+0x2019C]); the maximum is 1 minus that cold, the temperature can't go above the maximum.
#define SSX_FN_COLD_UPDATE SSX_GUEST(sub_82CF83B0, sub_82CD3970)
#define SSX_FN_COLD_UPDATE_IMP SSX_GUEST(__imp__sub_82CF83B0, __imp__sub_82CD3970)
#define SSX_FN_GET_CREDITS SSX_GUEST(sub_824A7910, sub_82473950)
#define SSX_FN_GET_CREDITS_IMP SSX_GUEST(__imp__sub_824A7910, __imp__sub_82473950)
// The add the change above makes: profile r3, amount r4, r5 1 to count it as earned too (key 'EARN').
#define SSX_FN_ADD_CREDITS SSX_GUEST(sub_824A9DF8, sub_82475E38)
// ...and how it then hands the menus their credits: SetInt(*SSX_MENU_DATA, SSX_MENU_ID_CREDITS, &value).
#define SSX_FN_MENU_SET_CREDITS SSX_GUEST(sub_8239FE40, sub_8256A820)
#define SSX_MENU_DATA SSX_GUEST(0x838AB660u, 0x8388F3F0u)
#define SSX_MENU_ID_CREDITS SSX_GUEST(552u, 571u)
// The rider's trick scoring (this r3; same fields in both versions): [r3+264] points at the combo's trick
// count, [r3+280] at its points, [r3+596] at the multiplier level. Each frame the scoring update first
// checks for a wipeout (the first, r4 a flag it sets): when [r3+300] and [r3+308] are set it loses the
// combo (its points are cleared without the combo bonus) and ends Tricky. Later the update adds a frame's
// points (the second: f1 points, r5 1 when the combo's time is up: then it also adds the combo bonus,
// points x (multiplier level + 1), and clears the combo). Both report the combo's end with the third
// (r5 1 banked, 0 lost), which tells the HUD.
#define SSX_FN_SCORE_WIPEOUT SSX_GUEST(sub_82CF12A0, sub_82CCC808)
#define SSX_FN_SCORE_WIPEOUT_IMP SSX_GUEST(__imp__sub_82CF12A0, __imp__sub_82CCC808)
#define SSX_FN_SCORE_ADD SSX_GUEST(sub_82CF0968, sub_82CCBED0)
#define SSX_FN_COMBO_ENDED SSX_GUEST(sub_82CEFEB0, sub_82CCB418)
#define SSX_FN_COMBO_ENDED_IMP SSX_GUEST(__imp__sub_82CEFEB0, __imp__sub_82CCB418)
// The multiplier level ([r3+596]) after a trick or a frame: up one for a big enough trick while flowing, down
// one when the flow stops (at once, or as its timer runs out), between -1 and 19.
#define SSX_FN_FLOW_UPDATE SSX_GUEST(sub_82CED030, sub_82CC8598)
#define SSX_FN_FLOW_UPDATE_IMP SSX_GUEST(__imp__sub_82CED030, __imp__sub_82CC8598)
// A gear stat of the rider's outfit (loadout r3, stat r4 -> r3): the sum of that stat's points over the 15 items
// worn (each lists up to 6 stat, points pairs). Stats 0-25 are the gear's (board speed to pulse goggles),
// 26-36 its perks (30 glowing, 34 big head, 35 big hands, 36 both). At the start of a ride each is divided
// by its maximum (100 in both versions), at most 1, and the rider's tuning is set between a worst and a
// best one by these.
#define SSX_FN_GEAR_STAT SSX_GUEST(sub_82C64778, sub_82C3FB78)
#define SSX_FN_GEAR_STAT_IMP SSX_GUEST(__imp__sub_82C64778, __imp__sub_82C3FB78)
// A wingsuit flight's node of the rider's behaviour graph, once a frame while flying (node r4, [r4+48] its
// ports): it adds the frame to the time flown (port +216, a float the graph keeps, 60 a second) and publishes
// the share of the flight used (port +44; stat 232 FLT_WingsuitUsePercentage, the HUD's wingsuit meter); the
// flight ends near 1. Three curves of the node (ports +192, +196, +200) are read at a point that moves with
// the time flown. The graph's floats are set through the second (port r3, value f1; returns the value's
// address in r3), the curves read through the third (out r3, curve r4, in r5: a vector of the point).
#define SSX_FN_WING_FLIGHT SSX_GUEST(sub_82CB9770, sub_82C94B78)
#define SSX_FN_WING_FLIGHT_IMP SSX_GUEST(__imp__sub_82CB9770, __imp__sub_82C94B78)
#define SSX_FN_GRAPH_SET_FLOAT SSX_GUEST(sub_831A9D48, sub_83186728)
#define SSX_FN_GRAPH_SET_FLOAT_IMP SSX_GUEST(__imp__sub_831A9D48, __imp__sub_83186728)
#define SSX_FN_CURVE_EVAL SSX_GUEST(sub_82C7B420, sub_82C567F0)
#define SSX_FN_CURVE_EVAL_IMP SSX_GUEST(__imp__sub_82C7B420, __imp__sub_82C567F0)
// The rider's out-of-bounds correction, once a frame (rider r3): unless [r3+0x20380] points at a set byte (stat 23,
// the game's switch for it), it looks the rider's position up in the drop's map of where the course is and pushes
// the rider back from the cells outside it.
#define SSX_FN_OOB_CORRECTION SSX_GUEST(sub_82CF7A70, sub_82CD3080)
#define SSX_FN_OOB_CORRECTION_IMP SSX_GUEST(__imp__sub_82CF7A70, __imp__sub_82CD3080)
