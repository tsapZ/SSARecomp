#include "tools/xex_check_exit_code.h"
#include "xex_verify.h"

#include <cstdio>
#include <filesystem>

#ifndef SSARECOMP_XEX_SHA256
#error "SSARECOMP_XEX_SHA256 must be defined by the build (see CMakeLists.txt)"
#endif

namespace {

int RunCheck(const std::filesystem::path& xex) {
  const auto result = ssarecomp::VerifyXex(xex, SSARECOMP_XEX_SHA256);
  const auto problem = ssarecomp::DescribeXexProblem(xex, result, SSARECOMP_XEX_SHA256);

  if (problem.empty()) {
    std::puts("OK");
    return ssarecomp::kExitMatch;
  }
  std::fputs(problem.c_str(), stderr);
  std::fputc('\n', stderr);
  return ssarecomp::ExitCodeForStatus(result.status);
}

}  // namespace

#ifdef _WIN32

// A plain main(argc, char** argv) receives argv narrowed to the system code page on Windows, so any
// path character outside it (e.g. a non-ASCII Windows username under a user's own AppData path)
// silently becomes '?' and the file is reported as unreadable. wmain gets the real Unicode argv,
// and std::filesystem::path's wstring constructor preserves it exactly -- no narrowing anywhere.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    std::fwprintf(stderr, L"usage: %s <path-to-default.xex>\n",
                  argc > 0 ? argv[0] : L"ssarecomp_xexcheck");
    return ssarecomp::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#else

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path-to-default.xex>\n",
                 argc > 0 ? argv[0] : "ssarecomp_xexcheck");
    return ssarecomp::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#endif
