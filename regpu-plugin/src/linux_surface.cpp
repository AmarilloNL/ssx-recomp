// Linux: the game window's Wayland or X11 surface (what ReXGlue's own Vulkan presenter would use)
// becomes the Vulkan surface ReGPU presents to. The window comes from SDL (ReXGlue's runtime library
// exports it; Window::CreateSurface is private). A file of its own: the platform Vulkan headers
// can't follow vkd3d's, which include vulkan.h without them.
// (ReGPU's and ReXGlue's headers first: Xlib's macros, None among them, would break them.)
#include <rex/logging.h>
#include <rex/ui/window.h>
#include <regpu/regpu.h>

#include "linux_surface.h"

#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>

namespace
{
// The game's window: the only SDL window ReXGlue opens.
SDL_Window* GameWindow()
{
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* window = count > 0 ? windows[0] : nullptr;
    SDL_free(windows);
    return window;
}
} // namespace

bool glue2gpu::UseWindowAsVulkanSurface(rex::ui::Window* window, regpu::Config& config)
{
    SDL_Window* sdl = GameWindow();
    const SDL_PropertiesID props = sdl ? SDL_GetWindowProperties(sdl) : 0;
    wl_display* wlDisplay = static_cast<wl_display*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr));
    wl_surface* wlSurface = static_cast<wl_surface*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
    Display* xDisplay = static_cast<Display*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
    const ::Window xWindow = ::Window(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    const bool wayland = wlDisplay && wlSurface;
    if (!wayland && !(xDisplay && xWindow))
    {
        REXLOG_ERROR("ReGPU: the game window has no Wayland or X11 surface");
        return false;
    }
    config.vulkanInstanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME,
                                        wayland ? VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME : VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
    config.createVulkanSurface = [=](void* vkInstance) -> uint64_t {
        VkInstance instance = static_cast<VkInstance>(vkInstance);
        VkSurfaceKHR out = VK_NULL_HANDLE;
        VkResult r;
        if (wayland)
        {
            VkWaylandSurfaceCreateInfoKHR ci{ VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
            ci.display = wlDisplay;
            ci.surface = wlSurface;
            r = vkCreateWaylandSurfaceKHR(instance, &ci, nullptr, &out);
        }
        else
        {
            VkXlibSurfaceCreateInfoKHR ci{ VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
            ci.dpy = xDisplay;
            ci.window = xWindow;
            r = vkCreateXlibSurfaceKHR(instance, &ci, nullptr, &out);
        }
        if (r != VK_SUCCESS)
            REXLOG_ERROR("ReGPU: creating the Vulkan surface failed ({})", int(r));
        return uint64_t(out);
    };
    config.windowPixelSize = [window](uint32_t& width, uint32_t& height) {
        if (window->GetActualPhysicalWidth() && window->GetActualPhysicalHeight())
        {
            width = window->GetActualPhysicalWidth();
            height = window->GetActualPhysicalHeight();
        }
    };
    REXLOG_INFO("ReGPU: presenting through Vulkan on the game window ({})", wayland ? "Wayland" : "X11");
    return true;
}
