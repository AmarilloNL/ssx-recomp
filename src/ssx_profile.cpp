// ssx - player name and gamer picture. The runtime's profile is always called "User" and has a
// plain white picture; these replace them with the ones chosen in the settings.

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <rex/cvar.h>

#include "ssx_runtime_export.h"

#pragma comment(lib, "windowscodecs.lib")

REXCVAR_DEFINE_STRING(ssx_player_name, "", "SSX", "Player name shown in the game (empty: User)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(ssx_player_picture, "", "SSX", "Image file shown as the gamer picture")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

using Microsoft::WRL::ComPtr;

// Copies the player name (if set) over the runtime's into a guest char buffer of `size` bytes.
void WriteName(uint8_t* dest, uint32_t size) {
  const std::string& name = REXCVAR_GET(ssx_player_name);
  if (name.empty() || !size) return;
  size_t n = std::min<size_t>(name.size(), size - 1);
  std::memcpy(dest, name.data(), n);
  dest[n] = 0;
}

// The picture file centre-cropped to a square and scaled to size x size, as 0xAARRGGBB; empty
// without a picture or if it can't be read.
std::vector<uint32_t> LoadPicture(uint32_t size) {
  const std::string& path = REXCVAR_GET(ssx_player_picture);
  if (path.empty()) return {};
  std::wstring wpath(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), int(wpath.size()));
  HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::vector<uint32_t> pixels(size_t(size) * size);
  bool ok = [&] {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapClipper> clipper;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    UINT w, h;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h))) {
      return false;
    }
    UINT side = std::min(w, h);
    WICRect crop = {INT((w - side) / 2), INT((h - side) / 2), INT(side), INT(side)};
    return SUCCEEDED(factory->CreateBitmapClipper(&clipper)) &&
           SUCCEEDED(clipper->Initialize(frame.Get(), &crop)) &&
           SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
           SUCCEEDED(scaler->Initialize(clipper.Get(), size, size,
                                        WICBitmapInterpolationModeHighQualityCubic)) &&
           SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
           SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom)) &&
           SUCCEEDED(converter->CopyPixels(nullptr, size * 4, UINT(pixels.size() * 4),
                                           reinterpret_cast<BYTE*>(pixels.data())));
  }();
  if (SUCCEEDED(com)) CoUninitialize();
  if (!ok) pixels.clear();
  return pixels;
}

}  // namespace

// XamUserGetName(r3 = user, r4 = char*, r5 = size).
extern "C" REX_FUNC(__imp__XamUserGetName) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XamUserGetName");
  uint32_t buffer = ctx.r4.u32, size = std::min(ctx.r5.u32, 16u);
  original(ctx, base);
  if (ctx.r3.u32 == 0) WriteName(base + buffer, size);
}

// XamUserGetSigninInfo(r3 = user, r4 = flags, r5 = X_USER_SIGNIN_INFO*, name: char[16] at +24).
extern "C" REX_FUNC(__imp__XamUserGetSigninInfo) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XamUserGetSigninInfo");
  uint32_t info = ctx.r5.u32;
  original(ctx, base);
  if (ctx.r3.u32 == 0) WriteName(base + info + 24, 16);
}

// XamReadTileToTexture(r3 = tile type, r4 = title, r5 = tile id, r6 = user, r7 = buffer,
// r8 = pitch, r9 = height, r10 = overlapped). SSX reads the player's (square) gamer picture
// into a linear 32bpp texture this way.
extern "C" REX_FUNC(__imp__XamReadTileToTexture) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XamReadTileToTexture");
  uint32_t buffer = ctx.r7.u32, pitch = ctx.r8.u32, size = ctx.r9.u32;
  std::vector<uint32_t> pixels = size && pitch >= size * 4 ? LoadPicture(size) : std::vector<uint32_t>();
  // The runtime rejects tile id 0 (the user's own picture); any other id completes the request.
  if (!pixels.empty() && !ctx.r5.u64) ctx.r5.u64 = 1;
  original(ctx, base);
  if (pixels.empty() || (ctx.r3.u32 != 0 && ctx.r3.u32 != 997)) return;  // success / IO pending
  for (uint32_t y = 0; y < size; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(base + buffer + y * pitch);
    for (uint32_t x = 0; x < size; ++x) row[x] = __builtin_bswap32(pixels[y * size + x]);
  }
}
