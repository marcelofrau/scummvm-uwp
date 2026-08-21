#include "pch.h"
#include "ScummVMMain.h"
#include "CoreDll.h"
#include "Bootstrap.h"
#include "DataPaths.h"

#include <Windows.ApplicationModel.h>
#include <Windows.UI.Core.h>
#include <windows.h>

using namespace scummvm_uwp;
using namespace Windows::ApplicationModel;
using namespace Windows::System;
using namespace Windows::UI::Core;

void PatchExitProcessImports(); // App.cpp

static std::wstring InstalledLocationDir()
{
    return std::wstring(Package::Current->InstalledLocation->Path->Data());
}

ScummVMMain::ScummVMMain(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
    , m_clearColor(DirectX::Colors::Black)
{
    m_sdlInput = std::make_unique<SdlInput>();
    m_sdlInput->Initialize();

    m_retroD3D11 = std::make_unique<RetroD3D11Renderer>(deviceResources);
    m_deviceResources->RegisterDeviceNotify(this);

    BootTrace(L"renderer init begin");
    CreateWindowSizeDependentResources();
    BootTrace(L"renderer init done");
}

ScummVMMain::~ScummVMMain()
{
    // If boot is still running on background thread, wait for it to finish
    // before destroying members that the boot thread may be writing to.
    if (m_bootFuture.valid())
        m_bootFuture.wait();

    m_deviceResources->RegisterDeviceNotify(nullptr);
    if (m_retroCore)
    {
        m_retroCore->Shutdown();
        m_retroCore.reset();
    }
    m_xaudio2.reset();
    CoreDll::Unload();
}

bool ScummVMMain::BootCore()
{
    spdlog::info("[scummvm-uwp] --- boot (async) ---");
    BootTrace(L"boot: begin");

    Bootstrap::Run();
    BootTrace(L"boot: bootstrap done");

    // DIAGNOSTIC: nocore.txt in LocalState skips core init/load entirely —
    // isolates "core kills the process" vs "environment kills the app".
    if (Bootstrap::FileExistsInLocalState(L"nocore.txt"))
    {
        spdlog::warn("[scummvm-uwp] nocore.txt present — skipping core init/load (diagnostic)");
        BootTrace(L"boot: nocore.txt — core skipped");
        m_retroCore = std::make_unique<RetroCore>();
        m_retroRunning = true;
        return true;
    }

    m_xaudio2 = std::make_unique<XAudio2Output>();
    if (!m_xaudio2->Initialize())
    {
        spdlog::error("[scummvm-uwp] XAudio2 init FAILED");
        BootTrace(L"boot: XAudio2 init FAILED");
        return false;
    }
    BootTrace(L"boot: XAudio2 init ok");

    // DIAGNOSTIC: nload.txt skips LoadGame (InitCore runs) — isolates which
    // core stage kills the process.
    if (Bootstrap::FileExistsInLocalState(L"nload.txt"))
    {
        spdlog::warn("[scummvm-uwp] nload.txt present — skipping LoadGame (diagnostic)");
        BootTrace(L"boot: nload.txt — LoadGame skipped");
        m_retroRunning = true;
        return true;
    }

    // No-game boot: the ScummVM core opens its own GUI.
    std::wstring corePath = InstalledLocationDir() + L"\\cores\\scummvm_libretro.dll";
    BootTrace(L"boot: core load begin");
    if (!CoreDll::Load(corePath.c_str()))
    {
        DWORD gle = GetLastError();
        spdlog::error("[scummvm-uwp] core load FAILED: {} gle={:08X}", corePath, gle);
        BootTrace(L"boot: core load FAILED");
        return false;
    }
    BootTrace(L"boot: core loaded");
    PatchExitProcessImports();
    BootTrace(L"boot: IAT hooks patched");

    // Create GL context BEFORE emu thread starts, so SET_HW_RENDER has
    // working callbacks when the core requests them during retro_init.
    spdlog::info("[scummvm-uwp] boot: attempting GL context creation (pre-emu)");
    BootTrace(L"boot: GL context begin");
    if (CreateGLContext())
    {
        // Wire GL callbacks through RetroCore
        RetroCore::SetGLProcFunc(m_wglGetProcAddress);
        RetroCore::s_glContextReady.store(true);
        m_useGL = true;
        spdlog::info("[scummvm-uwp] boot: Mesa WGL context ready — GL mode enabled");
        BootTrace(L"boot: GL context OK — GL mode enabled");
    }
    else
    {
        spdlog::warn("[scummvm-uwp] boot: GL context FAILED — software mode fallback");
        BootTrace(L"boot: GL context FAILED — SW fallback");
        m_useGL = false;
    }

    RetroCore::SetAudioOutput(m_xaudio2.get());

    m_retroCore = std::make_unique<RetroCore>();
    if (!m_retroCore->Init())
    {
        spdlog::error("[scummvm-uwp] RetroCore::Init FAILED");
        BootTrace(L"boot: emu thread FAILED");
        return false;
    }
    BootTrace(L"boot: emu thread started");

    // No-game boot: the ScummVM core opens its own GUI.
    m_retroCore->LoadGame(L"", {});
    BootTrace(L"boot: load_game enqueued");
    m_retroRunning = true;
    spdlog::info("[scummvm-uwp] core booted — ScummVM GUI expected");
    return true;
}

void ScummVMMain::CreateWindowSizeDependentResources()
{
    m_retroD3D11->CreateDeviceDependentResources();
}

void ScummVMMain::Update()
{
    // Launch async boot on first Update — AFTER SetWindow/Run so D3D11 swap
    // chain exists. Xbox kills process if no Present() within ~250ms.
    if (!m_bootStarted)
    {
        m_bootStarted = true;
        m_bootFuture = std::async(std::launch::async, [this]() -> bool
        {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            bool ok = BootCore();
            CoUninitialize();
            return ok;
        });
        BootTrace(L"boot: async boot launched");
    }

    // Check async boot completion (non-blocking).
    if (m_bootStarted && !m_retroRunning && !m_bootFailed)
    {
        if (m_bootFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            bool ok = m_bootFuture.get();
            m_bootFailed = !ok;
            if (ok)
            {
                spdlog::info("[scummvm-uwp] async boot completed OK");
            }
            else
                spdlog::error("[scummvm-uwp] async boot FAILED");
        }
    }

    static int step = 0;
    step++;
    if ((step % 300) == 0)
        spdlog::info("[scummvm-uwp] Update step {}", step);

    static bool firstUpdate = true;
    if (firstUpdate)
    {
        firstUpdate = false;
        spdlog::info("[scummvm-uwp] Update() first call");
    }

    m_timer.Tick([&] { });

    if (m_paused || m_bootFailed || !m_retroCore)
        return;

    if (m_retroCore->IsShutdownRequested())
    {
        spdlog::info("[scummvm-uwp] core requested shutdown → exiting");
        Windows::ApplicationModel::Core::CoreApplication::Exit();
        return;
    }

    m_sdlInput->PollEvents();
    UpdateRetroPad();

    // Present the newest frame from the core (non-blocking).
    RetroCore::FrameView frame;
    if (RetroCore::AcquireFrame(frame))
    {
        m_retroD3D11->UpdateVideoFrame(frame.data, frame.w, frame.h, frame.pitch);
        RetroCore::ReleaseFrame();
    }
}

void ScummVMMain::UpdateRetroPad()
{
    struct { int sdl; unsigned retro; } map[] = {
        { BUTTON_A, RETRO_DEVICE_ID_JOYPAD_A },
        { BUTTON_B, RETRO_DEVICE_ID_JOYPAD_B },
        { BUTTON_X, RETRO_DEVICE_ID_JOYPAD_X },
        { BUTTON_Y, RETRO_DEVICE_ID_JOYPAD_Y },
        { BUTTON_L, RETRO_DEVICE_ID_JOYPAD_L },
        { BUTTON_R, RETRO_DEVICE_ID_JOYPAD_R },
        { BUTTON_L2, RETRO_DEVICE_ID_JOYPAD_L2 },
        { BUTTON_R2, RETRO_DEVICE_ID_JOYPAD_R2 },
        { BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START },
        { BUTTON_SELECT, RETRO_DEVICE_ID_JOYPAD_SELECT },
        { BUTTON_L3, RETRO_DEVICE_ID_JOYPAD_L3 },
        { BUTTON_R3, RETRO_DEVICE_ID_JOYPAD_R3 },
        { BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP },
        { BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN },
        { BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT },
        { BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
    };
    for (auto& m : map)
        RetroCore::SetJoypadButton(m.retro, m_sdlInput->IsButtonHeld(m.sdl));

    float lx, ly, rx, ry;
    m_sdlInput->GetLeftStick(lx, ly);
    m_sdlInput->GetRightStick(rx, ry);
    RetroCore::SetAnalogAxis(0, RETRO_DEVICE_ID_ANALOG_X, (int16_t)(lx * 32767.0f));
    RetroCore::SetAnalogAxis(0, RETRO_DEVICE_ID_ANALOG_Y, (int16_t)(-ly * 32767.0f));
    RetroCore::SetAnalogAxis(1, RETRO_DEVICE_ID_ANALOG_X, (int16_t)(rx * 32767.0f));
    RetroCore::SetAnalogAxis(1, RETRO_DEVICE_ID_ANALOG_Y, (int16_t)(-ry * 32767.0f));
}

bool ScummVMMain::Render()
{
    static bool firstRender = true;
    if (firstRender)
    {
        firstRender = false;
        spdlog::info("[scummvm-uwp] Render() first call");
    }

    // Bind the swap-chain back buffer as the render target (dosbox pattern).
    auto context = m_deviceResources->GetD3DDeviceContext();
    ID3D11RenderTargetView* targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
    context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());
    context->ClearRenderTargetView(targets[0], DirectX::Colors::Black);
    context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_retroD3D11->Render();
    return true;
}

void ScummVMMain::OnKeyEvent(VirtualKey key, bool down, uint32_t scanCode, bool isExtended)
{
    if (!m_retroCore || !m_retroCore->IsLoaded())
        return;

    int vk = (int)key;
    unsigned retroKey = RETROK_UNKNOWN;

    switch (vk)
    {
    case 0x10: retroKey = (scanCode == 0x36) ? RETROK_RSHIFT : RETROK_LSHIFT; break;
    case 0x11: retroKey = isExtended ? RETROK_RCTRL : RETROK_LCTRL; break;
    case 0x12: retroKey = isExtended ? RETROK_RALT : RETROK_LALT; break;

    case 0x08: retroKey = RETROK_BACKSPACE; break;
    case 0x09: retroKey = RETROK_TAB;       break;
    case 0x0C: retroKey = RETROK_CLEAR;     break;
    case 0x0D: retroKey = RETROK_RETURN;    break;
    case 0x1B: retroKey = RETROK_ESCAPE;    break;
    case 0x20: retroKey = RETROK_SPACE;     break;

    case 0x30: retroKey = RETROK_0; break; case 0x31: retroKey = RETROK_1; break;
    case 0x32: retroKey = RETROK_2; break; case 0x33: retroKey = RETROK_3; break;
    case 0x34: retroKey = RETROK_4; break; case 0x35: retroKey = RETROK_5; break;
    case 0x36: retroKey = RETROK_6; break; case 0x37: retroKey = RETROK_7; break;
    case 0x38: retroKey = RETROK_8; break; case 0x39: retroKey = RETROK_9; break;

    case 0x41: retroKey = RETROK_a; break; case 0x42: retroKey = RETROK_b; break;
    case 0x43: retroKey = RETROK_c; break; case 0x44: retroKey = RETROK_d; break;
    case 0x45: retroKey = RETROK_e; break; case 0x46: retroKey = RETROK_f; break;
    case 0x47: retroKey = RETROK_g; break; case 0x48: retroKey = RETROK_h; break;
    case 0x49: retroKey = RETROK_i; break; case 0x4A: retroKey = RETROK_j; break;
    case 0x4B: retroKey = RETROK_k; break; case 0x4C: retroKey = RETROK_l; break;
    case 0x4D: retroKey = RETROK_m; break; case 0x4E: retroKey = RETROK_n; break;
    case 0x4F: retroKey = RETROK_o; break; case 0x50: retroKey = RETROK_p; break;
    case 0x51: retroKey = RETROK_q; break; case 0x52: retroKey = RETROK_r; break;
    case 0x53: retroKey = RETROK_s; break; case 0x54: retroKey = RETROK_t; break;
    case 0x55: retroKey = RETROK_u; break; case 0x56: retroKey = RETROK_v; break;
    case 0x57: retroKey = RETROK_w; break; case 0x58: retroKey = RETROK_x; break;
    case 0x59: retroKey = RETROK_y; break; case 0x5A: retroKey = RETROK_z; break;

    case 0x21: retroKey = RETROK_PAGEUP;   break;
    case 0x22: retroKey = RETROK_PAGEDOWN; break;
    case 0x23: retroKey = RETROK_END;      break;
    case 0x24: retroKey = RETROK_HOME;     break;
    case 0x25: retroKey = RETROK_LEFT;     break;
    case 0x26: retroKey = RETROK_UP;       break;
    case 0x27: retroKey = RETROK_RIGHT;    break;
    case 0x28: retroKey = RETROK_DOWN;     break;
    case 0x2D: retroKey = RETROK_INSERT;   break;
    case 0x2E: retroKey = RETROK_DELETE;   break;
    case 0x2F: retroKey = RETROK_HELP;     break;

    case 0xA0: retroKey = RETROK_LSHIFT; break;
    case 0xA1: retroKey = RETROK_RSHIFT; break;
    case 0xA2: retroKey = RETROK_LCTRL;  break;
    case 0xA3: retroKey = RETROK_RCTRL;  break;
    case 0xA4: retroKey = RETROK_LALT;   break;
    case 0xA5: retroKey = RETROK_RALT;   break;

    case 0x5B: retroKey = RETROK_LSUPER; break;
    case 0x5C: retroKey = RETROK_RSUPER; break;
    case 0x5D: retroKey = RETROK_MENU;   break;

    case 0x60: retroKey = RETROK_KP0; break; case 0x61: retroKey = RETROK_KP1; break;
    case 0x62: retroKey = RETROK_KP2; break; case 0x63: retroKey = RETROK_KP3; break;
    case 0x64: retroKey = RETROK_KP4; break; case 0x65: retroKey = RETROK_KP5; break;
    case 0x66: retroKey = RETROK_KP6; break; case 0x67: retroKey = RETROK_KP7; break;
    case 0x68: retroKey = RETROK_KP8; break; case 0x69: retroKey = RETROK_KP9; break;
    case 0x6A: retroKey = RETROK_KP_MULTIPLY; break;
    case 0x6B: retroKey = RETROK_KP_PLUS; break;
    case 0x6D: retroKey = RETROK_KP_MINUS; break;
    case 0x6E: retroKey = RETROK_KP_PERIOD; break;
    case 0x6F: retroKey = RETROK_KP_DIVIDE; break;
    case 0x6C: retroKey = RETROK_KP_ENTER; break;

    case 0x70: retroKey = RETROK_F1; break;  case 0x71: retroKey = RETROK_F2; break;
    case 0x72: retroKey = RETROK_F3; break;  case 0x73: retroKey = RETROK_F4; break;
    case 0x74: retroKey = RETROK_F5; break;  case 0x75: retroKey = RETROK_F6; break;
    case 0x76: retroKey = RETROK_F7; break;  case 0x77: retroKey = RETROK_F8; break;
    case 0x78: retroKey = RETROK_F9; break;  case 0x79: retroKey = RETROK_F10; break;
    case 0x7A: retroKey = RETROK_F11; break; case 0x7B: retroKey = RETROK_F12; break;

    case 0x13: retroKey = RETROK_PAUSE;     break;
    case 0x14: retroKey = RETROK_CAPSLOCK;  break;
    case 0x90: retroKey = RETROK_NUMLOCK;   break;
    case 0x91: retroKey = RETROK_SCROLLOCK; break;
    case 0x2C: retroKey = RETROK_PRINT;     break;
    case 0x2A: retroKey = RETROK_PRINT;     break;
    case 0xB7: retroKey = RETROK_SYSREQ;    break;
    case 0x1C: retroKey = RETROK_BREAK;     break;

    case 0xBA: retroKey = RETROK_SEMICOLON;    break;
    case 0xBB: retroKey = RETROK_EQUALS;       break;
    case 0xBC: retroKey = RETROK_COMMA;        break;
    case 0xBD: retroKey = RETROK_MINUS;        break;
    case 0xBE: retroKey = RETROK_PERIOD;       break;
    case 0xBF: retroKey = RETROK_SLASH;        break;
    case 0xC0: retroKey = RETROK_BACKQUOTE;    break;
    case 0xDB: retroKey = RETROK_LEFTBRACKET;  break;
    case 0xDC: retroKey = RETROK_BACKSLASH;    break;
    case 0xDD: retroKey = RETROK_RIGHTBRACKET; break;
    case 0xDE: retroKey = RETROK_QUOTE;        break;

    default: break;
    }

    if (retroKey != RETROK_UNKNOWN)
        RetroCore::SetKeyState(retroKey, down);
}

void ScummVMMain::PauseEmulation()
{
    m_paused = true;
    if (m_retroCore)
        m_retroCore->Pause();
}

void ScummVMMain::ResumeEmulation()
{
    m_paused = false;
    if (m_retroCore)
        m_retroCore->Resume();
}

void ScummVMMain::Shutdown()
{
    if (m_retroCore)
        m_retroCore->Shutdown();
}

void ScummVMMain::OnDeviceLost()
{
    m_retroD3D11->ReleaseDeviceDependentResources();
}

void ScummVMMain::OnDeviceRestored()
{
    m_retroD3D11->CreateDeviceDependentResources();
}

// --- OpenGL mode (Mesa WGL) ---

void ScummVMMain::CreatePresentationResources()
{
    // Called after boot completes. If core negotiated HW render, switch to GL.
    if (!m_useGL || m_glInitialized)
        return;

    spdlog::info("[scummvm-uwp] CreatePresentationResources: switching to GL mode");
    BootTrace(L"boot: GL mode — creating Mesa context");

    if (!CreateGLContext())
    {
        spdlog::error("[scummvm-uwp] GL context creation FAILED — falling back to software");
        BootTrace(L"boot: GL context FAILED, fallback SW");
        m_useGL = false;
        return;
    }

    spdlog::info("[scummvm-uwp] GL context created successfully");
    BootTrace(L"boot: GL context OK");
}

bool ScummVMMain::CreateGLContext()
{
    // Mesa WGL on UWP routes through SDL2 — SDL_Init must be called first
    // Get SDL2 handle (loaded as dependency of libgallium_wgl.dll)
    HMODULE sdlLib = GetModuleHandle(L"SDL2.dll");
    if (!sdlLib)
    {
        // Try explicit load if not yet loaded as dependency
        sdlLib = LoadLibrary(L"SDL2.dll");
    }
    if (!sdlLib)
    {
        spdlog::error("[scummvm-uwp] SDL2.dll not found");
        return false;
    }

    using PFN_SDL_INIT = int (*)(unsigned int);
    using PFN_SDL_SETMAINREADY = void (*)();
    auto sdlInit = (PFN_SDL_INIT)GetProcAddress(sdlLib, "SDL_Init");
    auto sdlSetMainReady = (PFN_SDL_SETMAINREADY)GetProcAddress(sdlLib, "SDL_SetMainReady");
    if (!sdlInit)
    {
        spdlog::error("[scummvm-uwp] SDL_Init not found in SDL2.dll");
        return false;
    }

    // UWP has its own main() — tell SDL not to hijack it
    if (sdlSetMainReady)
    {
        spdlog::info("[scummvm-uwp] calling SDL_SetMainReady()");
        sdlSetMainReady();
    }

    // SDL_INIT_VIDEO = 0x20
    int initResult = sdlInit(0x20);
    spdlog::info("[scummvm-uwp] SDL_Init(SDL_INIT_VIDEO) = {}", initResult);
    BootTrace(L"boot: SDL_Init done");

    if (initResult != 0)
    {
        // Log SDL error for diagnostics
        using PFN_SDL_GETERROR = const char* (*)();
        auto sdlGetError = (PFN_SDL_GETERROR)GetProcAddress(sdlLib, "SDL_GetError");
        const char* err = sdlGetError ? sdlGetError() : "unknown";
        spdlog::error("[scummvm-uwp] SDL_Init FAILED: {} (result={})", err, initResult);
        BootTrace(L"boot: SDL_Init FAILED");
        return false;
    }

    // Load Mesa WGL forwarder
    m_glLib = LoadLibrary(L"opengl32.dll");
    if (!m_glLib)
    {
        spdlog::error("[scummvm-uwp] Failed to load opengl32.dll (gle={})", GetLastError());
        return false;
    }

    // Resolve WGL functions from the forwarder
    m_wglCreateContext = (PFNWGLCREATECONTEXT)GetProcAddress(m_glLib, "wglCreateContext");
    m_wglMakeCurrent = (PFNWGLMAKECURRENT)GetProcAddress(m_glLib, "wglMakeCurrent");
    m_wglDeleteContext = (PFNWGLDELETECONTEXT)GetProcAddress(m_glLib, "wglDeleteContext");
    m_wglSwapBuffers = (PFNWGLSWAPBUFFERS)GetProcAddress(m_glLib, "wglSwapBuffers");
    m_wglGetProcAddress = (PFNWGLGETPROCADDRESS)GetProcAddress(m_glLib, "wglGetProcAddress");

    if (!m_wglCreateContext || !m_wglMakeCurrent || !m_wglDeleteContext ||
        !m_wglSwapBuffers || !m_wglGetProcAddress)
    {
        spdlog::error("[scummvm-uwp] Failed to resolve WGL functions");
        FreeLibrary(m_glLib);
        m_glLib = nullptr;
        return false;
    }

    // Get HDC for the CoreWindow — Mesa's GDI stubs handle the rest
    // On UWP, HDC is an opaque identifier; Mesa wraps ICoreWindow internally
    // C++/CX: ^ handles store IUnknown* internally, reinterpret_cast is safe
    // for the opaque HDC parameter Mesa expects.
    Windows::UI::Core::CoreWindow^ coreWindow = m_deviceResources->GetCoreWindow();
    m_glDC = reinterpret_cast<HDC>(coreWindow);

    // Create OpenGL context via Mesa WGL
    m_glContext = m_wglCreateContext(m_glDC);
    if (!m_glContext)
    {
        DWORD err = GetLastError();
        spdlog::error("[scummvm-uwp] wglCreateContext FAILED (gle={:08X})", err);
        FreeLibrary(m_glLib);
        m_glLib = nullptr;
        return false;
    }

    if (!m_wglMakeCurrent(m_glDC, m_glContext))
    {
        DWORD err = GetLastError();
        spdlog::error("[scummvm-uwp] wglMakeCurrent FAILED (gle={:08X})", err);
        m_wglDeleteContext(m_glContext);
        m_glContext = nullptr;
        FreeLibrary(m_glLib);
        m_glLib = nullptr;
        return false;
    }

    m_glInitialized = true;
    spdlog::info("[scummvm-uwp] Mesa WGL context active");
    return true;
}

void ScummVMMain::DestroyGLContext()
{
    if (!m_glInitialized)
        return;

    if (m_glContext)
    {
        if (m_wglMakeCurrent)
            m_wglMakeCurrent(nullptr, nullptr);
        if (m_wglDeleteContext)
            m_wglDeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_glLib)
    {
        FreeLibrary(m_glLib);
        m_glLib = nullptr;
    }
    m_glInitialized = false;
    spdlog::info("[scummvm-uwp] Mesa WGL context destroyed");
}

void ScummVMMain::PresentGLFrame()
{
    if (!m_glInitialized || !m_glDC || !m_wglSwapBuffers)
        return;

    m_wglSwapBuffers(m_glDC);
    m_frameReadyGL = false;
}
