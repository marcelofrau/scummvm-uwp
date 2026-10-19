#include "pch.h"
#include "App.h"
#include "Bootstrap.h"
#include "SDL2/SDL.h"

#include <appmodel.h>
#pragma comment(lib, "onecoreuap.lib")

using namespace Windows::ApplicationModel::Core;
using namespace scummvm_uwp;

// Forward declaration — defined in SdlMain.cpp (compiled without /ZW)
extern "C" int sdl_main(int argc, char* argv[]);

// Bridge: SdlMain.cpp (no /ZW) calls Bootstrap via this C-linkage wrapper
// (main.cpp is compiled with /ZW, so Bootstrap name mangling matches).
extern "C" bool Bootstrap_Run()
{
    return Bootstrap::Run();
}

// Bridge: SDL2/WinRT only calls CoreWindow::Activate() from its Activated
// handler, which fires on the first event pump — i.e. after the whole boot.
// Without a debugger the Xbox PLM kills an app whose window is not activated
// in time (DISCOVERIES §18), so sdl_main activates the window up front.
extern "C" void WinRT_ActivateWindow()
{
    try
    {
        auto window = Windows::UI::Core::CoreWindow::GetForCurrentThread();
        if (window)
        {
            window->Activate();
            BootTrace(L"CoreWindow activated");
        }
        else
        {
            BootTrace(L"CoreWindow activate: no window on this thread");
        }
    }
    catch (...)
    {
        BootTrace(L"CoreWindow activate: exception");
    }
}

// Bridge: log the app memory budget. On Xbox an "App"-type package is capped at
// 1 GB (Game: 5 GB) and the cap is NOT enforced under the VS debugger —
// another debugger-only difference (docs/XBOX-TILE-LAUNCH-KILL.md).
extern "C" void WinRT_LogMemory(const char* tag)
{
    try
    {
        using Windows::System::MemoryManager;
        unsigned long long usage = MemoryManager::AppMemoryUsage;
        unsigned long long limit = MemoryManager::AppMemoryUsageLimit;
        spdlog::info("[mem] {} usage={}MB limit={}MB", tag ? tag : "?",
            (unsigned long long)(usage >> 20), (unsigned long long)(limit >> 20));
    }
    catch (...)
    {
        spdlog::warn("[mem] {} MemoryManager unavailable", tag ? tag : "?");
    }
}

// OutputDebugString raises DBG_PRINTEXCEPTION_C/_WIDE_C. With a debugger the
// debugger consumes it before any SEH dispatch; without one the system walks
// the SEH frames. The core runs ScummVM (and all its GL → Mesa calls) on a
// libco amd64 stack that is outside the TEB stack limits, so that frame walk
// fails (EXCEPTION_STACK_INVALID) and the process dies silently — no
// unhandled-exception filter runs. Mesa's os_log_message calls
// OutputDebugStringA unconditionally. Vectored handlers run before the frame
// walk, so consuming these codes here makes ODS a no-op anywhere.
// Same codes as the proven fix in 226d29f (App.cpp, now dead code).
static LONG NTAPI ConsumeDebugPrintException(PEXCEPTION_POINTERS ep)
{
    if (ep && ep->ExceptionRecord)
    {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code == 0x40010006 || code == 0x40010007 || code == 0x4001000A)
            return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static std::wstring LocalStateDir()
{
    try
    {
        auto p = Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data();
        return std::wstring(p);
    }
    catch (...) { return L""; }
}

// Resolve the single shared log file (LocalState\scummvm-debug.log) as early
// as possible. ApplicationData LocalFolder is the proven path; a Win32
// fallback (LOCALAPPDATA + package family name) covers the case where the
// app-data context is not ready yet. Only a path that probe-opens is accepted.
static std::wstring ResolveLogPath()
{
    std::wstring dir = LocalStateDir();

    if (dir.empty())
    {
        wchar_t fam[256] = { 0 };
        UINT32 len = 255;
        if (GetPackageFamilyName(GetCurrentProcess(), &len, fam) == ERROR_SUCCESS)
        {
            wchar_t base[512] = { 0 };
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, 511);
            if (n > 0 && n < 512)
                dir = std::wstring(base) + L"\\Packages\\" + fam + L"\\LocalState";
        }
    }

    if (dir.empty())
        return L"";

    std::wstring path = dir + L"\\scummvm-debug.log";
    std::ofstream probe(path, std::ios::binary | std::ios::app);
    return probe ? path : L"";
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    std::wstring p = spdlog::g_logPath.empty()
        ? (LocalStateDir() + L"\\scummvm-debug.log")
        : spdlog::g_logPath;
    std::ofstream f(p, std::ios::app);
    if (f)
    {
        char mod[MAX_PATH] = { 0 };
        HMODULE h = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &h);
        if (h)
            GetModuleFileNameA(h, mod, MAX_PATH);
        f << "CRASH code=0x" << std::hex << ep->ExceptionRecord->ExceptionCode
          << " addr=0x" << ep->ExceptionRecord->ExceptionAddress
          << " thread=0x" << std::hex << GetCurrentThreadId()
          << " mod=" << (mod[0] ? mod : "?") << "\n";
        f.flush();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

ref class Direct3DApplicationSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
{
public:
    virtual Windows::ApplicationModel::Core::IFrameworkView^ CreateView()
    {
        return ref new App();
    }
};

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^ args)
{
    std::wstring logPath = ResolveLogPath();
    if (!logPath.empty())
    {
        LogInit(logPath.c_str());
        ::OutputDebugStringW((L"scummvm-debug.log -> " + logPath + L"\n").c_str());
    }

    SetUnhandledExceptionFilter(CrashFilter);
    BootTrace(L"main enter — " FRONTEND_VERSION);

    // First in the chain (1) so no other VEH sees ODS exceptions first.
    // Resolved dynamically, same as the old App.cpp (UWP partition headers).
    typedef LONG(NTAPI* PVectHandlerFn)(PEXCEPTION_POINTERS);
    typedef PVOID(WINAPI* PAddVectoredFn)(ULONG, PVectHandlerFn);
    PAddVectoredFn addVeh = (PAddVectoredFn)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "AddVectoredExceptionHandler");
    if (addVeh && addVeh(1, ConsumeDebugPrintException))
        BootTrace(L"VEH: ODS consumer installed");
    else
        BootTrace(L"VEH: ODS consumer FAILED to install");
    BootTrace(IsDebuggerPresent() ? L"debugger: PRESENT" : L"debugger: absent");

    // GL mode: delegate entire lifecycle to SDL_WinRTRunApp.
    // SDL creates its own CoreWindow + IFrameworkView, calls sdl_main (in SdlMain.cpp).
    // Our Direct3DApplicationSource/App are never used — SDL owns the event loop.
    BootTrace(L"SDL_WinRTRunApp start");
    int result = SDL_WinRTRunApp(sdl_main, nullptr);
    BootTrace(L"main exit after SDL_WinRTRunApp");
    return result;
}
