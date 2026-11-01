// ssx - frame statistics from the guest's VdSwap (one call per presented frame).
//
// Logs the presented frame rate every 5 s and whenever the front buffer size
// changes, and exposes the current rate for the settings overlay.

#include "ssx_frame_stats.h"

#include <atomic>
#include <chrono>

#include <rex/logging.h>

#include "ssx_runtime_export.h"


namespace {
std::atomic<float> current_fps{0.0f};
}  // namespace

float SsxCurrentFps() { return current_fps.load(std::memory_order_relaxed); }

extern "C" REX_FUNC(__imp__VdSwap) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__VdSwap");
  using clock = std::chrono::steady_clock;
  static clock::time_point window_start = clock::now();
  static uint32_t frames = 0;
  static uint32_t last_size = 0;

  // r4 = guest pointer to the front buffer's texture fetch constant (6 big-endian
  // dwords); dword 2 holds width-1 in bits 0-12 and height-1 in bits 13-25.
  uint32_t size_word = __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + ctx.r4.u32 + 8));
  if (size_word != last_size) {
    last_size = size_word;
    REXLOG_INFO("VdSwap: front buffer {}x{}", (size_word & 0x1FFF) + 1,
                ((size_word >> 13) & 0x1FFF) + 1);
  }

  ++frames;
  auto elapsed = clock::now() - window_start;
  if (elapsed >= std::chrono::seconds(5)) {
    float fps = frames / std::chrono::duration<float>(elapsed).count();
    current_fps.store(fps, std::memory_order_relaxed);
    REXLOG_INFO("VdSwap: {:.1f} fps", fps);
    frames = 0;
    window_start = clock::now();
  }

  original(ctx, base);
}

