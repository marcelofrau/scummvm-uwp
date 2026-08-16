#include "pch.h"
#include "App.h"
#include <Windows.Storage.h>
#include <fstream>
#include <windows.h>
#include <intrin.h>

using namespace scummvm_uwp;

static std::wstring g_crashLogPath;

static void WriteDebugMarker(const wchar_t* msg)
{
    try
    {
        std::wstring p = Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data();
        std::ofstream f(p + L"\\crash.log", std::ios::app);
        if (f)
            f << "[" << std::string(msg, msg + wcslen(msg)) << "]\n";
        f << "[thread id " << std::hex << GetCurrentThreadId() << "]\n";
    }
    catch (...) {}
}

// UCRT "invalid parameter" handler. A C-runtime parameter failure (bad fopen
// mode, memcpy_s overflow, NULL to wcstombs, ...) normally goes to watson ->
// __fastfail: a silent, uncatchable, debugger-suppressed kill. Logging it here
// catches the exact CRT call that kills the core in SW mode.
static void __cdecl InvalidParameterHandler(const wchar_t* expression,
    const wchar_t* function, const wchar_t* file, unsigned int line,
    uintptr_t reserved)
{
    try
    {
        if (!g_crashLogPath.empty())
        {
            std::wofstream f(g_crashLogPath, std::ios::app);
            if (f)
            {
                f << L"[InvalidParameter] expr=" << (expression ? expression : L"?")
                  << L" func=" << (function ? function : L"?")
                  << L" file=" << (file ? file : L"?")
                  << L" line=" << std::dec << line
                  << L" tid=0x" << std::hex << GetCurrentThreadId() << L"\n";
                f.flush();
            }
        }
    }
    catch (...) {}
}

// Process-wide first-chance exception observer. Catches exceptions raised on
// ANY thread — including inside the core's libco coroutine. Also harvests
// OutputDebugString payloads (DBG_PRINTEXCEPTION) so the core's uwp_log lines
// land in crash.log even after the process dies.
static LONG NTAPI FirstChanceExceptionHandler(PEXCEPTION_POINTERS ep)
{    static volatile LONG s_count = 0;
    LONG n = InterlockedIncrement(&s_count);
    if (n <= 300 && !g_crashLogPath.empty())
    {
        try
        {
            DWORD code = ep->ExceptionRecord->ExceptionCode;
            ULONG_PTR addr = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
            std::wofstream f(g_crashLogPath, std::ios::app);
            if (f)
            {
                if (code == 0x40010006 || code == 0x40010007 || code == 0x4001000A)
                {
                    // OutputDebugStringA/W payload — captured by the file log
                    // already (LogHelper + uwp_log redirection); do not read the
                    // exception payload here (its layout differs per API).
                }
                else
                {
                    f << L"[VectoredException code=0x" << std::hex << code
                      << L" addr=0x" << std::hex << addr
                      << L" tid=0x" << std::hex << GetCurrentThreadId() << L"]\n";

                    HMODULE hMod = nullptr;
                    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)addr, &hMod) && hMod)
                    {
                        wchar_t modName[260] = L"?";
                        if (GetModuleFileNameW(hMod, modName, 260) > 0)
                        {
                            wchar_t* slash = wcsrchr(modName, L'\\');
                            f << L"[module] " << (slash ? slash + 1 : modName)
                              << L" +0x" << std::hex << (ULONG_PTR)(addr - (ULONG_PTR)hMod) << L"\n";
                        }
                    }

                    void* frames[16] = {};
                    USHORT n = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
                    for (USHORT i = 0; i < n; i++)
                    {
                        ULONG_PTR fa = (ULONG_PTR)frames[i];
                        HMODULE m = nullptr;
                        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)fa, &m) && m)
                        {
                            wchar_t mn[260] = L"?";
                            if (GetModuleFileNameW(m, mn, 260) > 0)
                            {
                                wchar_t* slash = wcsrchr(mn, L'\\');
                                f << L"  #" << std::dec << i << L" "
                                  << (slash ? slash + 1 : mn)
                                  << L"+0x" << std::hex << (ULONG_PTR)(fa - (ULONG_PTR)m) << L"\n";
                            }
                        }
                        else
                        {
                            f << L"  #" << std::dec << i << L" 0x" << std::hex << fa << L"\n";
                        }
                    }
                }
            }
        }
        catch (...) {}
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- ExitProcess IAT hook ---------------------------------------------------
// The core dies with a CLEAN exit() (no exception, no WER). This AppContainer
// blocks executable-memory allocation AND W->X page transitions (ACG/CFG), so
// an inline detour is impossible. Instead rewrite the ExitProcess slot in each
// loaded module's import table (a data page) so the core's exit() path lands in
// HookedExitProcess, which captures the calling stack first.
typedef void(WINAPI* ExitProcessFn)(UINT uExitCode);
typedef BOOL(WINAPI* TerminateProcessFn)(HANDLE hProcess, UINT uExitCode);
static ExitProcessFn s_origExitProcess = nullptr;
static TerminateProcessFn s_origTerminateProcess = nullptr;

static void WriteStackToCrashLog(const wchar_t* what, UINT code)
{
    try
    {
        if (!g_crashLogPath.empty())
        {
            std::wofstream f(g_crashLogPath, std::ios::app);
            if (f)
            {
                f << L"[" << what << L"] code=" << std::dec << code
                  << L" tid=0x" << std::hex << GetCurrentThreadId() << L"\n";
                void* frames[24] = {};
                USHORT n = RtlCaptureStackBackTrace(3, 24, frames, nullptr);
                for (USHORT i = 0; i < n; i++)
                {
                    ULONG_PTR fa = (ULONG_PTR)frames[i];
                    HMODULE m = nullptr;
                    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCWSTR)fa, &m) && m)
                    {
                        wchar_t mn[260] = L"?";
                        if (GetModuleFileNameW(m, mn, 260) > 0)
                        {
                            wchar_t* slash = wcsrchr(mn, L'\\');
                            f << L"  #" << std::dec << i << L" "
                              << (slash ? slash + 1 : mn)
                              << L"+0x" << std::hex << (ULONG_PTR)(fa - (ULONG_PTR)m) << L"\n";
                        }
                    }
                    else
                    {
                        f << L"  #" << std::dec << i << L" 0x" << std::hex << fa << L"\n";
                    }
                }
                f.flush();
            }
        }
    }
    catch (...) {}
}

static void NTAPI HookedExitProcess(UINT uExitCode)
{
    WriteStackToCrashLog(L"ExitProcess", uExitCode);
    if (s_origExitProcess)
        s_origExitProcess(uExitCode);
}

static BOOL WINAPI HookedTerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    WriteStackToCrashLog(L"TerminateProcess", uExitCode);
    if (s_origTerminateProcess)
        return s_origTerminateProcess(hProcess, uExitCode);
    return FALSE;
}

static void PatchModuleIatByName(HMODULE h, const char* fname, void* hook)
{
    BYTE* base = (BYTE*)h;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    DWORD impRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!impRva)
        return;
    PIMAGE_IMPORT_DESCRIPTOR desc = (PIMAGE_IMPORT_DESCRIPTOR)(base + impRva);
    for (; desc->Name; desc++)
    {
        const char* dllName = (const char*)(base + desc->Name);
        if (_stricmp(dllName, "KERNEL32.dll") != 0 && _stricmp(dllName, "KERNELBASE.dll") != 0)
            continue;
        PIMAGE_THUNK_DATA ithunk = (PIMAGE_THUNK_DATA)(base + desc->FirstThunk);
        if (!ithunk)
            continue;
        PIMAGE_THUNK_DATA othunk = desc->OriginalFirstThunk
            ? (PIMAGE_THUNK_DATA)(base + desc->OriginalFirstThunk) : nullptr;
        for (int j = 0; ithunk[j].u1.Function; j++)
        {
            bool match = false;
            if (othunk && !(othunk[j].u1.Ordinal & IMAGE_ORDINAL_FLAG))
            {
                PIMAGE_IMPORT_BY_NAME iname = (PIMAGE_IMPORT_BY_NAME)(base + othunk[j].u1.AddressOfData);
                if (iname && strcmp((const char*)iname->Name, fname) == 0)
                    match = true;
            }
            else if (ithunk[j].u1.Function == (ULONG_PTR)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), fname))
            {
                match = true; // resolved by ordinal to the same function
            }
            if (match)
            {
                DWORD old;
                if (VirtualProtect(&ithunk[j], sizeof(ULONG_PTR), PAGE_READWRITE, &old))
                {
                    ithunk[j].u1.Function = (ULONG_PTR)hook;
                    VirtualProtect(&ithunk[j], sizeof(ULONG_PTR), old, &old);
                }
            }
        }
    }
}

void PatchExitProcessImports()
{
    HMODULE hk = GetModuleHandleW(L"kernel32.dll");
    if (!hk)
        return;
    if (!s_origExitProcess)
    {
        s_origExitProcess = (ExitProcessFn)GetProcAddress(hk, "ExitProcess");
        s_origTerminateProcess = (TerminateProcessFn)GetProcAddress(hk, "TerminateProcess");
        WriteDebugMarker(L"ExitProcess/TerminateProcess IAT hook ready");
    }
    // Patch the known modules that can reach ExitProcess: the shared CRT and
    // the dynamically-loaded core. (No PEB walk — explicit list is sufficient
    // and avoids fragile TEB/PEB offsets.)
    static const wchar_t* modules[] = {
        L"ucrtbase.dll", L"vcruntime140.dll", L"vcruntime140_1.dll",
        L"msvcp140.dll", L"msvcp140_1.dll", L"msvcp140_2.dll",
        L"scummvm_libretro.dll",
    };
    for (int i = 0; i < ARRAYSIZE(modules); i++)
    {
        HMODULE m = GetModuleHandleW(modules[i]);
        if (m)
        {
            PatchModuleIatByName(m, "ExitProcess", (void*)&HookedExitProcess);
            PatchModuleIatByName(m, "TerminateProcess", (void*)&HookedTerminateProcess);
        }
    }
}

using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::System;
using namespace Windows::UI::Core;
using namespace Platform;

App::App() :
    m_windowClosed(false),
    m_windowVisible(true),
    m_emulationPaused(false)
{
}

void App::Initialize(CoreApplicationView^ applicationView)
{
    std::wstring localFolder = Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data();
    g_crashLogPath = localFolder + L"\\crash.log";
    WriteDebugMarker(L"App::Initialize done");
    {
        std::ofstream m(localFolder + L"\\init-marker.txt", std::ios::app);
        if (m)
            m << "Initialize reached; LocalFolder=" << std::string(localFolder.begin(), localFolder.end()) << "\n";
    }
    LogInit((localFolder + L"\\scummvm-frontend.log").c_str());
    applicationView->Activated += ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &App::OnActivated);
    CoreApplication::Suspending += ref new EventHandler<SuspendingEventArgs^>(this, &App::OnSuspending);
    CoreApplication::Resuming += ref new EventHandler<Object^>(this, &App::OnResuming);

    m_deviceResources = std::make_shared<DX::DeviceResources>();
    WriteDebugMarker(L"App::Initialize done");
    typedef LONG(NTAPI* PvectHandlerFn)(PEXCEPTION_POINTERS);
    typedef LONG(NTAPI* PaddVectoredFn)(ULONG, PvectHandlerFn);
    PaddVectoredFn fn = (PaddVectoredFn)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "AddVectoredExceptionHandler");
    if (fn != nullptr)
        fn(1, FirstChanceExceptionHandler);
    _set_invalid_parameter_handler(InvalidParameterHandler);
    _set_thread_local_invalid_parameter_handler(InvalidParameterHandler);
    PatchExitProcessImports();
}

void App::SetWindow(CoreWindow^ window)
{
    WriteDebugMarker(L"App::SetWindow");
    window->SizeChanged += ref new TypedEventHandler<CoreWindow^, WindowSizeChangedEventArgs^>(this, &App::OnWindowSizeChanged);
    window->VisibilityChanged += ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &App::OnVisibilityChanged);
    window->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyDown);
    window->KeyUp += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyUp);
    window->Closed += ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &App::OnWindowClosed);
    window->Activated += ref new TypedEventHandler<CoreWindow^, WindowActivatedEventArgs^>(this, &App::OnWindowActivated);

    m_deviceResources->SetWindow(window);
}

void App::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
{
    spdlog::info("[scummvm-uwp] CoreWindow Closed");
    m_windowClosed = true;
}

void App::OnWindowActivated(CoreWindow^ sender, WindowActivatedEventArgs^ args)
{
    spdlog::info("[scummvm-uwp] CoreWindow activated state={}", (int)args->WindowActivationState);
}

void App::Load(Platform::String^ entryPoint)
{
    WriteDebugMarker(L"App::Load");
    if (!m_main)
    {
        m_main = std::make_unique<ScummVMMain>(m_deviceResources);
    }
    WriteDebugMarker(L"App::Load boot done");
}

void App::Run()
{
    WriteDebugMarker(L"App::Run");
    spdlog::info("[scummvm-uwp] App::Run enter (visible={})", (int)m_windowVisible);

    QueryPerformanceFrequency(&m_perfFrequency);
    QueryPerformanceCounter(&m_lastFrameTime);

    int loopCount = 0;
    while (!m_windowClosed)
    {
        if (m_windowVisible)
        {
            try
            {
                if (loopCount == 0) WriteDebugMarker(L"Run: loop0 before ProcessEvents");
                CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                if (loopCount == 0) WriteDebugMarker(L"Run: loop0 ProcessEvents done");

                m_main->Update();
                if (loopCount == 0) WriteDebugMarker(L"Run: loop0 Update done");

                if (m_main->Render())
                {
                    m_deviceResources->Present(m_deviceResources->GetSyncInterval(), 0);
                    if (loopCount == 0) WriteDebugMarker(L"Run: loop0 Present done");
                    if (loopCount % 120 == 1 && loopCount < 600)
                        WriteDebugMarker(L"Run: heartbeat");
                }
                loopCount++;

                // UI thread pacing. The emulation thread is paced by the blocking
                // audio Write() on its own thread — the UI must NOT call retro_run.
                if (m_deviceResources->GetSyncInterval() == 0)
                {
                    double targetMs = (m_main && m_main->IsLoaded())
                        ? (1000.0 / max(m_main->GetTargetFps(), 1.0))
                        : 16.6; // 60fps floor when nothing loaded
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);
                    double elapsedMs = (double)(now.QuadPart - m_lastFrameTime.QuadPart) * 1000.0 / m_perfFrequency.QuadPart;
                    m_lastFrameTime = now;
                    double remain = targetMs - elapsedMs;
                    if (remain > 0.0)
                        Sleep((DWORD)ceil(remain));
                    else
                        Sleep(1); // always yield — avoid 100% spin on skipped frames
                }
            }
            catch (Platform::Exception^ e)
            {
                spdlog::error("[scummvm-uwp] Run EXCEPTION hr=0x{:08X}", (unsigned)e->HResult);
                throw;
            }
            catch (const std::exception& e)
            {
                spdlog::error("[scummvm-uwp] Run std::exception: {}", e.what());
                throw;
            }
            catch (...)
            {
                spdlog::error("[scummvm-uwp] Run unknown exception");
                throw;
            }
        }
        else
        {
            CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        }
    }
    spdlog::info("[scummvm-uwp] App::Run exit (window closed)");
}

void App::Uninitialize()
{
    if (m_main)
    {
        m_main->Shutdown();
    }
}

void App::OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args)
{
    // Run() won't start until the CoreWindow is activated (dosbox pattern).
    WriteDebugMarker(L"App::OnActivated enter");
    CoreWindow::GetForCurrentThread()->Activate();
    WriteDebugMarker(L"App::OnActivated exit");
}

void App::OnSuspending(Object^ sender, SuspendingEventArgs^ args)
{
    if (m_main)
    {
        m_main->PauseEmulation();
    }
}

void App::OnResuming(Object^ sender, Object^ args)
{
    if (m_main)
    {
        m_main->ResumeEmulation();
    }
}

void App::OnWindowSizeChanged(CoreWindow^ sender, WindowSizeChangedEventArgs^ args)
{
    if (m_main)
    {
        m_main->CreateWindowSizeDependentResources();
    }
}

void App::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
    m_windowVisible = args->Visible;
    if (m_main)
    {
        if (m_windowVisible)
        {
            m_main->ResumeEmulation();
        }
        else
        {
            m_main->PauseEmulation();
        }
    }
}

void App::OnKeyDown(CoreWindow^ sender, KeyEventArgs^ args)
{
    if (m_main)
    {
        m_main->OnKeyEvent(args->VirtualKey, true, (uint32_t)args->KeyStatus.ScanCode, (args->KeyStatus.IsExtendedKey != 0));
        args->Handled = true;
    }
}

void App::OnKeyUp(CoreWindow^ sender, KeyEventArgs^ args)
{
    if (m_main)
    {
        m_main->OnKeyEvent(args->VirtualKey, false, (uint32_t)args->KeyStatus.ScanCode, (args->KeyStatus.IsExtendedKey != 0));
        args->Handled = true;
    }
}
