#include "util.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {
std::atomic<bool> g_handling_crash{false};

struct CrashContextData {
  std::atomic<int> worldW{0};
  std::atomic<int> worldH{0};
  std::atomic<int> dayCount{0};
  std::atomic<int> population{0};
  std::atomic<int> lastHumanId{-1};
  std::atomic<int> lastHumanX{0};
  std::atomic<int> lastHumanY{0};
  char stage[64] = "startup";
  char note[128] = "-";
};

CrashContextData g_crash_context;

const char* SafeStr(const char* value) {
  return (value && value[0] != '\0') ? value : "unknown";
}

#ifdef _WIN32
size_t ModuleSize(HMODULE module) {
  if (!module) return 0;
  auto* base = reinterpret_cast<const unsigned char*>(module);
  auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  return static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
}

const char* BaseName(const char* path) {
  if (!path) return "unknown";
  const char* last = path;
  for (const char* it = path; *it; ++it) {
    if (*it == '\\' || *it == '/') last = it + 1;
  }
  return (*last != '\0') ? last : path;
}
#endif

void WriteCrashLogHeader(const char* reason) {
  FILE* file = std::fopen("crash.log", "a");
  if (!file) return;

  std::time_t now = std::time(nullptr);
  std::tm* tm = std::localtime(&now);
  char timebuf[32] = "unknown";
  if (tm) {
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
  }

  std::fprintf(file, "==== Crash ====\n");
  std::fprintf(file, "time: %s\n", timebuf);
  std::fprintf(file, "reason: %s\n", reason ? reason : "unknown");
  std::fprintf(file, "stage: %s\n", SafeStr(g_crash_context.stage));
  std::fprintf(file, "note: %s\n", SafeStr(g_crash_context.note));
  std::fprintf(file, "world: %d x %d\n", g_crash_context.worldW.load(),
               g_crash_context.worldH.load());
  std::fprintf(file, "day: %d\n", g_crash_context.dayCount.load());
  std::fprintf(file, "population: %d\n", g_crash_context.population.load());
  std::fprintf(file, "last human: id=%d pos=(%d,%d)\n", g_crash_context.lastHumanId.load(),
               g_crash_context.lastHumanX.load(), g_crash_context.lastHumanY.load());
#ifdef _WIN32
  std::fprintf(file, "pid: %lu tid: %lu\n",
               static_cast<unsigned long>(GetCurrentProcessId()),
               static_cast<unsigned long>(GetCurrentThreadId()));
  HMODULE exe = GetModuleHandleA(nullptr);
  if (exe) {
    char modulePath[MAX_PATH] = "unknown";
    GetModuleFileNameA(exe, modulePath, MAX_PATH);
    std::fprintf(file, "module: %s base=%p size=0x%llx\n", BaseName(modulePath), exe,
                 static_cast<unsigned long long>(ModuleSize(exe)));
  }
#else
  std::fprintf(file, "pid: %d\n", static_cast<int>(getpid()));
#endif
  std::fflush(file);
  std::fclose(file);
}

void WriteCrashStackSimple(void* const* stack, int count) {
  FILE* file = std::fopen("crash.log", "a");
  if (!file) return;
  std::fprintf(file, "stack:\n");
  for (int i = 0; i < count; ++i) {
    std::fprintf(file, "  [%d] %p\n", i, stack[i]);
  }
  std::fprintf(file, "==============\n");
  std::fflush(file);
  std::fclose(file);
}

#ifdef _WIN32
void WriteCrashStackDetailed(void* const* stack, int count) {
  FILE* file = std::fopen("crash.log", "a");
  if (!file) return;
  std::fprintf(file, "stack:\n");
  for (int i = 0; i < count; ++i) {
    void* addr = stack[i];
    HMODULE module = nullptr;
    char modulePath[MAX_PATH] = "unknown";
    size_t moduleSize = 0;
    uintptr_t base = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &module)) {
      GetModuleFileNameA(module, modulePath, MAX_PATH);
      base = reinterpret_cast<uintptr_t>(module);
      moduleSize = ModuleSize(module);
    }
    uintptr_t address = reinterpret_cast<uintptr_t>(addr);
    std::fprintf(file, "  [%d] %p", i, addr);
    if (module) {
      const char* name = BaseName(modulePath);
      std::fprintf(file, " %s+0x%llx", name,
                   static_cast<unsigned long long>(address - base));
      if (moduleSize > 0) {
        std::fprintf(file, " (size=0x%llx)", static_cast<unsigned long long>(moduleSize));
      }
    }
    std::fprintf(file, "\n");
  }
  std::fprintf(file, "==============\n");
  std::fflush(file);
  std::fclose(file);
}

void WriteExceptionDetails(EXCEPTION_POINTERS* info) {
  FILE* file = std::fopen("crash.log", "a");
  if (!file) return;
  if (info && info->ExceptionRecord) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    std::fprintf(file, "exception_code: 0x%08lx\n", static_cast<unsigned long>(code));
    std::fprintf(file, "exception_addr: %p\n", info->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) {
      ULONG_PTR access = info->ExceptionRecord->ExceptionInformation[0];
      ULONG_PTR address = info->ExceptionRecord->ExceptionInformation[1];
      const char* accessType = (access == 0) ? "read" : (access == 1) ? "write" : "exec";
      std::fprintf(file, "access: %s at %p\n", accessType,
                   reinterpret_cast<void*>(address));
    }
  }
  if (info && info->ContextRecord) {
    CONTEXT* ctx = info->ContextRecord;
    std::fprintf(file, "rip=%p rsp=%p rbp=%p\n", reinterpret_cast<void*>(ctx->Rip),
                 reinterpret_cast<void*>(ctx->Rsp), reinterpret_cast<void*>(ctx->Rbp));
    std::fprintf(file, "rax=%p rbx=%p rcx=%p rdx=%p\n", reinterpret_cast<void*>(ctx->Rax),
                 reinterpret_cast<void*>(ctx->Rbx), reinterpret_cast<void*>(ctx->Rcx),
                 reinterpret_cast<void*>(ctx->Rdx));
    std::fprintf(file, "rsi=%p rdi=%p r8=%p r9=%p\n", reinterpret_cast<void*>(ctx->Rsi),
                 reinterpret_cast<void*>(ctx->Rdi), reinterpret_cast<void*>(ctx->R8),
                 reinterpret_cast<void*>(ctx->R9));
    std::fprintf(file, "r10=%p r11=%p r12=%p r13=%p\n", reinterpret_cast<void*>(ctx->R10),
                 reinterpret_cast<void*>(ctx->R11), reinterpret_cast<void*>(ctx->R12),
                 reinterpret_cast<void*>(ctx->R13));
    std::fprintf(file, "r14=%p r15=%p\n", reinterpret_cast<void*>(ctx->R14),
                 reinterpret_cast<void*>(ctx->R15));
  }
  std::fflush(file);
  std::fclose(file);
}
#endif

void WriteCrashLog(const char* reason, void* const* stack, int count) {
  WriteCrashLogHeader(reason);
  WriteCrashStackSimple(stack, count);
}

#ifdef _WIN32
void HandleSignal(int sig) {
  if (g_handling_crash.exchange(true)) {
    std::_Exit(1);
  }
  void* stack[64] = {};
  int count = static_cast<int>(CaptureStackBackTrace(0, 64, stack, nullptr));
  char reason[32] = {};
  std::snprintf(reason, sizeof(reason), "signal %d", sig);
  WriteCrashLogHeader(reason);
  WriteCrashStackDetailed(stack, count);

  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

LONG WINAPI HandleException(EXCEPTION_POINTERS* exception_info) {
  if (g_handling_crash.exchange(true)) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  void* stack[64] = {};
  int count = static_cast<int>(CaptureStackBackTrace(0, 64, stack, nullptr));
  WriteCrashLogHeader("unhandled exception");
  WriteExceptionDetails(exception_info);
  WriteCrashStackDetailed(stack, count);
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
void HandleSignal(int sig, siginfo_t* info, void* /*context*/) {
  if (g_handling_crash.exchange(true)) {
    std::_Exit(1);
  }
  void* stack[64] = {};
  int count = backtrace(stack, 64);
  char reason[64] = {};
  if (info && info->si_addr) {
    std::snprintf(reason, sizeof(reason), "signal %d addr=%p", sig, info->si_addr);
  } else {
    std::snprintf(reason, sizeof(reason), "signal %d", sig);
  }
  WriteCrashLog(reason, stack, count);
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
#endif

void HandleTerminate() {
  if (g_handling_crash.exchange(true)) {
    std::_Exit(1);
  }
  void* stack[64] = {};
#ifdef _WIN32
  int count = static_cast<int>(CaptureStackBackTrace(0, 64, stack, nullptr));
  WriteCrashLogHeader("terminate");
  WriteCrashStackDetailed(stack, count);
#else
  int count = backtrace(stack, 64);
  WriteCrashLog("terminate", stack, count);
#endif
  std::abort();
}
}  // namespace

Random::Random() {
  std::random_device rd;
  rng_.seed(rd());
}

Random::Random(uint32_t seed) : rng_(seed) {}

int Random::RangeInt(int min_inclusive, int max_inclusive) {
  std::uniform_int_distribution<int> dist(min_inclusive, max_inclusive);
  return dist(rng_);
}

float Random::RangeFloat(float min_inclusive, float max_inclusive) {
  std::uniform_real_distribution<float> dist(min_inclusive, max_inclusive);
  return dist(rng_);
}

bool Random::Chance(float probability) {
  if (probability <= 0.0f) return false;
  if (probability >= 1.0f) return true;
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng_) < probability;
}

void InstallCrashHandlers() {
#ifdef _WIN32
  std::signal(SIGABRT, HandleSignal);
  std::signal(SIGFPE, HandleSignal);
  std::signal(SIGILL, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  SetUnhandledExceptionFilter(HandleException);
#else
  struct sigaction sa = {};
  sa.sa_sigaction = HandleSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGFPE, &sa, nullptr);
  sigaction(SIGILL, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#endif
  std::set_terminate(HandleTerminate);
}

void CrashContextSetStage(const char* stage) {
  std::snprintf(g_crash_context.stage, sizeof(g_crash_context.stage), "%s",
                stage ? stage : "");
}

void CrashContextSetWorld(int width, int height) {
  g_crash_context.worldW.store(width);
  g_crash_context.worldH.store(height);
}

void CrashContextSetDay(int dayCount) { g_crash_context.dayCount.store(dayCount); }

void CrashContextSetPopulation(int population) { g_crash_context.population.store(population); }

void CrashContextSetHuman(int id, int x, int y) {
  g_crash_context.lastHumanId.store(id);
  g_crash_context.lastHumanX.store(x);
  g_crash_context.lastHumanY.store(y);
}

void CrashContextSetNote(const char* note) {
  const char* value = (note && note[0] != '\0') ? note : "-";
  std::snprintf(g_crash_context.note, sizeof(g_crash_context.note), "%s", value);
}
