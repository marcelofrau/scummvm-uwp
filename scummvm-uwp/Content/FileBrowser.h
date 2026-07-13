#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <functional>

namespace dosbox_uwp
{
    struct FileEntry
    {
        std::wstring name;
        bool isDir;
    };

    class FileBrowser
    {
    public:
        FileBrowser();

        void Open();
        void Close();
        void ReleaseResources();
        bool IsVisible() const { return m_visible; }

        void Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);

        void OnDPad(bool up);
        void OnConfirm();
        void OnBack();
        void OnPageUp();
        void OnPageDown();
        void OnHome();
        int  HitTest(float sx, float sy);
        void HandlePointerMove(float sx, float sy);
        void HandlePointerDown(float sx, float sy);
        void HandlePointerWheel(int delta);

        std::function<void(const std::wstring&)> onFileSelected;
        std::function<void()> onBeep;
        ULONGLONG m_wheelIgnoreUntil = 0;

    private:
        void EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);
        void ScanDirectory(const std::wstring& path);
        bool PassesExtensionFilter(const std::wstring& name);
        std::wstring GetParentPath(const std::wstring& path);
        void DrawMarqueeText(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite,
            const wchar_t* text, UINT32 len,
            float x, float y, float maxW, float h,
            ID2D1Brush* brush, IDWriteFontCollection* fc);

        bool m_visible = false;
        bool m_resourcesCreated = false;

        std::wstring m_currentPath;
        std::vector<FileEntry> m_entries;
        int m_selected = 0;
        int m_scrollOffset = 0;

        // Marquee state per-item (tracks which item is marqueeing)
        int m_marqueeItemIdx = -1;
        ULONGLONG m_marqueeStartTime = 0;

        // Panel geometry (recomputed each frame)
        float m_panelX = 0, m_panelY = 0, m_panelW = 0, m_panelH = 0;
        float m_lastScreenW = 0, m_lastScreenH = 0;

        // Brushes
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFrame;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTitleBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushSelectedBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushSelectedText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushItemText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushDirText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFileText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushPathText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFooter;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBlack;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushDimPrefix;

        // Text formats
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatTitle;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatItem;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatPath;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatFooter;

        // Font collection (VCR OSD Mono)
        Microsoft::WRL::ComPtr<IDWriteFontCollection> m_fontCollection;

        // Panel dimensions
        static constexpr float TITLE_HEIGHT = 36.0f;
        static constexpr float ITEM_HEIGHT = 30.0f;
        static constexpr float ITEM_INDENT = 24.0f;
        static constexpr float FOOTER_HEIGHT = 22.0f;
        static constexpr float MAX_VISIBLE = 16.0f;
        static constexpr float PANEL_PADDING = 8.0f;
        static constexpr float PANEL_WIDTH_RATIO = 0.70f;
        static constexpr float PANEL_HEIGHT_RATIO = 0.65f;
        static constexpr float PANEL_MAX_WIDTH = 900.0f;
        static constexpr float PANEL_MIN_WIDTH = 500.0f;
        static constexpr float PANEL_MAX_HEIGHT = 600.0f;
        static constexpr float PANEL_MIN_HEIGHT = 350.0f;

        // Marquee constants
        static constexpr float MARQUEE_PAUSE_SEC = 1.0f;
        static constexpr float MARQUEE_SPEED_PX = 60.0f;
        static constexpr float MARQUEE_PAD = 20.0f;
    };
}
