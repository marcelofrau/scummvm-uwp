#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include <future>
#include <functional>
#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\SdlInput.h"
#include "Content\RetroCore.h"
#include "Content\RetroD3D11Renderer.h"
#include "Content\XAudio2Output.h"

// Minimal EGL types — avoids pulling in full Khronos headers.
// Loaded dynamically from libEGL.dll at runtime.
typedef int EGLBoolean;
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef void* EGLNativeWindowType;

#define EGL_NO_DISPLAY nullptr
#define EGL_NO_SURFACE nullptr
#define EGL_NO_CONTEXT nullptr
#define EGL_DEFAULT_DISPLAY nullptr

// EGL constants
#define EGL_SUCCESS            0x3000
#define EGL_OPENGL_API         0x30A2
#define EGL_OPENGL_BIT         0x0008
#define EGL_RENDERABLE_TYPE    0x3040
#define EGL_SURFACE_TYPE       0x3033
#define EGL_WINDOW_BIT         0x0004
#define EGL_BLUE_SIZE          0x3022
#define EGL_GREEN_SIZE         0x3023
#define EGL_RED_SIZE           0x3024
#define EGL_ALPHA_SIZE         0x3021
#define EGL_DEPTH_SIZE         0x3025
#define EGL_NONE               0x3038
#define EGL_RENDER_BUFFER      0x3086
#define EGL_BACK_BUFFER        0x3084
#define EGL_EXTENSIONS         0x3055
#define EGL_VERSION            0x3054

namespace scummvm_uwp
{
    class ScummVMMain : public DX::IDeviceNotify
    {
    public:
        ScummVMMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
        ~ScummVMMain();

        void CreateWindowSizeDependentResources();
        void CreatePresentationResources();
        void Update();
        bool Render();
        void OnKeyEvent(Windows::System::VirtualKey key, bool down, uint32_t scanCode = 0, bool isExtended = false);

        // IDeviceNotify
        void OnDeviceLost() override;
        void OnDeviceRestored() override;

        void PauseEmulation();
        void ResumeEmulation();
        void Shutdown();
        bool IsLoaded() const { return m_retroCore && m_retroCore->IsLoaded(); }
        double GetTargetFps() const { return m_retroCore ? m_retroCore->GetTargetFps() : 60.0; }

        // GL mode: set by env handler when core requests SET_HW_RENDER
        bool m_useGL = false;
        void SetGLMode(bool gl) { m_useGL = gl; }
        bool CreateGLContext();
        void DestroyGLContext();
        void PresentGLFrame();

    private:
        bool BootCore();
        void UpdateRetroPad();

        std::shared_ptr<DX::DeviceResources> m_deviceResources;
        std::unique_ptr<SdlInput> m_sdlInput;
        std::unique_ptr<RetroCore> m_retroCore;
        std::unique_ptr<RetroD3D11Renderer> m_retroD3D11;
        std::unique_ptr<XAudio2Output> m_xaudio2;
        DX::StepTimer m_timer;
        DirectX::XMVECTORF32 m_clearColor;
        std::future<bool> m_bootFuture;
        bool m_bootStarted = false;
        bool m_retroRunning = false;
        bool m_paused = false;
        bool m_bootFailed = false;
        bool m_glInitialized = false;

        // Mesa EGL GL context (replaces WGL+SDL)
        HMODULE m_eglLib = nullptr;  // libEGL.dll handle
        bool m_frameReadyGL = false;

        EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
        EGLContext m_eglContext = EGL_NO_CONTEXT;
        EGLSurface m_eglSurface = EGL_NO_SURFACE;
        EGLConfig m_eglConfig = nullptr;

        // EGL function pointers — loaded dynamically from libEGL.dll
        using PFN_EGL_GET_PROC_ADDRESS = void* (*)(const char*);
        using PFN_EGL_GET_DISPLAY = EGLDisplay (*)(EGLNativeWindowType);
        using PFN_EGL_INITIALIZE = EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*);
        using PFN_EGL_CHOOSE_CONFIG = EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
        using PFN_EGL_BIND_API = EGLBoolean (*)(EGLenum);
        using PFN_EGL_CREATE_CONTEXT = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
        using PFN_EGL_CREATE_WINDOW_SURFACE = EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
        using PFN_EGL_MAKE_CURRENT = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
        using PFN_EGL_SWAP_BUFFERS = EGLBoolean (*)(EGLDisplay, EGLSurface);
        using PFN_EGL_DESTROY_SURFACE = EGLBoolean (*)(EGLDisplay, EGLSurface);
        using PFN_EGL_DESTROY_CONTEXT = EGLBoolean (*)(EGLDisplay, EGLContext);
        using PFN_EGL_TERMINATE = EGLBoolean (*)(EGLDisplay);
        using PFN_EGL_QUERY_STRING = const char* (*)(EGLDisplay, EGLint);
        using PFN_EGL_GET_ERROR = EGLint (*)(void);

        PFN_EGL_GET_PROC_ADDRESS      m_eglGetProcAddress = nullptr;
        PFN_EGL_GET_DISPLAY           m_eglGetDisplay = nullptr;
        PFN_EGL_INITIALIZE            m_eglInitialize = nullptr;
        PFN_EGL_CHOOSE_CONFIG         m_eglChooseConfig = nullptr;
        PFN_EGL_BIND_API              m_eglBindAPI = nullptr;
        PFN_EGL_CREATE_CONTEXT        m_eglCreateContext = nullptr;
        PFN_EGL_CREATE_WINDOW_SURFACE m_eglCreateWindowSurface = nullptr;
        PFN_EGL_MAKE_CURRENT          m_eglMakeCurrent = nullptr;
        PFN_EGL_SWAP_BUFFERS          m_eglSwapBuffers = nullptr;
        PFN_EGL_DESTROY_SURFACE       m_eglDestroySurface = nullptr;
        PFN_EGL_DESTROY_CONTEXT       m_eglDestroyContext = nullptr;
        PFN_EGL_TERMINATE             m_eglTerminate = nullptr;
        PFN_EGL_QUERY_STRING          m_eglQueryString = nullptr;
        PFN_EGL_GET_ERROR             m_eglGetError = nullptr;
    };
}
