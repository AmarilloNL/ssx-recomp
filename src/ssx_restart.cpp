// ssx - restart the game in place (for settings that apply at startup).
//
// The new process gets --ssx_wait_pid=<this pid> and waits for this one to exit
// before touching saves or the shader cache.

#include "ssx_restart.h"

#include <string>

#include <windows.h>
#include <shellapi.h>

#include <rex/cvar.h>

#pragma comment(lib, "shell32.lib")

REXCVAR_DEFINE_INT32(ssx_wait_pid, 0, "SSX", "Internal: wait for this process to exit at startup")
    .debug_only();

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
