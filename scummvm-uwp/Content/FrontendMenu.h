#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <functional>
#include "FileBrowser.h"
#include "AboutDialog.h"
#include "ConfirmDialog.h"

namespace dosbox_uwp
{
    enum class MenuAction
    {
        NONE,
        SUBMENU,
        BACK,
        CONTINUE_GAME,
        OPEN_FILE,
        OPEN_HISTORY,
        OPEN_PUREMENU,
        SETTINGS,
        GENERAL,
        INPUT,
        PERFORMANCE,
        VIDEO,
        SYSTEM,
        AUDIO,
        STATE,
        TOGGLE_VALUE,
        RESET_DEFAULTS,
        RESET_ALL_SETTINGS,
        CLEAR_HISTORY,
        ABOUT,
        EXIT
    };

    struct MenuItem
    {
        std::string label;
        MenuAction action;
        std::vector<MenuItem> children;
        std::vector<std::string> values;       // display labels (UI)
        std::vector<std::string> coreValues;   // core-expected values (sent to core, stored in SettingsManager)
        int currentValue = 0;
        bool enabled = true;
        std::string optionKey;  // core or frontend option key for TOGGLE_VALUE
    };

    struct MenuPage
    {
        std::string title;
        std::vector<MenuItem> items;
    };

    class FrontendMenu
    {
    public:
        FrontendMenu();

        void Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);
        void OnDPad(bool up);
        void OnDPadLeft();
        void OnDPadRight();
        void OnEasterEgg();
        void OnConfirm();
        void OnBack();
        void OnPageUp();
        void OnPageDown();
        int HitTest(float sx, float sy);
        void SelectItem(int idx);
        void HandlePointerMove(float sx, float sy);
        void HandlePointerDown(float sx, float sy, unsigned btn);
        void HandlePointerWheel(int delta);
        void RenderFullScreen(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);

        bool IsVisible() const { return m_visible; }
        void Show();
        void Hide() { ReleaseResources(); m_visible = false; }
        void ReleaseResources();
        void RebuildItems();
        void RefreshMenuItems() { BuildMenuTree(); }  // Re-reads saved values from SettingsManager

        void SetCoreLoaded(bool loaded);
        void LoadLogoBitmap(ID2D1DeviceContext* d2d);

        void SetBiosInfo(const std::vector<std::wstring>& lines) { m_biosLines = lines; }
        bool IsBootAnimComplete() const { return m_animPhase >= ANIM_COMPLETE; }
        bool IsBeepGracePeriod() const { return m_beepPlayed && (GetTickCount64() - m_animCompleteTick) < 300; }
        void ResetBootAnim() { m_animPhase = ANIM_INITIAL_DELAY; m_animStartTick = GetTickCount64(); m_beepPlayed = false; }

        std::function<void()> onOpenFile;
        std::function<void()> onOpenPuremenu;
        std::function<void()> onExit;
        std::function<void()> onBeep;
        std::function<void(const char* key, const char* value)> onOptionChanged;
        std::function<void(const std::wstring&)> onFileSelectedHistory;

        FileBrowser m_fileBrowser;
        AboutDialog m_aboutDialog;
        ConfirmDialog m_confirmDialog;

    private:
        void BuildMenuTree();
        void EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH);
        void SaveCurrentSettings();
        void ShowToast(const wchar_t* msg);
        void DrawValueText(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite,
            const std::string& value, float containerX, float containerW, float iy,
            ID2D1Brush* brush, bool isSelected);

        bool m_visible = true;
        bool m_coreLoadedPrev = false;

        // Toast feedback
        std::wstring m_toastMsg;
        ULONGLONG m_toastTick = 0;
        static constexpr DWORD TOAST_DURATION_MS = 2000;

        enum AnimPhase { ANIM_INITIAL_DELAY, ANIM_BIOS_POST, ANIM_MEMORY_COUNT, ANIM_COMPLETE };
        AnimPhase m_animPhase = ANIM_INITIAL_DELAY;
        ULONGLONG m_animStartTick = 0;
        int m_biosLinesToShow = 0;
        int m_animMemoryDisplayedKB = 0;
        int m_memoryTotalMB = 0;
        bool m_beepPlayed = false;
        ULONGLONG m_animCompleteTick = 0;

        int m_selected = 0;
        int m_scrollOffset = 0;

        std::vector<MenuItem> m_mainItems;
        std::vector<MenuItem> m_settingsItems;
        std::vector<MenuItem> m_generalItems;
        std::vector<MenuItem> m_inputItems;
        std::vector<MenuItem> m_performanceItems;
        std::vector<MenuItem> m_videoItems;
        std::vector<MenuItem> m_systemItems;
        std::vector<MenuItem> m_audioItems;
        std::vector<MenuItem> m_historyItems;
        std::vector<MenuItem> m_stateItems;
        std::vector<MenuItem> m_aboutItems;
        std::vector<std::wstring> m_biosLines;
        int m_easterEggIndex = 0;
        static const wchar_t* const s_easterEggs[];
        static const int s_easterEggCount;

        // Overlay state (Video/Audio/CoreOptions/Settings render as stacked overlays)
        bool m_overlayActive = false;
        std::string m_overlayTitle;
        std::vector<MenuItem>* m_overlayItems = nullptr;
        int m_panelSavedSelected = 0;
        int m_panelSavedScrollOffset = 0;

        struct OverlayState
        {
            std::string title;
            std::vector<MenuItem>* items;
            int selected;
            int scrollOffset;
        };
        std::vector<OverlayState> m_overlayStack;

        struct PageRef
        {
            std::string title;
            std::vector<MenuItem>* items;
        };
        std::vector<PageRef> m_stack;

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTitleBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushSelected;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushItemText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTitleText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushValueText;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushDisabled;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFooter;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFrame;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBios;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBlack;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushWhite;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatTitle;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatItem;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormatFooter;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_epaLogo;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_dosboxLogo;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_fontCollection;

    bool m_resourcesCreated = false;

        static constexpr float TITLE_HEIGHT = 36.0f;
        static constexpr float ITEM_HEIGHT = 30.0f;
        static constexpr float ITEM_INDENT = 24.0f;
        static constexpr float FOOTER_HEIGHT = 22.0f;
        static constexpr float MAX_VISIBLE = 14.0f;
        static constexpr float PANEL_MARGIN = 20.0f;
        static constexpr float PANEL_WIDTH_RATIO = 0.45f;
        static constexpr float PANEL_MAX_WIDTH = 480.0f;
        static constexpr float PANEL_MIN_WIDTH = 350.0f;
        static constexpr float PANEL_FIXED_HEIGHT = 360.0f;
        // Overlay panel (settings dialogs)
        static constexpr float OVERLAY_WIDTH_RATIO = 0.60f;
        static constexpr float OVERLAY_MAX_WIDTH = 800.0f;
        static constexpr float VALUE_WIDTH_RATIO = 0.40f;
        static constexpr float OVERLAY_HEIGHT_RATIO = 0.65f;
        static constexpr float OVERLAY_MAX_HEIGHT = 550.0f;
        static constexpr float LOGO_SIZE = 100.0f;
        static constexpr float LOGO_MARGIN = 20.0f;

        float m_lastPanelX = 0, m_lastPanelY = 0;
        float m_lastPanelW = 0, m_lastPanelH = 0;
        float m_lastScreenW = 0, m_lastScreenH = 0;
        std::wstring m_versionStr;
    };
}
