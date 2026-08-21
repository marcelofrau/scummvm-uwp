#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include <future>
#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\SdlInput.h"
#include "Content\RetroCore.h"
#include "Content\RetroD3D11Renderer.h"
#include "Content\XAudio2Output.h"

// GL typedefs — minimal, avoids pulling in full Mesa headers
typedef struct __GLsync* GLsync;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef void GLvoid;

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
        bool m_glTestDone = false;

        // Mesa WGL GL context
        HGLRC m_glContext = nullptr;
        HDC m_glDC = nullptr;
        HMODULE m_glLib = nullptr;
        bool m_frameReadyGL = false;

        // WGL function pointers
        using PFNWGLCREATECONTEXT = HGLRC(WINAPI*)(HDC);
        using PFNWGLMAKECURRENT = BOOL(WINAPI*)(HDC, HGLRC);
        using PFNWGLDELETECONTEXT = BOOL(WINAPI*)(HGLRC);
        using PFNWGLSWAPBUFFERS = BOOL(WINAPI*)(HDC);
        using PFNWGLGETPROCADDRESS = LPVOID(WINAPI*)(LPCSTR);

        PFNWGLCREATECONTEXT m_wglCreateContext = nullptr;
        PFNWGLMAKECURRENT m_wglMakeCurrent = nullptr;
        PFNWGLDELETECONTEXT m_wglDeleteContext = nullptr;
        PFNWGLSWAPBUFFERS m_wglSwapBuffers = nullptr;
        PFNWGLGETPROCADDRESS m_wglGetProcAddress = nullptr;
    };
}
