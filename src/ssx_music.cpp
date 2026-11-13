// ssx - your own music, as the Xbox 360's media player gave it to SSX.
//
// SSX lets you pick a playlist for race, trick, survive and menu music (Music > Personalize Your
// Music). On the console the playlists were the dashboard's, the console's media player played them,
// and the game took the player's output (XMPCaptureOutput) to remix it as you ride. The SDK's media
// player is a stub: no playlists, no playback, the capture refused.
//
// Here the port is the media player for those requests. Every folder in <user data>/music is a
// playlist (named after the folder, its songs in name order, subfolders included), and music files
// right in that folder form one more, "Music". Songs are decoded with Windows' Media Foundation (MP3,
// AAC/M4A, WMA, FLAC, WAV; on Linux by the system's ffmpeg) to 48 kHz stereo and handed to the game's capture callback in steps of 256
// frames, once per audio frame of the game.
//
// The game's requests (media player app 0xFA): XamCreateEnumeratorHandle with messages 0x70026/27
// lists the playlists (820-byte entries, the name in UTF-16 at +572; the rest is ours), 0x70025 starts
// that listing, 0x7003B plays a playlist (the entry comes back), 0x7003D registers the capture
// callback, 0x70009 asks the state, 0x70008 sets shuffle/repeat, 0x70003-0x70007 are stop, pause,
// resume, next and previous. Everything else goes to the SDK as before.

#include "ssx_music.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#else
#include <cstdio>
#include <cwctype>
#endif

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xmemory.h>

#include "generated/default/ssx_pch.h"
#include "ssx_runtime_export.h"

namespace {

constexpr uint32_t kMediaPlayerApp = 0xFA;
constexpr uint32_t kEntryBytes = 820;
constexpr uint32_t kEntryNameOffset = 572;
constexpr uint32_t kEntryNameChars = (kEntryBytes - kEntryNameOffset) / 2 - 1;
constexpr uint32_t kStateChanged = 0x0A000001;  // XN_XMP_STATECHANGED
constexpr uint32_t kFramesPerCall = 256;        // one audio frame of the game
constexpr uint32_t kRate = 48000;

enum State : uint32_t { kIdle = 0, kPlaying = 1, kPaused = 2 };

struct Playlist {
  std::u16string name;
  std::vector<std::filesystem::path> songs;
};

std::filesystem::path music_dir;

// For the log (a path's string() throws on names outside the ANSI code page).
std::string Utf8(const std::filesystem::path& path) {
  auto u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
std::string Utf8(const std::u16string& s) { return Utf8(std::filesystem::path(s)); }

// ---- The music folder -------------------------------------------------------------------------------

bool IsMusicFile(const std::filesystem::path& path) {
  std::wstring ext = path.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  for (const wchar_t* known : {L".mp3", L".m4a", L".aac", L".wma", L".flac", L".wav", L".mp4"}) {
    if (ext == known) return true;
  }
  return false;
}

std::vector<std::filesystem::path> SongsIn(const std::filesystem::path& dir, bool recursive) {
  std::vector<std::filesystem::path> songs;
  std::error_code ec;
  if (recursive) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
      if (entry.is_regular_file(ec) && IsMusicFile(entry.path())) songs.push_back(entry.path());
    }
  } else {
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (entry.is_regular_file(ec) && IsMusicFile(entry.path())) songs.push_back(entry.path());
    }
  }
  std::sort(songs.begin(), songs.end());
  return songs;
}

std::vector<Playlist> ScanPlaylists() {
  std::vector<Playlist> playlists;
  std::error_code ec;
  std::vector<std::filesystem::path> folders;
  for (const auto& entry : std::filesystem::directory_iterator(music_dir, ec)) {
    if (entry.is_directory(ec)) folders.push_back(entry.path());
  }
  std::sort(folders.begin(), folders.end());
  for (const auto& folder : folders) {
    Playlist playlist{folder.filename().u16string(), SongsIn(folder, true)};
    if (!playlist.songs.empty()) playlists.push_back(std::move(playlist));
  }
  if (auto loose = SongsIn(music_dir, false); !loose.empty()) playlists.push_back({u"Music", std::move(loose)});
  for (auto& playlist : playlists) {
    if (playlist.name.size() > kEntryNameChars) playlist.name.resize(kEntryNameChars);
  }
  return playlists;
}

#ifndef _WIN32
// ---- Decoding (ffmpeg, Linux) -------------------------------------------------------------------------

// One song, decoded by the system's ffmpeg program to 48 kHz stereo float.
// Note: needs ffmpeg installed (most desktops have it); link libavformat if that's a problem.
class Decoder {
 public:
  bool Open(const std::filesystem::path& path) {
    Close();
    std::string quoted = "'";
    for (char c : path.string()) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
    pipe_ = popen(("ffmpeg -nostdin -v error -i " + quoted + " -vn -f f32le -ac 2 -ar 48000 - 2>/dev/null").c_str(), "r");
    if (!pipe_) return false;
    std::vector<float> first;
    if (!Read(first) || first.empty()) {  // not a song ffmpeg can read (or no ffmpeg)
      Close();
      return false;
    }
    pending_ = std::move(first);
    return true;
  }

  void Close() {
    if (pipe_) pclose(pipe_);
    pipe_ = nullptr;
    pending_.clear();
  }

  void LogFormat() const { REXLOG_INFO("Music:   decoded by ffmpeg to 48000 Hz stereo"); }

  // Appends up to about 100 ms of 48 kHz stereo frames to `out`. False at the end of the song.
  bool Read(std::vector<float>& out) {
    if (!pending_.empty()) {
      out.insert(out.end(), pending_.begin(), pending_.end());
      pending_.clear();
      return true;
    }
    if (!pipe_) return false;
    float buffer[4800 * 2];
    size_t got = std::fread(buffer, sizeof(float), std::size(buffer), pipe_);
    got -= got % 2;
    out.insert(out.end(), buffer, buffer + got);
    return got > 0;
  }

  ~Decoder() { Close(); }

 private:
  FILE* pipe_ = nullptr;
  std::vector<float> pending_;
};
#else
// ---- Decoding (Media Foundation) ---------------------------------------------------------------------

template <typename T>
void SafeRelease(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

// One song, decoded to 48 kHz stereo float. Media Foundation picks the decoder; when its reader won't
// resample, a linear resampler here does.
class Decoder {
 public:
  bool Open(const std::filesystem::path& path) {
    Close();
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader_))) return false;
    reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    IMFMediaType* type = nullptr;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
    type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    HRESULT hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type);
    if (FAILED(hr)) {
      type->DeleteItem(MF_MT_AUDIO_SAMPLES_PER_SECOND);
      type->DeleteItem(MF_MT_AUDIO_NUM_CHANNELS);
      hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type);
    }
    SafeRelease(type);
    IMFMediaType* actual = nullptr;
    if (FAILED(hr) || FAILED(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual))) {
      Close();
      return false;
    }
    rate_ = MFGetAttributeUINT32(actual, MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
    channels_ = std::max(1u, MFGetAttributeUINT32(actual, MF_MT_AUDIO_NUM_CHANNELS, 2));
    SafeRelease(actual);
    position_ = 0.0;
    have_previous_ = false;
    return true;
  }

  void Close() { SafeRelease(reader_); }

  void LogFormat() const {
    REXLOG_INFO("Music:   {} Hz, {} channel(s){}", rate_, channels_, rate_ != kRate ? ", resampled to 48000 Hz" : "");
  }

  // Appends up to about 100 ms of 48 kHz stereo frames to `out`. False at the end of the song.
  bool Read(std::vector<float>& out) {
    if (!reader_) return false;
    DWORD flags = 0;
    IMFSample* sample = nullptr;
    if (FAILED(reader_->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample)) ||
        (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
      SafeRelease(sample);
      return false;
    }
    if (!sample) return true;
    IMFMediaBuffer* buffer = nullptr;
    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
      BYTE* data = nullptr;
      DWORD bytes = 0;
      if (SUCCEEDED(buffer->Lock(&data, nullptr, &bytes))) {
        Convert(reinterpret_cast<const float*>(data), bytes / (sizeof(float) * channels_), out);
        buffer->Unlock();
      }
    }
    SafeRelease(buffer);
    SafeRelease(sample);
    return true;
  }

  ~Decoder() { Close(); }

 private:
  void Convert(const float* in, size_t frames, std::vector<float>& out) {
    auto stereo = [&](size_t i, float& l, float& r) {
      l = in[i * channels_];
      r = channels_ > 1 ? in[i * channels_ + 1] : l;
    };
    if (rate_ == kRate) {
      for (size_t i = 0; i < frames; ++i) {
        float l, r;
        stereo(i, l, r);
        out.push_back(l);
        out.push_back(r);
      }
      return;
    }
    // Note: linear resampling (only when Media Foundation won't resample); a windowed-sinc
    // resampler if anyone hears the difference.
    const double step = double(rate_) / kRate;
    for (size_t i = 0; i < frames; ++i) {
      float l, r;
      stereo(i, l, r);
      if (!have_previous_) {
        previous_l_ = l, previous_r_ = r, have_previous_ = true;
      }
      while (position_ < 1.0) {
        float t = float(position_);
        out.push_back(previous_l_ + (l - previous_l_) * t);
        out.push_back(previous_r_ + (r - previous_r_) * t);
        position_ += step;
      }
      position_ -= 1.0;
      previous_l_ = l, previous_r_ = r;
    }
  }

  IMFSourceReader* reader_ = nullptr;
  uint32_t rate_ = kRate, channels_ = 2;
  double position_ = 0.0;
  float previous_l_ = 0, previous_r_ = 0;
  bool have_previous_ = false;
};
#endif

// ---- The player --------------------------------------------------------------------------------------

class Player {
 public:
  void Play(const std::u16string& name) {
    std::lock_guard lock(mutex_);
    playlist_ = {};
    for (auto& playlist : ScanPlaylists()) {
      if (playlist.name == name) playlist_ = std::move(playlist);
    }
    if (playlist_.songs.empty()) {
      REXLOG_WARN("Music: playlist '{}' not found in {}", Utf8(name), Utf8(music_dir));
      state_ = kIdle;
      return;
    }
    order_.resize(playlist_.songs.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    if (shuffle_) std::shuffle(order_.begin(), order_.end(), std::mt19937(std::random_device{}()));
    position_ = 0;
    StartSong();
    state_ = kPlaying;
    REXLOG_INFO("Music: playing '{}' ({} songs{})", Utf8(name), playlist_.songs.size(), shuffle_ ? ", shuffled" : "");
    EnsureThread();
    wake_.notify_all();
  }

  void Stop() {
    std::lock_guard lock(mutex_);
    if (state_ != kIdle) REXLOG_INFO("Music: stopped");
    state_ = kIdle;
    pcm_.clear();
  }
  void Pause() {
    std::lock_guard lock(mutex_);
    if (state_ == kPlaying) {
      state_ = kPaused;
      REXLOG_INFO("Music: paused");
    }
  }
  void Resume() {
    std::lock_guard lock(mutex_);
    if (state_ == kPaused) {
      state_ = kPlaying;
      REXLOG_INFO("Music: resumed");
    }
    wake_.notify_all();
  }
  void Skip(int direction) {
    std::lock_guard lock(mutex_);
    if (playlist_.songs.empty()) return;
    position_ = (position_ + order_.size() + direction) % order_.size();
    StartSong();
    wake_.notify_all();
  }
  void SetBehavior(bool shuffle, bool no_repeat) {
    std::lock_guard lock(mutex_);
    shuffle_ = shuffle;
    no_repeat_ = no_repeat;
  }
  State state() const { return state_.load(); }

  // The next 256 frames (stereo, interleaved), silence where the decoder hasn't caught up.
  void Take(float* out) {
    std::lock_guard lock(mutex_);
    size_t have = std::min<size_t>(pcm_.size(), kFramesPerCall * 2);
    std::copy(pcm_.begin(), pcm_.begin() + have, out);
    std::fill(out + have, out + kFramesPerCall * 2, 0.0f);
    pcm_.erase(pcm_.begin(), pcm_.begin() + have);
    wake_.notify_all();
  }

 private:

  // (mutex held) Switches to the song at position_; the decoder thread opens it. `cut`: drop what is
  // decoded of the song before (a skip or a new playlist); a song that ended plays out first.
  void StartSong(bool cut = true) {
    if (cut) pcm_.clear();
    song_ = playlist_.songs[order_[position_]];
    song_changed_ = true;
  }

  // One decoder thread for the whole run (a detached std::thread is no longer joinable, so that
  // can't tell whether it was started: every playlist started another, and they played together).
  void EnsureThread() {
    if (thread_started_) return;
    thread_started_ = true;
    std::thread([this] { Run(); }).detach();
  }

  void Run() {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
#endif
    Decoder decoder;
    std::vector<float> chunk;
    std::unique_lock lock(mutex_);
    for (;;) {
      // Keep about a second decoded ahead.
      wake_.wait_for(lock, std::chrono::milliseconds(50),
                     [&] { return song_changed_ || (state_ == kPlaying && pcm_.size() < kRate * 2); });
      if (song_changed_) {
        song_changed_ = false;
        std::filesystem::path song = song_;
        lock.unlock();
        bool opened = decoder.Open(song);
        lock.lock();
        if (song_changed_) continue;
        if (!opened) {
          REXLOG_WARN("Music: can't play {}", Utf8(song));
          NextOrEnd();
          continue;
        }
        REXLOG_INFO("Music: {}", Utf8(song.filename()));
        decoder.LogFormat();
      }
      if (state_ != kPlaying || pcm_.size() >= kRate * 2) continue;
      chunk.clear();
      lock.unlock();
      bool more = decoder.Read(chunk);
      lock.lock();
      if (song_changed_) continue;
      pcm_.insert(pcm_.end(), chunk.begin(), chunk.end());
      if (!more) NextOrEnd();
    }
  }

  // (mutex held) The song ended: the next one, or the end of the playlist.
  void NextOrEnd() {
    if (playlist_.songs.empty()) return;
    if (position_ + 1 >= order_.size() && no_repeat_) {
      state_ = kIdle;
      return;
    }
    position_ = (position_ + 1) % order_.size();
    StartSong(false);
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  bool thread_started_ = false;
  Playlist playlist_;
  std::vector<size_t> order_;
  size_t position_ = 0;
  std::filesystem::path song_;
  bool song_changed_ = false;
  bool shuffle_ = false, no_repeat_ = false;
  std::atomic<State> state_{kIdle};
  std::deque<float> pcm_;
};

Player player;
std::atomic<uint32_t> capture_callback{0}, capture_context{0};

void Broadcast(State state) { rex::system::kernel_state()->BroadcastNotification(kStateChanged, state); }

// Finishes a request the port handled: with an overlapped structure the result goes there.
void Complete(PPCContext& ctx, uint32_t overlapped) {
  if (overlapped) {
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlapped, 0);
    ctx.r3.u64 = 0x3E5;  // ERROR_IO_PENDING
  } else {
    ctx.r3.u64 = 0;
  }
}

// A media player request with a buffer (StartIORequest / InProcessCall). True when handled here.
bool HandleRequest(uint8_t* base, uint32_t message, uint32_t buffer) {
  switch (message) {
    case 0x00070003:  // stop
      player.Stop();
      Broadcast(kIdle);
      return true;
    case 0x00070004:  // pause
      player.Pause();
      Broadcast(player.state());
      return true;
    case 0x00070005:  // resume
      player.Resume();
      Broadcast(player.state());
      return true;
    case 0x00070006:  // next
      player.Skip(1);
      return true;
    case 0x00070007:  // previous
      player.Skip(-1);
      return true;
    case 0x00070008:  // playback behavior: client, playback mode (1 shuffle), repeat mode (1 none)
      if (buffer) player.SetBehavior(REX_LOAD_U32(buffer + 4) == 1, REX_LOAD_U32(buffer + 8) == 1);
      return true;
    case 0x00070009:  // state: client, out pointer
      if (buffer && REX_LOAD_U32(buffer + 4)) REX_STORE_U32(REX_LOAD_U32(buffer + 4), player.state());
      return true;
    case 0x00070025:  // the playlist listing starts (the enumerator holds the playlists already)
      return true;
    case 0x0007003B: {  // play a playlist: client, then the playlist's entry
      if (!buffer) return true;
      std::u16string name;
      for (uint32_t i = 0; i < kEntryNameChars; ++i) {
        char16_t c = char16_t(REX_LOAD_U16(buffer + 4 + kEntryNameOffset + i * 2));
        if (!c) break;
        name += c;
      }
      player.Play(name);
      Broadcast(player.state());
      return true;
    }
    case 0x0007003D:  // capture the output: client, callback, context, flags
      if (buffer) {
        capture_callback = REX_LOAD_U32(buffer + 4);
        capture_context = REX_LOAD_U32(buffer + 8);
      }
      return true;
    default:
      return false;
  }
}

}  // namespace

void SsxSetMusicDir(const std::filesystem::path& dir) {
  music_dir = dir;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
}

extern "C" REX_FUNC(__imp__XMsgInProcessCall) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XMsgInProcessCall");
  if (ctx.r3.u32 == kMediaPlayerApp && HandleRequest(base, ctx.r4.u32, ctx.r5.u32)) {
    ctx.r3.u64 = 0;
    return;
  }
  original(ctx, base);
}

extern "C" REX_FUNC(__imp__XMsgStartIORequest) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XMsgStartIORequest");
  if (ctx.r3.u32 == kMediaPlayerApp && HandleRequest(base, ctx.r4.u32, ctx.r6.u32)) {
    Complete(ctx, ctx.r5.u32);
    return;
  }
  original(ctx, base);
}

extern "C" REX_FUNC(__imp__XMsgStartIORequestEx) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XMsgStartIORequestEx");
  if (ctx.r3.u32 == kMediaPlayerApp && HandleRequest(base, ctx.r4.u32, ctx.r6.u32)) {
    Complete(ctx, ctx.r5.u32);
    return;
  }
  original(ctx, base);
}

// XamCreateEnumeratorHandle(user index, app, open message, close message, extra bytes, items per call,
// flags, out handle). The media player's playlist listing gets an enumerator with the music folders.
extern "C" REX_FUNC(__imp__XamCreateEnumeratorHandle) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XamCreateEnumeratorHandle");
  if (ctx.r4.u32 != kMediaPlayerApp || ctx.r5.u32 != 0x00070026) {
    original(ctx, base);
    return;
  }
  uint32_t items_per_call = std::max(1u, ctx.r8.u32), out_handle = ctx.r10.u32;
  auto enumerator = rex::system::make_object<rex::system::XStaticUntypedEnumerator>(
      rex::system::kernel_state(), items_per_call, kEntryBytes);
  void* extra = nullptr;
  if (XFAILED(enumerator->Initialize(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r9.u32, ctx.r7.u32,
                                     &extra))) {
    ctx.r3.u64 = 0x80070008;  // E_OUTOFMEMORY
    return;
  }
  auto playlists = ScanPlaylists();
  for (uint32_t i = 0; i < playlists.size(); ++i) {
    uint8_t* entry = enumerator->AppendItem();
    std::memset(entry, 0, kEntryBytes);
    entry[3] = uint8_t(i + 1);  // ours: a number for the log
    for (size_t c = 0; c < playlists[i].name.size(); ++c) {
      entry[kEntryNameOffset + c * 2] = uint8_t(playlists[i].name[c] >> 8);
      entry[kEntryNameOffset + c * 2 + 1] = uint8_t(playlists[i].name[c]);
    }
  }
  if (out_handle) REX_STORE_U32(out_handle, enumerator->handle());
  ctx.r3.u64 = 0;
}

// The game's mixed audio frame goes out here, once per 256 frames: the capture callback gets the next
// 256 frames of music right after, from the game's audio thread, as the console's player gave them.
extern "C" REX_FUNC(__imp__XAudioSubmitRenderDriverFrame) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XAudioSubmitRenderDriverFrame");
  original(ctx, base);
  uint32_t callback = capture_callback.load();
  if (!callback || player.state() != kPlaying) return;
  static uint32_t buffer = rex::system::kernel_memory()->SystemHeapAlloc(kFramesPerCall * 8);
  if (!buffer) return;
  float frames[kFramesPerCall * 2];
  player.Take(frames);
  for (uint32_t i = 0; i < kFramesPerCall * 2; ++i) {
    uint32_t bits;
    std::memcpy(&bits, &frames[i], 4);
    REX_STORE_U32(buffer + i * 4, bits);
  }
  PPCContext saved = ctx;
  ctx.r3.u64 = buffer;
  ctx.r4.u64 = capture_context.load();
  ctx.r5.u64 = kFramesPerCall * 8;
  REX_CALL_INDIRECT_FUNC(callback);
  ctx = saved;
}
