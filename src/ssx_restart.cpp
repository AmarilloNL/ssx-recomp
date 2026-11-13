// ssx - restart the game in place (for settings that apply at startup).
//
// The new process gets --ssx_wait_pid=<this pid> and waits for this one to exit
// before touching saves or the shader cache.

#include "ssx_restart.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#else
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(ssx_wait_pid, 0, "SSX", "Internal: wait for this process to exit at startup")
    .debug_only();

#ifdef _WIN32
namespace {

std::wstring Quote(const std::wstring& arg) {
  return arg.find_first_of(L" \t") == std::wstring::npos ? arg : L"\"" + arg + L"\"";
}

}  // namespace

void SsxWaitForPreviousInstance() {
  if (int32_t pid = REXCVAR_GET(ssx_wait_pid)) {
    if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid))) {
      WaitForSingleObject(process, 15000);
      CloseHandle(process);
    }
  }
}

bool SsxSpawnRestart() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return false;
  std::wstring command = Quote(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (std::wstring_view(argv[i]).starts_with(L"--ssx_wait_pid")) continue;
    command += L" " + Quote(argv[i]);
  }
  LocalFree(argv);
  command += L" --ssx_wait_pid=" + std::to_wstring(GetCurrentProcessId());

  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION info = {};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &info)) {
    return false;
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return true;
}
#else
void SsxWaitForPreviousInstance() {
  if (int32_t pid = REXCVAR_GET(ssx_wait_pid)) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (kill(pid, 0) == 0 && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

bool SsxSpawnRestart() {
  std::ifstream file("/proc/self/cmdline", std::ios::binary);
  std::string all((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<std::string> args;
  for (size_t start = 0; start < all.size();) {
    size_t end = all.find('\0', start);
    if (end == std::string::npos) end = all.size();
    std::string arg = all.substr(start, end - start);
    if (args.empty() || !arg.starts_with("--ssx_wait_pid")) args.push_back(arg);
    start = end + 1;
  }
  if (args.empty()) return false;
  args.push_back("--ssx_wait_pid=" + std::to_string(getpid()));
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  pid_t child = 0;
  return posix_spawn(&child, "/proc/self/exe", nullptr, nullptr, argv.data(), environ) == 0;
}
#endif
