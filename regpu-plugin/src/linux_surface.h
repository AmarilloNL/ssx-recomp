#pragma once
// Linux: ReXGlue's game window as a Vulkan surface for ReGPU (Config::createVulkanSurface).

namespace rex::ui { class Window; }
namespace regpu { struct Config; }

namespace glue2gpu
{
// Fills config's Vulkan surface fields from the window (Wayland or X11). False without a surface.
bool UseWindowAsVulkanSurface(rex::ui::Window* window, regpu::Config& config);
} // namespace glue2gpu
