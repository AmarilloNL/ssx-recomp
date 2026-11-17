#pragma once

#include <string>
#include <vector>

// Snowflakes (the game's offline geotags) collected per drop, for the F1 tracker (issue #9).
struct SsxSnowflakeDrop {
  std::string range;  // e.g. "Rockies"
  std::string drop;   // e.g. "Drop 05"
  int collected = 0;
  int total = 0;
};

// In range order, then an "Other" row (total 0) for collected ones on drops without a list. Empty
// until the game has loaded its snowflake lists (at startup); the collected counts come from the
// loaded profile (0 before it loads).
std::vector<SsxSnowflakeDrop> SsxSnowflakeCounts();
