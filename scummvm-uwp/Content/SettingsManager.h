#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <map>

struct ThemeColors
{
    // Panel
    uint32_t bg_panel       = 0xFF0b002e;
    uint32_t bg_fullscreen  = 0xFF000000;
    uint32_t frame          = 0xFF761694;
    uint32_t title_bg       = 0xFF94164d;

    // Text
    uint32_t text_title     = 0xFFfefefe;
    uint32_t text_normal    = 0xFFaabbb9;
    uint32_t text_value     = 0xFF59caf9;
    uint32_t text_disabled  = 0xFF74898e;
    uint32_t text_bios      = 0xFF30f84c;

    // Selection
    uint32_t selection_bg   = 0xFF2c0087;
    uint32_t selection_text = 0xFFfefefe;

    // FileBrowser extras
    uint32_t file_text      = 0xFFd0d0d0;
    float    overlay_alpha  = 0.55f;

    // PUREMENU extras
    uint32_t col_warn       = 0xFFF8305b;
    uint32_t col_dim        = 0xFF74898e;
    uint32_t col_white      = 0xFFfefefe;

    // Buttons
    uint32_t bg_btn_off     = 0xFF2c004a;
    uint32_t bg_btn_on      = 0xFF761694;
    uint32_t bg_btn_hover   = 0xFF94164d;
    uint32_t col_btn_text   = 0xFFfefefe;

    // Keyboard
    uint32_t bg_key         = 0xFF2c004a;
    uint32_t bg_key_hover   = 0xFF761694;
    uint32_t bg_key_press   = 0xFFF8305b;
    uint32_t bg_key_held    = 0xFF30f84c;
    uint32_t bg_key_outline = 0xFF000000;
    uint32_t col_key_text   = 0xFFfefefe;
};

class SettingsManager
{
public:
    static void Initialize(const std::string& settingsPath);
    static void Shutdown();

    static const ThemeColors& GetTheme();
    static void ApplyThemeToPUREMENU();

    static std::string GetOption(const char* key, const char* defaultVal = nullptr);
    static void SetOption(const char* key, const char* value);
    static void ResetToDefaults();
    static void ResetSectionDefaults(const std::vector<std::string>& keys);

    static void Save();
    static bool IsLoaded();
    static void ForEachOption(void (*callback)(const char* key, const char* value));

    // History (recently opened games)
    struct HistoryEntry {
        std::string filename;  // display name (e.g. "xargon.dosz")
        std::string fullPath;  // full path for loading
        uint64_t timestamp;    // unix time for sorting
    };
    static void AddToHistory(const std::string& filename, const std::string& fullPath);
    static const std::vector<HistoryEntry>& GetHistory();
    static void ClearHistory();
    static void SaveHistory();

private:
    static std::string s_settingsPath;
    static ThemeColors s_theme;
    static std::map<std::string, std::string> s_coreOptions;
    static bool s_loaded;
    static bool s_dirty;
    static uint64_t s_lastSaveTime;

    // History
    static std::vector<HistoryEntry> s_history;
    static std::string s_historyPath;
    static const int MAX_HISTORY = 10;

    static uint32_t ParseHexColor(const char* str, uint32_t defaultVal);
    static std::string StripJsonComments(const std::string& raw);
    static std::string SerializeJson();
    static void LoadDefaults();
    static void LoadHistory();
};

// Bridge: defined in dosbox_pure_libretro.cpp (forked OSD header scope)
// Sets the PUREMENU static color variables from a ThemeColors struct.
void DBPS_SetMenuColorsFromTheme(const ThemeColors& t);
