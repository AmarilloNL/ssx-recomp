// rexgpu-regpu for SSX: ReGPU (https://github.com/zer0ducksgiven/ReGPU, rexglue-driver branch) behind
// ReXGlue's GPU plugin interface. Adapted from Glue2GPU's Forza Horizon plugin
// (https://github.com/zer0ducksgiven/Glue2GPU, MIT, see ../LICENSE); the Forza-specific parts are left out.
#include "regpu_graphics_system.h"
#include "overlay.h"
#include "stack_dump.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/system/xvideo.h>
#include <rex/ui/window.h>

#include <regpu/driver.h>
#include <regpu/regpu.h>

// The settings of ReXGlue's own renderer that mean the same here (SSX's F1 menu shows them).
REXCVAR_DEFINE_INT32(resolution_scale, 1, "GPU",
                     "Resolution scale: ReGPU draws at 1280x720 times this (2: 2560x1440, 3: 3840x2160)")
    .range(1, 3)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(anisotropic_override, 3, "GPU",
                     "Texture filtering: -1 the game's, 0 off, 1-5 1x-16x anisotropic")
    .range(-1, 5);

namespace glue2gpu
{
namespace
{
constexpr uint32_t kRegisterBase = 0x7FC80000;

// ReGPU's Host takes plain function pointers, so its callbacks reach ReXGlue through these.
rex::memory::Memory* g_memory = nullptr;
rex::runtime::FunctionDispatcher* g_dispatcher = nullptr;
rex::system::KernelState* g_kernel = nullptr;

// ReGPU's threads (vblank interrupt, command processor) are guest threads: they need a
// PPCContext, and the interrupt one calls the title's callback on it.
std::mutex g_threadsMutex;
std::vector<rex::system::object_ref<rex::system::XHostThread>> g_threads;

std::atomic<bool> g_running{ false };
std::atomic<uint64_t> g_swapCount{ 0 };
std::atomic<uint64_t> g_swaps{ 0 };

int32_t FlagInt(const char* name, int32_t fallback)
{
    const std::string v = rex::cvar::GetFlagByName(name);
    return v.empty() ? fallback : std::atoi(v.c_str());
}

// SSX's frame rate setting (ssx_framerate: 0 = the main display's rate). The game paces itself
// (its main-loop task period, src/ssx_settings.cpp); ReGPU's cap only stops it from running past that.
uint32_t TargetFps()
{
    int32_t fps = FlagInt("ssx_framerate", 60);
    if (fps == 0)
    {
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        fps = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
                  ? int32_t(mode.dmDisplayFrequency)
                  : 60;
    }
    return uint32_t(std::clamp(fps, 30, 360));
}

// The F1 menu's frame rate and texture filtering, applied while the game runs.
void FollowSettings()
{
    static uint32_t lastFps = 0;
    static int32_t lastAniso = -2;
    if (const uint32_t fps = TargetFps(); fps != lastFps)
    {
        regpu::SetFrameLimit(fps, false);
        lastFps = fps;
    }
    if (const int32_t aniso = REXCVAR_GET(anisotropic_override); aniso != lastAniso)
    {
        regpu::SetAnisotropy(aniso <= 0 ? aniso : 1 << (aniso - 1));
        lastAniso = aniso;
    }
}

// The window title shows the frame rate. Set from a thread of its own, as WM_SETTEXT waits for the
// window's thread.
void StartTitleFps(HWND hwnd)
{
    if (!hwnd)
        return;
    std::thread([hwnd] {
        auto last = std::chrono::steady_clock::now();
        uint64_t lastSwaps = g_swaps.load();
        while (IsWindow(hwnd) && g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto now = std::chrono::steady_clock::now();
            const uint64_t swaps = g_swaps.load();
            const double fps = double(swaps - lastSwaps) / std::chrono::duration<double>(now - last).count();
            last = now;
            lastSwaps = swaps;
            wchar_t title[96];
            swprintf_s(title, L"SSX (ReGPU) - %.0f fps", fps);
            SendMessageTimeoutW(hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(title), SMTO_ABORTIFHUNG, 200, nullptr);
            FollowSettings();
        }
    }).detach();
}

uint32_t Alloc(bool physical, uint32_t size, uint32_t align)
{
    if (!physical)
        return g_memory->SystemHeapAlloc(size, align);
    // The 0xA0000000 view (64 KB pages), where ReGPU expects physical memory. As
    // MmAllocatePhysicalMemoryEx does: whole pages, over the heap's whole range.
    constexpr uint32_t kPage = 64 * 1024;
    auto* heap = g_memory->LookupHeapByType(true, kPage);
    const uint32_t base = heap->heap_base();
    uint32_t address = 0;
    if (!heap->AllocRange(base, base + heap->heap_size() - 1, (size + kPage - 1) & ~(kPage - 1),
                          std::max(align, kPage),
                          rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                          rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                          /*top_down=*/true, &address))
    {
        REXLOG_ERROR("regpu: physical alloc of {:#x} bytes (align {:#x}) failed", size, align);
        return 0;
    }
    return address;
}

void CreateThread(const char* name, void (*body)(PPCContext&))
{
    auto* thread = new rex::system::XHostThread(g_kernel, 128 * 1024, 0, [body]() {
        body(*rex::system::XThread::GetCurrentThread()->thread_state()->context());
        return 0;
    });
    thread->set_name(name);
    thread->Create();
    std::lock_guard lock(g_threadsMutex);
    g_threads.emplace_back(thread);
}

uint32_t CurrentPcr()
{
    return rex::system::XThread::GetCurrentThread()->pcr_ptr();
}

// ReGPU sets r3/r4 (source, user data) and calls this between interruptLockAcquire/Release.
// ExecuteInterrupt takes the dispatcher's global lock and sets the arguments itself, so the
// lock hooks are empty.
void GuestCall(PPCContext& ctx, uint32_t address)
{
    auto* thread = rex::system::XThread::GetCurrentThread();
    uint64_t args[] = { ctx.r3.u64, ctx.r4.u64 };
    g_dispatcher->ExecuteInterrupt(thread->thread_state(), address, args, 2);
}

void InterruptLockAcquire(int) {}
void InterruptLockRelease() {}

// Which resolves are also written to guest memory, for what SSX reads on the CPU (the same policy
// as its readback_resolve settings with ReXGlue's renderer, src/ssx_settings.cpp):
// - small ones (32x16 on Mount Craddock: sun or shade for the cold) every time, a frame late;
// - every image the first time it is resolved, at once (the flare lights of night Deadly Descents
//   are rendered and resolved at load, then compressed on the CPU).
// Large per-frame resolves are not written: the CPU never reads them, and some overrun into
// CPU data (the snow volume into the trail quads' index buffer).
// SSX_REGPU_WRITEBACK=none|all: none at all, or every resolve at once (A/B).
regpu::Writeback ResolveWriteback(const regpu::ResolveCopy& copy)
{
    static const std::string mode = std::getenv("SSX_REGPU_WRITEBACK") ? std::getenv("SSX_REGPU_WRITEBACK") : "";
    regpu::Writeback w;
    if (mode == "none")
        return w;
    if (mode == "all")
    {
        w.mode = regpu::Writeback::Mode::Now;
        return w;
    }
    if (copy.width * copy.height <= 4096)
    {
        w.mode = regpu::Writeback::Mode::Deferred;
        return w;
    }
    static std::mutex mutex;
    static std::unordered_map<uint64_t, bool> seen;
    const uint64_t key = uint64_t(copy.base) << 32 ^ uint64_t(copy.width) << 18 ^ uint64_t(copy.height) << 6 ^
                         copy.format ^ (copy.depth ? 1ull << 63 : 0);
    std::lock_guard lock(mutex);
    if (seen.try_emplace(key, true).second)
        w.mode = regpu::Writeback::Mode::Now;
    return w;
}

// ReXGlue's VdQueryVideoMode reports the refresh rate the title paces itself by, so ReGPU's
// vblank follows it, as ReXGlue's own vsync thread does.
void RefreshRateLoop()
{
    while (g_running)
    {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        regpu::driver::SetRefreshRate(std::max(1.0, double(float(mode.refresh_rate))));
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void SetDefaultSwitch(const char* name)
{
    if (!std::getenv(name))
    {
        _putenv_s(name, "1");
        regpu::driver::SetSwitch(name, true);
    }
}
} // namespace

ReGpuGraphicsSystem::~ReGpuGraphicsSystem() { Shutdown(); }

X_STATUS ReGpuGraphicsSystem::SetupPresentation(rex::ui::WindowedAppContext*)
{
    // ReGPU creates its device in regpu::Start(), which needs Host filled, so the real work
    // waits for SetupGuestGpu.
    presentation_ = true;
    return X_STATUS_SUCCESS;
}

void ReGpuGraphicsSystem::FillHost()
{
    regpu::Host& host = regpu::GetHost();
    host.base = g_memory->virtual_membase();
    host.physicalSection = nullptr; // ReGPU opens the 0xA0000000 view by address, or copies
    host.alloc = Alloc;
    host.createThread = CreateThread;
    host.currentPcr = CurrentPcr;
    host.guestCall = GuestCall;
    host.interruptLockAcquire = InterruptLockAcquire;
    host.interruptLockRelease = InterruptLockRelease;
    host.logThreadStacks = LogThreadStacks;
    host.resolveWriteback = ResolveWriteback;
    host.onSwap = [](uint64_t frame) {
        g_swapCount.store(frame, std::memory_order_relaxed);
        g_swaps.fetch_add(1, std::memory_order_relaxed);
        OverlayFramePresented();
    };
    host.drawOverlay = DrawOverlay;
}

X_STATUS ReGpuGraphicsSystem::SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                                            rex::system::KernelState* kernel_state)
{
    g_dispatcher = dispatcher;
    g_memory = dispatcher->memory();
    g_kernel = kernel_state;
    FillHost();

    regpu::Config& config = regpu::GetConfig();
    // The drawing size: the console's 1280x720 times resolution_scale, or SSX_REGPU_RES=WxH.
    const uint32_t scale = uint32_t(std::clamp(REXCVAR_GET(resolution_scale), 1, 3));
    config.width = 1280 * scale;
    config.height = 720 * scale;
    if (const char* res = std::getenv("SSX_REGPU_RES"))
        std::sscanf(res, "%ux%u", &config.width, &config.height);
    // SSX_REGPU_UPSCALER=fsr1 with SSX_REGPU_QUALITY=0-4 (native, quality, balanced, performance, ultra).
    if (const char* up = std::getenv("SSX_REGPU_UPSCALER"); up && std::string(up) == "fsr1")
        config.upscaler = regpu::Config::Upscaler::Fsr1;
    if (const char* q = std::getenv("SSX_REGPU_QUALITY"))
        config.quality = uint32_t(std::atoi(q));
    config.vsync = false;
    config.maxFps = TargetFps();
    config.windowTitle = L"SSX (ReGPU)";
    // Present into ReXGlue's game window (it keeps input, focus, closing and fullscreen).
    if (auto* runtime = rex::Runtime::instance(); runtime && runtime->display_window())
        config.hostWindow = runtime->display_window()->GetNativeWindowHandle();
    // ReXGlue calls InitializeShaderStorage after this, so the cache root comes from its setting.
    if (cacheRoot_.empty())
        if (const std::string root = rex::cvar::GetFlagByName("cache_root"); !root.empty())
            cacheRoot_ = root;
    if (!cacheRoot_.empty())
        config.userDir = cacheRoot_ / "regpu";

    // Same range, mask and size as ReXGlue's own GraphicsSystem.
    g_memory->AddVirtualMappedRange(
        kRegisterBase, 0xFFFF0000, 0x0000FFFF, this,
        reinterpret_cast<rex::runtime::MMIOReadCallback>(ReadRegisterThunk),
        reinterpret_cast<rex::runtime::MMIOWriteCallback>(WriteRegisterThunk));

    // ReGPU switches that are off for XenonRecomp ports and that Glue2GPU turns on for ReXGlue
    // (each can still be set to 0 in the environment):
    // - gamma-signed fetches and gamma-encoding resolves of k_8_8_8_8_GAMMA surfaces (console behaviour);
    // - draws whose shaders or pipelines are still compiling are skipped, except opaque ones once the
    //   game has settled (those wait: they may draw something only once);
    // - texture filtering as the fetch asks (not 16x everywhere); vertex fetches not held to the
    //   fetch constant's size (as the console);
    // - fences written when the command processor reaches them (SSX waits on a fence every frame);
    // - draws translated on a thread of their own; one sample per 2D fetch.
    SetDefaultSwitch("REGPU_TEXTURE_GAMMA");
    SetDefaultSwitch("REGPU_RESOLVE_GAMMA");
    SetDefaultSwitch("REGPU_ASYNC_SHADERS");
    SetDefaultSwitch("REGPU_ASYNC_PIPELINES");
    SetDefaultSwitch("REGPU_NO_SETTLED_SKIP");
    SetDefaultSwitch("REGPU_ANISO_FROM_FETCH");
    SetDefaultSwitch("REGPU_NO_FETCH_BOUNDS");
    SetDefaultSwitch("REGPU_EARLY_FENCES");
    SetDefaultSwitch("REGPU_DRAW_THREAD");
    SetDefaultSwitch("REGPU_LEAN_FETCH");
    // Textures SSX updates a little at a time on the CPU, which sampled change checks miss:
    // - its text comes from a 1 MB font atlas (1024x1024 k_8) filled glyph by glyph as new text
    //   appears (a region name on the globe was drawn from old glyphs, a dark smudge);
    // - the snow surface finds its trail tiles in the 128x128x256 trail volume through a 3.2 MB
    //   1268x1280 k_16 table (stale, the snow showed no tracks).
    if (!std::getenv("REGPU_DENSE_HASH_KB"))
        _putenv_s("REGPU_DENSE_HASH_KB", "4096");

    regpu::Start();
    started_ = true;
    g_running = true;
    StartTitleFps(static_cast<HWND>(config.hostWindow));
    std::thread(RefreshRateLoop).detach();
    REXLOG_INFO("regpu: started, drawing {}x{}, frame cap {}", config.width, config.height, config.maxFps);
    return X_STATUS_SUCCESS;
}

uint32_t ReGpuGraphicsSystem::ReadRegisterThunk(void*, ReGpuGraphicsSystem*, uint32_t addr)
{
    return regpu::driver::ReadRegister((addr & 0xFFFF) >> 2);
}

void ReGpuGraphicsSystem::WriteRegisterThunk(void*, ReGpuGraphicsSystem*, uint32_t addr, uint32_t value)
{
    regpu::driver::WriteRegister((addr & 0xFFFF) >> 2, value);
}

void ReGpuGraphicsSystem::SetInterruptCallback(uint32_t callback, uint32_t user_data)
{
    regpu::driver::SetInterruptCallback(callback, user_data);
}

void ReGpuGraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2)
{
    regpu::driver::InitializeRingBuffer(ptr, size_log2);
}

void ReGpuGraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2)
{
    regpu::driver::EnableReadPointerWriteBack(ptr, block_size_log2);
}

void ReGpuGraphicsSystem::InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t, bool)
{
    cacheRoot_ = cache_root;
}

void ReGpuGraphicsSystem::Shutdown()
{
    if (!started_)
        return;
    started_ = false;
    g_running = false;
    regpu::SaveCaches();
    // ReGPU has no Stop(); its threads run until process exit.
}
} // namespace glue2gpu
