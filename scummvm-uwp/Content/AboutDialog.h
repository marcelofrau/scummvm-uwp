#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace dosbox_uwp
{
    class AboutDialog
    {
    public:
        AboutDialog();

        void Open(const std::wstring& versionStr);
        void Close();
        void ReleaseResources();
        bool IsVisible() const { return m_visible; }

        void Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);

        void OnConfirm();
        void OnBack();
        int  HitTest(float sx, float sy);
        void HandlePointerDown(float sx, float sy);

    private:
        void EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);

        bool m_visible = false;
        bool m_resourcesCreated = false;

        // Panel geometry
        float m_panelX = 0, m_panelY = 0, m_panelW = 0, m_panelH = 0;

        // Fonts
        Microsoft::WRL::ComPtr<IDWriteFontCollection> m_fontCollection;

        // Brushes
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFrame;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTitleBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTitleText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBodyText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushCyan;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushGreen;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushYellow;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushRed;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushDim;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBlack;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushSelection;

        // Text formats
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatBig;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatSmall;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatTitle;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatBody;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatFooter;

        // DOSBox logo bitmap
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_dosboxLogo;

        static constexpr float TITLE_HEIGHT = 36.0f;
        static constexpr float FOOTER_HEIGHT = 32.0f;
        static constexpr float PANEL_PADDING = 24.0f;

        // About content (built once in Open)
        enum LineFont { FONT_BODY, FONT_BIG, FONT_SMALL };

        struct AboutLine {
            std::wstring text;
            uint32_t color;
            LineFont font;
            bool isCentered;
        };
        std::vector<AboutLine> m_lines;
    };
}
