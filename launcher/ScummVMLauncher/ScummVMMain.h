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

// SDL2 for GL context management — Xbox UWP path via opengl32.dll (Mesa WGL)
#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_video.h"

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
        bool InitSDLForGL();

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
        bool m_d3d11ReleasedForGL = false;
        std::atomic<bool> m_d3d11ReleaseRequested{ false };

        // SDL2 GL context (replaces EGL) — Xbox UWP path via opengl32.dll + libgallium_wgl.dll
        bool m_frameReadyGL = false;
        SDL_Window* m_sdlWindow = nullptr;
        SDL_GLContext m_sdlGLContext = nullptr;
        bool m_sdlInitialized = false;
        bool m_sdlWindowReady = false;
    };
}
