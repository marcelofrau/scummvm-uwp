#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <functional>

namespace dosbox_uwp
{
    class ConfirmDialog
    {
    public:
        enum Mode { CONFIRM, INFO };

        ConfirmDialog();

        void Open(const std::string& message, Mode mode, std::function<void(bool confirmed)> onResult = nullptr);
        void Close();
        bool IsVisible() const { return m_visible; }

        void Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);

        void OnConfirm();
        void OnBack();
        void HandlePointerDown(float sx, float sy);
        void HandleKeyDown(unsigned int vkey);
        void ReleaseResources();

    private:
        void EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);

        bool m_visible = false;
        bool m_resourcesCreated = false;
        Mode m_mode = INFO;
        std::string m_message;
        std::function<void(bool)> m_onResult;

        // Panel geometry
        float m_panelX = 0, m_panelY = 0, m_panelW = 0, m_panelH = 0;

        // Button hit areas
        float m_btnOkX = 0, m_btnOkY = 0, m_btnOkW = 0, m_btnOkH = 0;
        float m_btnCancelX = 0, m_btnCancelY = 0, m_btnCancelW = 0, m_btnCancelH = 0;
        int m_selectedBtn = 0; // 0=OK, 1=Cancel (CONFIRM mode)

        // D2D resources
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFrame;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTitleBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushDim;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBlack;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtnBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtnHover;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtnText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushCyan;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushGreen;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatMsg;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatBtn;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatTitle;
        Microsoft::WRL::ComPtr<IDWriteFontCollection> m_fontCollection;

        static constexpr float TITLE_HEIGHT = 46.0f;
        static constexpr float PANEL_PADDING = 24.0f;
        static constexpr float BTN_HEIGHT = 40.0f;
        static constexpr float BTN_SPACING = 16.0f;
    };
}
