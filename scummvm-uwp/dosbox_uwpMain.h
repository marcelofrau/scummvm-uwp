#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>
#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\SdlInput.h"
#include "Content\RetroCore.h"
#include "Content\RetroScreenRenderer.h"
#include "Content\XAudio2Output.h"
#include "Content\FrontendMenu.h"

namespace dosbox_uwp
{
    class dosbox_uwpMain : public DX::IDeviceNotify
    {
    public:
        dosbox_uwpMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
        ~dosbox_uwpMain();
        void CreateWindowSizeDependentResources();
        void Update();
        bool Render();

        virtual void OnDeviceLost();
        virtual void OnDeviceRestored();
		void OnKeyEvent(Windows::System::VirtualKey key, bool down, uint32_t scanCode = 0, bool isExtended = false);
#ifdef MOUSE_SUPPORT
		void OnPointerMove(float nx, float ny, float px, float py);
		void OnPointerDown(float nx, float ny, unsigned btn);
		void OnPointerUp(unsigned btn);
		void OnPointerRelease();
		void OnPointerWheel(int delta);
		void SetMousePointerId(uint32_t id);
		void PollMouseButtons();
#endif
        void ToggleOSD();
        bool IsMenuVisible() const { return m_menu.IsVisible(); }
        FrontendMenu& GetMenu() { return m_menu; }
        void LoadRom(const std::wstring& path, std::vector<uint8_t> romData, const std::wstring& originalPath = {});
        void QueueLoadRom(const std::wstring& path, std::vector<uint8_t> romData, const std::wstring& originalPath = {});
        void ProcessPendingLoad();
        void RenderLoadingScreen(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, D2D1_SIZE_F logicalSize);
        void EnsureLoadingDisc();
        enum LoadState { LOAD_IDLE, LOAD_PICKING, LOAD_READING, LOAD_BOOTING, LOAD_DONE, LOAD_FAILED };
        bool WasFilePickerRequested() { bool r = m_requestFilePicker; m_requestFilePicker = false; return r; }
        LoadState GetLoadState() const { return m_loadState; }
        void SetLoadState(LoadState s) { m_loadState = s; }
        bool IsLoaded() const { return m_retroCore && m_retroCore->IsLoaded(); }
        void ActivateLoadingScreen() {
            m_loadState = LOAD_BOOTING;
            m_loadingActive = true;
            m_loadingAngle = 0.0f;
            m_loadingDots = 0;
            m_loadingFrame = 0;
            QueryPerformanceCounter(&m_loadingStart);
        }

    private:
        void BootCore();
        void PollKeyboard();

        std::shared_ptr<DX::DeviceResources> m_deviceResources;

        std::unique_ptr<SdlInput> m_sdlInput;
        std::unique_ptr<RetroCore> m_retroCore;
        std::unique_ptr<RetroScreenRenderer> m_retroScreen;
        std::unique_ptr<XAudio2Output> m_xaudio2;
        FrontendMenu m_menu;

        DX::StepTimer m_timer;

        DirectX::XMVECTORF32 m_clearColor;
        bool m_requestFilePicker = false;
        bool m_spaceHeld = false;
        bool m_hasController;

        bool m_retroRunning = false;

        std::wstring m_currentTempPath;
        void CleanupTempFile();

        // Load state tracking (for hang detection + loading screen)
        LoadState m_loadState = LOAD_IDLE;
        int m_loadTimer = 0;
        struct PendingLoad {
            std::wstring path;
            std::wstring originalPath;
            std::vector<uint8_t> data;
        };
        std::unique_ptr<PendingLoad> m_pendingLoad;

        // Loading screen state
        bool m_loadingActive = false;
        float m_loadingAngle = 0.0f;
        int m_loadingDots = 0;
        int m_loadingFrame = 0;
        LARGE_INTEGER m_loadingStart = {};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> m_loadingDisc;

#ifdef MOUSE_SUPPORT
        float m_pointerX = 0.5f;
        float m_pointerY = 0.5f;
        bool m_pointerDown = false;
        float m_lastPointerX = 0.5f;
        float m_lastPointerY = 0.5f;
        float m_lastPointerPX = 0.0f;
        float m_lastPointerPY = 0.0f;
        uint32_t m_mousePointerId = 0;

        // Virtual cursor for gamepad→PUREMENU (Phase 3)
        float m_virtualCursorX = 0.5f;
        float m_virtualCursorY = 0.5f;
        LARGE_INTEGER m_lastPointerTime = {};
#endif

        bool m_activeVKeyState[256] = {};

        // Frame pacing tracking
        bool m_frameLate = false;
        int m_lateFrameCount = 0;
        LARGE_INTEGER m_qpcFreq = {};

        // DPad auto-repeat state
        int m_dpadRepeatBtn = -1;       // which button is being held (-1 = none)
        ULONGLONG m_dpadRepeatStart = 0; // GetTickCount64 when button was first pressed
        ULONGLONG m_dpadRepeatNext = 0;  // GetTickCount64 when next repeat fires

        // Gamepad mouse mode toggle (LB+RB+Select)
        bool m_gamepadMouseMode = false;  // OFF by default; stick→mouse, A→click, B→escape
        bool m_lbrbsPrevHeld = false;     // edge detection for combo press

        // Audio-driven pacing (audio backpressure in XAudio2Output handles timing)
        int m_lastRetroRuns = 0;

#ifdef MOUSE_SUPPORT
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_cursorBrush;
#endif

    };
}
