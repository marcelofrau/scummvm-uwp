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
    spdlog::info("[scummvm-uwp] boot: attempting EGL context creation (pre-emu)");
    BootTrace(L"boot: EGL context begin");
    if (CreateGLContext())
    {
        // Wire GL callbacks through RetroCore — EGL version
        // get_proc_address: use eglGetProcAddress
        static auto eglGetProc = m_eglGetProcAddress;
        RetroCore::SetGLProcFunc([](const char* name) -> void* {
            return eglGetProc(name);
        });
        // make-current: EGL on emu thread
        RetroCore::SetGLMakeCurrentFunc([this](void*, void*) -> bool {
            return m_eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext) != 0;
        });
        // Resolve GL functions for FBO blit (core renders into its own FBO, not FBO 0)
        typedef void   (APIENTRY *PFNGLGETINTEGERV)(unsigned int, int*);
        typedef void   (APIENTRY *PFNGLBINDFRAMEBUFFER)(unsigned int, unsigned int);
        typedef void   (APIENTRY *PFNGLVIEWPORT)(int, int, int, int);
        typedef void   (APIENTRY *PFNGLBLITFRAMEBUFFER)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
        auto glGetIntegerv_   = reinterpret_cast<PFNGLGETINTEGERV>(m_eglGetProcAddress("glGetIntegerv"));
        auto glBindFramebuffer_= reinterpret_cast<PFNGLBINDFRAMEBUFFER>(m_eglGetProcAddress("glBindFramebuffer"));
        auto glViewport_      = reinterpret_cast<PFNGLVIEWPORT>(m_eglGetProcAddress("glViewport"));
        auto glBlitFramebuffer_= reinterpret_cast<PFNGLBLITFRAMEBUFFER>(m_eglGetProcAddress("glBlitFramebuffer"));
        auto eglSwap = m_eglSwapBuffers;
        auto eglDpy = m_eglDisplay;
        auto eglSrf = m_eglSurface;
        spdlog::info("[scummvm-uwp] GL blit functions resolved: getIntegerv={} bindFBO={} viewport={} blitFBO={}",
            (void*)glGetIntegerv_, (void*)glBindFramebuffer_, (void*)glViewport_, (void*)glBlitFramebuffer_);
        // swap buffers: blit core's FBO → FBO 0, then eglSwapBuffers
        RetroCore::SetGLSwapBuffersFunc([eglSwap, eglDpy, eglSrf,
                                         glGetIntegerv_, glBindFramebuffer_, glViewport_, glBlitFramebuffer_]() {
            if (!eglSwap || !eglDpy || !eglSrf) return;
            if (glGetIntegerv_ && glBindFramebuffer_ && glViewport_ && glBlitFramebuffer_)
            {
                // Get core's current FBO (the one it rendered into)
                int srcFbo = 0;
                glGetIntegerv_(0x8CA6, &srcFbo); // GL_FRAMEBUFFER_BINDING = 0x8CA6
                // Bind default framebuffer (EGL surface back buffer)
                glBindFramebuffer_(0x8D40, 0); // GL_FRAMEBUFFER = 0x8D40
                // Set viewport to full window (use 1920x1080 Xbox resolution)
                glViewport_(0, 0, 1920, 1080);
                // Blit from core's FBO to default framebuffer
                if (srcFbo != 0)
                {
                    glBlitFramebuffer_(0, 0, 1920, 1080, 0, 0, 1920, 1080,
                        0x00004000, 0x00000300); // GL_COLOR_BUFFER_BIT, GL_NEAREST
                }
            }
            eglSwap(eglDpy, eglSrf);
        });
        RetroCore::s_glContextReady.store(true);
        m_useGL = true;
        // Release context from boot thread so emu thread can acquire it.
        // EGL context is thread-local — must be released from creating thread first.
        m_eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        spdlog::info("[scummvm-uwp] boot: Mesa EGL context ready -- GL mode enabled (released from boot thread)");
        BootTrace(L"boot: EGL context OK -- GL mode enabled");
    }
    else
    {
        spdlog::warn("[scummvm-uwp] boot: GL context FAILED -- software mode fallback");
        BootTrace(L"boot: GL context FAILED -- SW fallback");
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
        spdlog::info("[scummvm-uwp] Render() first call (useGL=%d)", (int)m_useGL);
    }

    if (m_useGL || m_d3d11ReleaseRequested.load() || !m_deviceResources->GetSwapChain())
    {
        // First time entering GL path: release D3D11 resources on UI thread
        // (boot thread waits for this before creating EGL surface)
        if (!m_d3d11ReleasedForGL && (m_useGL || m_d3d11ReleaseRequested.load()))
        {
            m_d3d11ReleasedForGL = true;
            m_deviceResources->DestroyDevice();
            spdlog::info("[scummvm-uwp] D3D11 device fully destroyed on UI thread for GL mode");
        }
        return false;
    }

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

// --- OpenGL mode (Mesa EGL on CoreWindow) ---
// Following RetroArch UWP pattern: gfx/drivers_context/uwp_egl_ctx.c
// EGL surface created directly on CoreWindow - no SDL, no WGL.

void ScummVMMain::CreatePresentationResources()
{
    if (!m_useGL || m_glInitialized)
        return;

    spdlog::info("[scummvm-uwp] CreatePresentationResources: switching to GL mode");
    BootTrace(L"boot: GL mode -- creating Mesa EGL context");

    if (!CreateGLContext())
    {
        spdlog::error("[scummvm-uwp] GL context creation FAILED -- falling back to software");
        BootTrace(L"boot: GL context FAILED, fallback SW");
        m_useGL = false;
        return;
    }

    spdlog::info("[scummvm-uwp] EGL context created successfully");
    BootTrace(L"boot: EGL context OK");
}

bool ScummVMMain::CreateGLContext()
{
    // Load libEGL.dll - Mesa's EGL backed by Gallium D3D12
    m_eglLib = LoadLibrary(L"libEGL.dll");
    if (!m_eglLib)
    {
        spdlog::error("[scummvm-uwp] Failed to load libEGL.dll (gle={})", GetLastError());
        return false;
    }

    // Resolve all EGL function pointers from libEGL.dll
    m_eglGetProcAddress = (PFN_EGL_GET_PROC_ADDRESS)GetProcAddress(m_eglLib, "eglGetProcAddress");
    m_eglGetDisplay = (PFN_EGL_GET_DISPLAY)GetProcAddress(m_eglLib, "eglGetDisplay");
    m_eglInitialize = (PFN_EGL_INITIALIZE)GetProcAddress(m_eglLib, "eglInitialize");
    m_eglChooseConfig = (PFN_EGL_CHOOSE_CONFIG)GetProcAddress(m_eglLib, "eglChooseConfig");
    m_eglBindAPI = (PFN_EGL_BIND_API)GetProcAddress(m_eglLib, "eglBindAPI");
    m_eglCreateContext = (PFN_EGL_CREATE_CONTEXT)GetProcAddress(m_eglLib, "eglCreateContext");
    m_eglCreateWindowSurface = (PFN_EGL_CREATE_WINDOW_SURFACE)GetProcAddress(m_eglLib, "eglCreateWindowSurface");
    m_eglMakeCurrent = (PFN_EGL_MAKE_CURRENT)GetProcAddress(m_eglLib, "eglMakeCurrent");
    m_eglSwapBuffers = (PFN_EGL_SWAP_BUFFERS)GetProcAddress(m_eglLib, "eglSwapBuffers");
    m_eglDestroySurface = (PFN_EGL_DESTROY_SURFACE)GetProcAddress(m_eglLib, "eglDestroySurface");
    m_eglDestroyContext = (PFN_EGL_DESTROY_CONTEXT)GetProcAddress(m_eglLib, "eglDestroyContext");
    m_eglTerminate = (PFN_EGL_TERMINATE)GetProcAddress(m_eglLib, "eglTerminate");
    m_eglQueryString = (PFN_EGL_QUERY_STRING)GetProcAddress(m_eglLib, "eglQueryString");
    m_eglGetError = (PFN_EGL_GET_ERROR)GetProcAddress(m_eglLib, "eglGetError");

    if (!m_eglGetProcAddress || !m_eglGetDisplay || !m_eglInitialize ||
        !m_eglChooseConfig || !m_eglBindAPI || !m_eglCreateContext ||
        !m_eglCreateWindowSurface || !m_eglMakeCurrent || !m_eglSwapBuffers ||
        !m_eglDestroySurface || !m_eglDestroyContext || !m_eglTerminate ||
        !m_eglQueryString || !m_eglGetError)
    {
        spdlog::error("[scummvm-uwp] Failed to resolve EGL function pointers from libEGL.dll");
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] EGL functions resolved from libEGL.dll");
    BootTrace(L"boot: EGL functions loaded");

    // Get EGL display via eglGetDisplay(EGL_DEFAULT_DISPLAY) — matches RetroArch Mesa path
    m_eglDisplay = m_eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (!m_eglDisplay || m_eglDisplay == EGL_NO_DISPLAY)
    {
        spdlog::error("[scummvm-uwp] eglGetDisplay FAILED (eglErr={})", m_eglGetError());
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }

    // Initialize EGL
    EGLint major = 0, minor = 0;
    if (!m_eglInitialize(m_eglDisplay, &major, &minor))
    {
        spdlog::error("[scummvm-uwp] eglInitialize FAILED (eglErr={})", m_eglGetError());
        m_eglTerminate(m_eglDisplay);
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] EGL {}.{} initialized", major, minor);
    BootTrace(L"boot: EGL initialized");

    // Query EGL extensions
    if (m_eglQueryString)
    {
        const char* exts = m_eglQueryString(m_eglDisplay, EGL_EXTENSIONS);
        if (exts)
            spdlog::info("[scummvm-uwp] EGL extensions: {}", exts);
    }

    // Bind OpenGL API (desktop GL, not GLES)
    if (!m_eglBindAPI(EGL_OPENGL_API))
    {
        spdlog::error("[scummvm-uwp] eglBindAPI(EGL_OPENGL_API) FAILED (eglErr={})", m_eglGetError());
        m_eglTerminate(m_eglDisplay);
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] eglBindAPI(EGL_OPENGL_API) OK");

    // Choose config: request OpenGL renderable, window surface, RGBA8888
    const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_NONE
    };
    EGLint numConfigs = 0;
    if (!m_eglChooseConfig(m_eglDisplay, config_attribs, &m_eglConfig, 1, &numConfigs) || numConfigs == 0)
    {
        spdlog::error("[scummvm-uwp] eglChooseConfig FAILED (eglErr={}, numConfigs={})", m_eglGetError(), numConfigs);
        m_eglTerminate(m_eglDisplay);
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] eglChooseConfig OK ({} configs)", numConfigs);

    // Create OpenGL context - request core profile
    const EGLint ctx_attribs[] = {
        0x30FD /* EGL_CONTEXT_MAJOR_VERSION */, 4,
        0x30E0 /* EGL_CONTEXT_MINOR_VERSION */, 6,
        0x30FD /* EGL_CONTEXT_OPENGL_PROFILE_MASK */, 0x00000001 /* EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT */,
        EGL_NONE
    };
    m_eglContext = m_eglCreateContext(m_eglDisplay, m_eglConfig, EGL_NO_CONTEXT, ctx_attribs);
    if (!m_eglContext)
    {
        spdlog::warn("[scummvm-uwp] eglCreateContext(4.6 core) FAILED (eglErr={}), trying compat", m_eglGetError());
        // Fallback: no version requirements
        m_eglContext = m_eglCreateContext(m_eglDisplay, m_eglConfig, EGL_NO_CONTEXT, nullptr);
    }
    if (!m_eglContext)
    {
        spdlog::error("[scummvm-uwp] eglCreateContext FAILED (eglErr={})", m_eglGetError());
        m_eglTerminate(m_eglDisplay);
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] EGL context created");

    // Create window surface on the CoreWindow - this is the key step.
    // But D3D11 must release its swap chain FIRST, otherwise Mesa D3D12 can't
    // get exclusive CoreWindow access. Signal UI thread and wait.
    spdlog::info("[scummvm-uwp] requesting D3D11 release before EGL surface...");
    m_d3d11ReleaseRequested.store(true);
    // Wait for UI thread to release D3D11 (it checks this flag in Render())
    auto waitStart = std::chrono::steady_clock::now();
    while (!m_d3d11ReleasedForGL)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto elapsed = std::chrono::steady_clock::now() - waitStart;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 3)
        {
            spdlog::error("[scummvm-uwp] D3D11 release timeout (3s) — aborting GL");
            return false;
        }
    }
    spdlog::info("[scummvm-uwp] D3D11 released — creating EGL surface");

    Windows::UI::Core::CoreWindow^ coreWindow = m_deviceResources->GetCoreWindow();
    // CoreWindow^ is a WinRT handle; get the ABI IInspectable* pointer for EGL.
    // On UWP, EGL expects the ICoreWindow* (IInspectable*).
    // C++/CX handles can reinterpret_cast to void* (like HDC).
    EGLNativeWindowType nativeWindow = reinterpret_cast<void*>(coreWindow);

    const EGLint surface_attribs[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };
    m_eglSurface = m_eglCreateWindowSurface(m_eglDisplay, m_eglConfig, nativeWindow, surface_attribs);
    if (!m_eglSurface)
    {
        spdlog::error("[scummvm-uwp] eglCreateWindowSurface FAILED (eglErr={})", m_eglGetError());
        m_eglDestroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = EGL_NO_CONTEXT;
        m_eglTerminate(m_eglDisplay);
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] EGL window surface created on CoreWindow");

    // Make current on this thread
    if (!m_eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext))
    {
        spdlog::error("[scummvm-uwp] eglMakeCurrent FAILED (eglErr={})", m_eglGetError());
        m_eglDestroySurface(m_eglDisplay, m_eglSurface);
        m_eglDestroyContext(m_eglDisplay, m_eglContext);
        m_eglSurface = EGL_NO_SURFACE;
        m_eglContext = EGL_NO_CONTEXT;
        m_eglTerminate(m_eglDisplay);
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
        return false;
    }
    spdlog::info("[scummvm-uwp] eglMakeCurrent OK");

    m_glInitialized = true;
    spdlog::info("[scummvm-uwp] Mesa EGL context active");
    BootTrace(L"boot: EGL context active");
    return true;
}

void ScummVMMain::DestroyGLContext()
{
    if (!m_glInitialized)
        return;

    if (m_eglDisplay)
    {
        if (m_eglMakeCurrent)
            m_eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_eglSurface && m_eglDestroySurface)
            m_eglDestroySurface(m_eglDisplay, m_eglSurface);
        if (m_eglContext && m_eglDestroyContext)
            m_eglDestroyContext(m_eglDisplay, m_eglContext);
        if (m_eglTerminate)
            m_eglTerminate(m_eglDisplay);
        m_eglSurface = EGL_NO_SURFACE;
        m_eglContext = EGL_NO_CONTEXT;
        m_eglDisplay = EGL_NO_DISPLAY;
    }
    if (m_eglLib)
    {
        FreeLibrary(m_eglLib);
        m_eglLib = nullptr;
    }
    m_glInitialized = false;
    spdlog::info("[scummvm-uwp] Mesa EGL context destroyed");
}

void ScummVMMain::PresentGLFrame()
{
    if (!m_glInitialized || !m_eglSurface || !m_eglSwapBuffers)
        return;

    m_eglSwapBuffers(m_eglDisplay, m_eglSurface);
    m_frameReadyGL = false;
}
