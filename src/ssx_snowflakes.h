#pragma once

#include <string>
#include <vector>

#include <rex/ppc/context.h>

// Snowflakes (the game's offline geotags) collected per drop, for the F1 tracker (issue #9). A drop's
// snowflakes are those of its track(s); a track can be shared by several drops.
struct SsxSnowflakeDrop {
  std::string name;   // in the game's language ("Drop 05" until found)
  int collected = 0;
  int total = 0;
};
struct SsxSnowflakePeak {
  std::string name;  // in the game's language; empty until found
  std::vector<SsxSnowflakeDrop> drops;  // by name
};
struct SsxSnowflakeRange {
  std::string name;  // e.g. "Rockies"
  int collected = 0, total = 0;  // of its tracks, each counted once
  std::vector<SsxSnowflakePeak> peaks;  // by name
};
struct SsxSnowflakes {
  int collected = 0, total = 0;  // of all tracks listed (the game's own "N of 599" leaves out DLC)
  std::vector<SsxSnowflakeRange> ranges;  // in the game's order
};

// Empty until the game has loaded its snowflake lists (at startup); the collected counts come from
// the loaded profile (0 before it loads).
SsxSnowflakes SsxSnowflakeCounts();

// Looks the drops' names up again (the tab calls it when shown: DLC records load late).
void SsxSnowflakesRefreshNames();

// Looks up the drops' names (heap scan, localize); call on the game thread (the main loop's tick).
void SsxSnowflakesTick(PPCContext& ctx, uint8_t* base);
