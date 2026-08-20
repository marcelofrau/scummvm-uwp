#include "pch.h"
#include "App.h"

#include <appmodel.h>
#pragma comment(lib, "onecoreuap.lib")

using namespace Windows::ApplicationModel::Core;
using namespace scummvm_uwp;

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
    auto source = ref new Direct3DApplicationSource();
    BootTrace(L"CoreApplication::Run start");
    CoreApplication::Run(source);
    BootTrace(L"main exit after Run");
    return 0;
}
