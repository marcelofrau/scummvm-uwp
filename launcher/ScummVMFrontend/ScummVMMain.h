#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\SdlInput.h"
#include "Content\RetroCore.h"
#include "Content\RetroD3D11Renderer.h"
#include "Content\XAudio2Output.h"

namespace scummvm_uwp
{
    // Frontend main class: owns the libretro core (dynamically loaded), the
    // D3D11 video path, the XAudio2 audio path and the UWP gamepad input.
    // Boot sequence (called from App::OnLaunched):
    //   Bootstrap::Run() -> CoreDll::Load() -> RetroCore::Init() ->
    //   RetroCore::LoadGame(L"", {}) which boots the ScummVM GUI (no-game).
    class ScummVMMain : public DX::IDeviceNotify
    {
    public:
        ScummVMMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
        ~ScummVMMain();

        void CreateWindowSizeDependentResources();
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

    private:
        void EnsureBoot();
        void BootCore();
        void UpdateRetroPad();

        std::shared_ptr<DX::DeviceResources> m_deviceResources;
        std::unique_ptr<SdlInput> m_sdlInput;
        std::unique_ptr<RetroCore> m_retroCore;
        std::unique_ptr<RetroD3D11Renderer> m_retroD3D11;
        std::unique_ptr<XAudio2Output> m_xaudio2;
        DX::StepTimer m_timer;
        DirectX::XMVECTORF32 m_clearColor;
        LARGE_INTEGER m_bootFreq;
        bool m_bootStarted = false;
        bool m_retroRunning = false;
        bool m_paused = false;
        bool m_bootFailed = false;
    };
}
