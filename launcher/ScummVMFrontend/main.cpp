#include "pch.h"
#include "App.h"

using namespace Windows::ApplicationModel::Core;
using namespace scummvm_uwp;

static std::wstring CrashLogPath()
{
    try
    {
        auto p = Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data();
        return std::wstring(p) + L"\\crash.log";
    }
    catch (...) { return L"C:\\scummvm-crash.log"; }
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    std::ofstream f(CrashLogPath(), std::ios::app);
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
    SetUnhandledExceptionFilter(CrashFilter);
    {
        std::ofstream f(CrashLogPath(), std::ios::app);
        if (f)
            f << "main enter\n";
    }
    auto source = ref new Direct3DApplicationSource();
    CoreApplication::Run(source);
    {
        std::ofstream f(CrashLogPath(), std::ios::app);
        if (f)
            f << "main exit after Run\n";
    }
    return 0;
}
