// On Windows, prints where the program crashed to stderr: the exception and each frame of the crashing thread's stack,
// named from the .pdb beside the exe. Recompiled functions carry the game's names (syms/), so a trace points at the
// game code that crashed. --stacks <seconds> (development) prints every thread's stack that often, for a game that
// hangs without crashing. On Linux, a crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) prints the address it faulted on
// and each frame of the crashing thread's stack, as glibc names it and as its offset in the module that holds it, which
// `addr2line -f -C -e <module> <offset>` names from the build's symbols; then the signal does what it would have.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#include <cstdio>
#include <thread>

static void print_stack(HANDLE process, HANDLE thread, CONTEXT context) {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (int i = 0; i < 48; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0) {
            break;
        }
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            fprintf(stderr, "  %2d %s+0x%llX", i, symbol->Name, (unsigned long long)displacement);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
                fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
            }
            fprintf(stderr, "\n");
        } else {
            fprintf(stderr, "  %2d %p\n", i, (void*)frame.AddrPC.Offset);
        }
    }
}

static LONG WINAPI print_crash(EXCEPTION_POINTERS* info) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    fprintf(stderr, "Crash: exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode,
        info->ExceptionRecord->ExceptionAddress);
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        fprintf(stderr, " (%s 0x%llX)", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");

    print_stack(process, GetCurrentThread(), *info->ContextRecord);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}


void install_crash_trace() {
    SetUnhandledExceptionFilter(print_crash);
}

void start_stack_dumps(double seconds) {
    std::thread([seconds]() {
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(process, nullptr, TRUE);
        DWORD self = GetCurrentThreadId();
        for (int round = 1;; round++) {
            Sleep((DWORD)(seconds * 1000));
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 entry{ sizeof(entry) };
            for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
                if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self) {
                    continue;
                }
                HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);
                if (thread == nullptr) {
                    continue;
                }
                PWSTR name = nullptr;
                char name8[128] = "";
                if (SUCCEEDED(GetThreadDescription(thread, &name)) && name != nullptr) {
                    WideCharToMultiByte(CP_UTF8, 0, name, -1, name8, sizeof(name8), nullptr, nullptr);
                    LocalFree(name);
                }
                if (SuspendThread(thread) != (DWORD)-1) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(thread, &context)) {
                        fprintf(stderr, "Stacks %d: thread %lu %s\n", round, entry.th32ThreadID, name8);
                        print_stack(process, thread, context);
                    }
                    ResumeThread(thread);
                }
                CloseHandle(thread);
            }
            CloseHandle(snapshot);
            fflush(stderr);
        }
    }).detach();
}
#elif defined(__linux__)
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <unistd.h>

// Only what a signal handler may call: fprintf to an unbuffered stderr, backtrace (loaded below before any crash),
// dladdr. Each frame is printed once named and once as <module> +<offset>.
static void print_crash(int sig, siginfo_t* info, void*) {
    void* frames[64];
    const int count = backtrace(frames, 64);
    fprintf(stderr, "crash: signal %d (%s) at address %p, thread %ld\n", sig, strsignal(sig), info ? info->si_addr : nullptr,
        (long)syscall(SYS_gettid));
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    for (int i = 0; i < count; i++) {
        Dl_info where{};
        if (dladdr(frames[i], &where) && (where.dli_fname != nullptr)) {
            fprintf(stderr, "  #%d %s +0x%lx\n", i, where.dli_fname, (unsigned long)((char*)frames[i] - (char*)where.dli_fbase));
        }
    }
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

void install_crash_trace() {
    void* warm[1];
    backtrace(warm, 1); // loads libgcc's unwinder now, not inside the handler
    static char alt_stack[1 << 16]; // a stack overflow's handler runs on a stack of its own
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = print_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (int sig : signals) {
        sigaction(sig, &sa, nullptr);
    }
}
void start_stack_dumps(double) {}
#else
void install_crash_trace() {}
void start_stack_dumps(double) {}
#endif
