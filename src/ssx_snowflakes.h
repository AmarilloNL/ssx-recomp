#pragma once

#include <string>
#include <vector>

#include <rex/ppc/context.h>

// Snowflakes (the game's offline geotags) collected per drop, for the F1 tracker (issue #9).
struct SsxSnowflakeDrop {
  std::string range;  // e.g. "Rockies"
  std::string peak;   // in the game's language; empty until found
  std::string drop;   // the drop's name in the game's language ("Drop 05" until found)
  bool listed = true;  // false: a drop the game's own count leaves out (its total isn't known)
  int collected = 0;
  int total = 0;
};

// By range (the game's order), peak and drop. Empty until the game has loaded its snowflake lists (at
// startup); the collected counts come from the loaded profile (0 before it loads).
std::vector<SsxSnowflakeDrop> SsxSnowflakeCounts();

// Looks up the drops' names (heap scan, localize); call on the game thread (the main loop's tick).
void SsxSnowflakesTick(PPCContext& ctx, uint8_t* base);
