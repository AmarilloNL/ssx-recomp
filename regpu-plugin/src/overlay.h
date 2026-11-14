#pragma once
// ReXGlue's ImGui overlays (SSX's F1 menu and FPS counter) on ReGPU.
//
// ReXGlue's ImGuiDrawer builds its frames on the UI thread and draws them through a
// rex::ui::ImmediateDrawer. OverlayDrawer is one that records each frame's triangles on the CPU;
// ReGPU's presenting thread then draws the latest frame over the game (Host::drawOverlay,
// patches/regpu-0001-host-overlay-hook.patch).
#include <cstddef>
#include <cstdint>

namespace rex::ui
{
class ImmediateDrawer;
}

namespace glue2gpu
{
// The drawer, created on first use.
rex::ui::ImmediateDrawer* OverlayImmediateDrawer();
// regpu::Host::drawOverlay.
void DrawOverlay(void* commandList, void* backBuffer, size_t rtv, uint32_t width, uint32_t height);
// Called once per presented frame (Host::onSwap): the host's callback builds the next overlay frame.
void OverlayFramePresented();
void SetOverlayFrameCallback(void (*callback)(void*), void* context);
} // namespace glue2gpu
