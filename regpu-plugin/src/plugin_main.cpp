// rexgpu-regpu.dll: loaded by ReXGlue's LoadGpuPlugin("regpu") (--gpu_plugin=regpu).
#include <string_view>

#include <rex/system/gpu_plugin.h>

#include "overlay.h"
#include "regpu_graphics_system.h"

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version(void)
{
    return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* rex_gpu_create(
    uint32_t abi_version, const rex::system::GpuCreateInfo* info)
{
    if (abi_version != rex::system::kGpuPluginAbiVersion || !info ||
        info->struct_size < sizeof(rex::system::GpuCreateInfo))
        return nullptr;
    // ReGPU is Direct3D 12 only.
    std::string_view backend = info->backend ? info->backend : "any";
    if (backend != "any" && backend != "d3d12")
        return nullptr;
    return new glue2gpu::ReGpuGraphicsSystem();
}

// SSX's app draws ReXGlue's ImGui overlays (its F1 menu) through this drawer; callback runs on the
// presenting thread once per frame, to build the next overlay frame.
extern "C" __declspec(dllexport) rex::ui::ImmediateDrawer* ssx_regpu_overlay(void (*callback)(void*), void* context)
{
    glue2gpu::SetOverlayFrameCallback(callback, context);
    return glue2gpu::OverlayImmediateDrawer();
}
