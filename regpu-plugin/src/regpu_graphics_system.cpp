// rexgpu-regpu for SSX: ReGPU (https://github.com/zer0ducksgiven/ReGPU, rexglue-driver branch) behind
// ReXGlue's GPU plugin interface. Adapted from Glue2GPU's Forza Horizon plugin
// (https://github.com/zer0ducksgiven/Glue2GPU, MIT, see ../LICENSE); the Forza-specific parts are left out.
#include "regpu_graphics_system.h"
#include "linux_surface.h"
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
#ifndef _WIN32
#include <SDL3/SDL_video.h>
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <rex/logging.h>
#include <rex/filesystem.h>
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
                     "The standard renderer's resolution scale (kept for ssx.toml; ReGPU uses regpu_resolution)")
    .range(1, 3)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(regpu_resolution, "screen", "GPU",
                      "ReGPU's drawing resolution: screen (the monitor the game is on) or WIDTHxHEIGHT, "
                      "fitted to 16:9; changes while the game runs (resolution_scale is not used by ReGPU)");
REXCVAR_DEFINE_INT32(anisotropic_override, 3, "GPU",
                     "Texture filtering: -1 the game's, 0 off, 1-5 1x-16x anisotropic")
    .range(-1, 5);
// ReGPU's count of the dwords a shader uses (src/gpu/ucode.h).
namespace ucode { uint32_t UsedDwords(const uint32_t* microcodeBE, uint32_t dwords); }

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
#ifndef _WIN32
        // The main display's refresh rate from SDL, as the game's own setting (src/ssx_settings.cpp).
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        fps = mode && mode->refresh_rate > 1.0f ? int32_t(mode->refresh_rate + 0.5f) : 60;
#else
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        fps = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
                  ? int32_t(mode.dmDisplayFrequency)
                  : 60;
#endif
    }
    return uint32_t(std::clamp(fps, 30, 360));
}

// SSX's VSync setting (ssx_vsync): ReGPU then presents on the display's refresh (no tearing) and caps
// the frame rate at it; a lower frame rate setting still applies (issue #14).
bool Vsync()
{
    return rex::cvar::GetFlagByName("ssx_vsync") == "true";
}

// The drawing resolution (regpu_resolution): "screen" is the display mode of the monitor the game
// window is on (its real pixels, whatever the Windows scaling), else WIDTHxHEIGHT; 1280x720 if neither.
#ifdef _WIN32
HWND g_window = nullptr;
#endif
void DrawingSize(uint32_t& width, uint32_t& height)
{
    width = 1280;
    height = 720;
    const std::string v = REXCVAR_GET(regpu_resolution);
    if (v == "screen")
    {
#ifndef _WIN32
        // Linux: the game window's pixels (the screen's when fullscreen).
        if (auto* runtime = rex::Runtime::instance(); runtime && runtime->display_window() &&
            runtime->display_window()->GetActualPhysicalWidth())
        {
            width = runtime->display_window()->GetActualPhysicalWidth();
            height = runtime->display_window()->GetActualPhysicalHeight();
        }
#else
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        if (GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &info) &&
            EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        {
            width = mode.dmPelsWidth;
            height = mode.dmPelsHeight;
        }
#endif
    }
    else
    {
        std::sscanf(v.c_str(), "%ux%u", &width, &height);
    }
    // Note: the largest 16:9 size inside it, as the old 1x-3x scales were. SSX's scene doesn't take
    // ReGPU's wider-view path (its surfaces aren't 1280 wide), so 2560x1080 drew the 16:9 picture
    // stretched; real ultrawide needs that path made to work for SSX.
    if (uint64_t(width) * 9 > uint64_t(height) * 16)
        width = height * 16 / 9;
    else
        height = width * 9 / 16;
    width = std::clamp<uint32_t>(width & ~1u, 640, 7680);
    height = std::clamp<uint32_t>(height & ~1u, 360, 4320);
}

// The F1 menu's resolution, frame rate and texture filtering, applied while the game runs.
void FollowSettings()
{
    static uint32_t lastFps = 0;
    static bool lastVsync = false;
    static int32_t lastAniso = -2;
    static uint64_t lastSize = 0;
    uint32_t width, height;
    DrawingSize(width, height);
    if (const uint64_t size = uint64_t(width) << 32 | height; size != lastSize)
    {
        if (lastSize)
        {
            regpu::SetOutputSize(width, height);
            REXLOG_INFO("ReGPU: drawing at {}x{}", width, height);
        }
        lastSize = size;
    }
    if (const uint32_t fps = TargetFps(); fps != lastFps || Vsync() != lastVsync)
    {
        lastVsync = Vsync();
        regpu::SetFrameLimit(fps, lastVsync);
        lastFps = fps;
    }
    if (const int32_t aniso = REXCVAR_GET(anisotropic_override); aniso != lastAniso)
    {
        regpu::SetAnisotropy(aniso <= 0 ? aniso : 1 << (aniso - 1));
        lastAniso = aniso;
    }
}

// The window title shows the frame rate. Set from a thread of its own, as WM_SETTEXT waits for the
// window's thread. The same thread applies the F1 settings (FollowSettings).
#ifndef _WIN32
// Linux: the settings only (the title stays the game's).
void StartTitleFps(void*)
{
    std::thread([] {
        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            FollowSettings();
        }
    }).detach();
}
#else
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
#endif

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
// - every image the first 4 times it is resolved, at once: the flare lights of night Deadly
//   Descents are rendered and resolved at load, then compressed on the CPU, and the game draws up
//   to 3 of them one after another into the same image (with only the first written, a later
//   flare showed an earlier one's text, issue #7; ReXGlue's renderer reads back the first 4 too).
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
    static std::unordered_map<uint64_t, uint32_t> seen;
    const uint64_t key = uint64_t(copy.base) << 32 ^ uint64_t(copy.width) << 18 ^ uint64_t(copy.height) << 6 ^
                         copy.format ^ (copy.depth ? 1ull << 63 : 0);
    std::lock_guard lock(mutex);
    if (uint32_t& count = seen[key]; count < 4)
    {
        count++;
        w.mode = regpu::Writeback::Mode::Now;
    }
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

// Up to 1.9.5 ReGPU kept its caches in the working folder, which is ssx.exe's folder for a start
// from its shortcut, play.bat or RecompLauncher: moved from there once, so a warm cache stays warm.
// What the new folder has already is kept; other working folders aren't searched.
void MoveOldCaches(const std::filesystem::path& from, const std::filesystem::path& to)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::equivalent(from, to, ec))
        return;
    // (Linux: also vkd3d-proton's pipeline cache, which it kept in the working folder too.)
    for (const char* name : { "shadercache", "pipelines.manifest", "pipelines.bin", "vkd3d-proton.cache" })
    {
        const fs::path old = from / name, now = to / name;
        if (!fs::exists(old, ec) || fs::exists(now, ec))
            continue;
        fs::rename(old, now, ec);
        if (ec) // another drive (Documents elsewhere): copied, and the old one removed after
        {
            fs::copy(old, now, fs::copy_options::recursive, ec);
            fs::remove_all(ec ? now : old, ec);
        }
        if (fs::exists(old, ec))
            REXLOG_WARN("ReGPU: couldn't move {} to {}", old.string(), now.string());
        else
            REXLOG_INFO("ReGPU: moved {} to {}", old.string(), now.string());
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

// SSX's shaders for the first-boot compile (issue #17): static-shaders.bin beside the exe, written by
// ssx.exe --ssx_collect_shaders ("SSXS", the count, then per shader u8 pixel, u32 dwords, big-endian code),
// each cut to the dwords it uses, as ReGPU's own scanner does, so the hashes match the shaders the game draws.
std::vector<regpu::ScannedShader> ReadStaticShaders()
{
    std::vector<regpu::ScannedShader> out;
    std::ifstream f(rex::filesystem::GetExecutableFolder() / "static-shaders.bin", std::ios::binary);
    char magic[4];
    uint32_t count = 0;
    if (!f.read(magic, 4) || std::memcmp(magic, "SSXS", 4) != 0 || !f.read(reinterpret_cast<char*>(&count), 4))
        return out;
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t pixel = 0;
        uint32_t dwords = 0;
        if (!f.read(reinterpret_cast<char*>(&pixel), 1) || !f.read(reinterpret_cast<char*>(&dwords), 4) || dwords == 0 ||
            dwords > 0x2000)
            break;
        std::vector<uint32_t> code(dwords);
        if (!f.read(reinterpret_cast<char*>(code.data()), std::streamsize(dwords) * 4))
            break;
        code.resize(ucode::UsedDwords(code.data(), dwords));
        out.push_back({ pixel != 0, std::move(code) });
    }
    REXLOG_INFO("ReGPU: {} shaders from static-shaders.bin", out.size());
    return out;
}

// The screen ReGPU shows while it compiles the static shaders on the first start (a black window for half
// a minute looks like a hang): dark, with a progress bar of done / total. No text (no font here).
// The pipeline list shipped with the port (issue #17): pipeline-seed.txt beside the exe, in pipelines.manifest's
// format (P lines: shader hashes and render state). ReGPU builds them on the first start, after the static shaders.
const char* PipelineSeed()
{
    static const std::string text = [] {
        std::ifstream f(rex::filesystem::GetExecutableFolder() / "pipeline-seed.txt", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }();
    return text.c_str();
}

bool BootScreen(const wchar_t*, uint32_t done, uint32_t total, std::vector<uint8_t>& rgba)
{
    constexpr uint32_t kW = 1280, kH = 720, kBarW = 640, kBarH = 12, kX = (kW - kBarW) / 2, kY = kH * 3 / 4;
    rgba.assign(size_t(kW) * kH * 4, 0);
    for (size_t i = 3; i < rgba.size(); i += 4)
        rgba[i] = 255;  // opaque black
    const uint32_t filled = total ? uint32_t(uint64_t(kBarW) * std::min(done, total) / total) : 0;
    for (uint32_t y = kY; y < kY + kBarH; y++)
        for (uint32_t x = kX; x < kX + kBarW; x++)
        {
            uint8_t* p = &rgba[(size_t(y) * kW + x) * 4];
            const bool on = x - kX < filled;
            p[0] = on ? 0x4F : 0x20; // RGBA: the menus' cyan, on a dim track
            p[1] = on ? 0xC3 : 0x30;
            p[2] = on ? 0xF7 : 0x38;
        }
    return true;
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
    host.staticShaders = ReadStaticShaders;
    host.staticShadersSaved = [] { return true; };  // reading the file is quick: no "searching" screen
    host.bootScreen = BootScreen;
    host.pipelineSeed = PipelineSeed;
}

X_STATUS ReGpuGraphicsSystem::SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                                            rex::system::KernelState* kernel_state)
{
    g_dispatcher = dispatcher;
    g_memory = dispatcher->memory();
    g_kernel = kernel_state;
    FillHost();

    regpu::Config& config = regpu::GetConfig();
    // Present into ReXGlue's game window (it keeps input, focus, closing and fullscreen).
#ifdef _WIN32
    if (auto* runtime = rex::Runtime::instance(); runtime && runtime->display_window())
        config.hostWindow = runtime->display_window()->GetNativeWindowHandle();
    g_window = static_cast<HWND>(config.hostWindow);
#else
    // Linux: through Vulkan, on the window's Wayland or X11 surface.
    if (auto* runtime = rex::Runtime::instance(); runtime && runtime->display_window())
        UseWindowAsVulkanSurface(runtime->display_window(), config);
#endif
    // The drawing size (regpu_resolution, or SSX_REGPU_RES=WxH for a test).
    DrawingSize(config.width, config.height);
    if (const char* res = std::getenv("SSX_REGPU_RES"))
        std::sscanf(res, "%ux%u", &config.width, &config.height);
    REXLOG_INFO("ReGPU: drawing at {}x{}", config.width, config.height);
    // SSX_REGPU_UPSCALER=fsr1 with SSX_REGPU_QUALITY=0-4 (native, quality, balanced, performance, ultra).
    if (const char* up = std::getenv("SSX_REGPU_UPSCALER"); up && std::string(up) == "fsr1")
        config.upscaler = regpu::Config::Upscaler::Fsr1;
    if (const char* q = std::getenv("SSX_REGPU_QUALITY"))
        config.quality = uint32_t(std::atoi(q));
    config.vsync = Vsync();
    config.maxFps = TargetFps();
    config.windowTitle = L"SSX (ReGPU)";
    // ReGPU's caches (shadercache, pipelines.*) go in regpu in SSX's cache folder (userdata\cache in a
    // portable install, Documents\ssx\cache otherwise, or --cache_root), never in the working folder
    // (ReGPU's default): a start from another folder found no warm cache and left files there.
    // ReXGlue calls InitializeShaderStorage only after this, but its runtime has the folder already.
    if (cacheRoot_.empty())
        if (auto* runtime = rex::Runtime::instance())
            cacheRoot_ = runtime->cache_root();
    if (!cacheRoot_.empty())
    {
        config.userDir = cacheRoot_ / "regpu";
        std::error_code ec;
        std::filesystem::create_directories(config.userDir, ec);
        MoveOldCaches(rex::filesystem::GetExecutableFolder(), config.userDir);
#ifndef _WIN32
        // vkd3d-proton (D3D12 on Vulkan) writes its pipeline cache to the working folder unless told where.
        if (!std::getenv("VKD3D_SHADER_CACHE_PATH"))
            setenv("VKD3D_SHADER_CACHE_PATH", config.userDir.string().c_str(), 1);
#endif
    }
    // regpu.log beside the game's own logs (logs next to ssx.exe), which a bug report takes.
    if (!std::getenv("REGPU_LOG_DIR"))
    {
        const auto logs = rex::filesystem::GetExecutableFolder() / "logs";
        std::error_code ec;
        std::filesystem::create_directories(logs, ec);
        _putenv_s("REGPU_LOG_DIR", logs.string().c_str());
    }

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
    // SSX binds two texture fetch constants a frame with type 0 ("invalid") but sane contents (its
    // backdrop and a full-screen pass): sampled, as on the console (the standard renderer's
    // gpu_allow_invalid_fetch_constants, src/ssx_app.h).
    SetDefaultSwitch("REGPU_INVALID_FETCH_AS_TEXTURE");
    // ReGPU's status box (shader compiles, pipeline warm-up) is for testing, not for players
    // (SSX_REGPU_STATUS=1 shows it).
    if (!std::getenv("SSX_REGPU_STATUS"))
        SetDefaultSwitch("REGPU_NO_OVERLAY");
    if (!std::getenv("REGPU_DENSE_HASH_KB"))
        _putenv_s("REGPU_DENSE_HASH_KB", "4096");

    regpu::Start();
    started_ = true;
    g_running = true;
#ifdef _WIN32
    StartTitleFps(static_cast<HWND>(config.hostWindow));
#else
    StartTitleFps(nullptr);
#endif
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
