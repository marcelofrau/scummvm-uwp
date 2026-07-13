#include <string>
#include "libretro.h"
#include "dosbox_pure_sta.h"
#include "LogHelper.h"
#include "dbp_log.h"
#include <string>

extern "C" void dbp_log_info(const char* msg)
{
#ifdef XB_INSPECTOR_ENABLED
    std::string s(msg);
    if (!s.empty() && s.back() == '\n')
        s.pop_back();
    spdlog::info("{}", s);
#endif
}
#ifdef MOUSE_SUPPORT
#include "Content/RetroCore.h"
using namespace dosbox_uwp;
#endif

int DBPS_SaveSlotIndex = 0;
std::string DBPS_BrowsePath;

void DBPS_OnContentLoad(const char*, const char*, size_t) {}
void DBPS_SubmitOSDFrame(const void*, unsigned, unsigned) {}
void DBPS_GetMouse(short& mx, short& my, bool)
{
#ifdef MOUSE_SUPPORT
    RetroCore::GetPointer(mx, my);
#else
    mx = 0; my = 0;
#endif
}
void DBPS_StartCaptureJoyBind(unsigned, unsigned, unsigned, unsigned, bool) {}
bool DBPS_HaveJoy() { return false; }
bool DBPS_GetJoyBind(unsigned, unsigned, unsigned, unsigned, bool, std::string&, std::string&, const char*) { return false; }
void DBPS_RequestSaveLoad(bool, bool) {}
bool DBPS_HaveSaveSlot() { return false; }

// Minimal JSON parser for {"key":"value","key2":"value2"} config overrides
static std::string json_str(const std::string& j, size_t& i)
{
    std::string s;
    if (i >= j.size() || j[i] != '"') return s;
    ++i; // skip opening "
    for (; i < j.size(); ++i)
    {
        if (j[i] == '\\') { ++i; if (i < j.size()) s += j[i]; }
        else if (j[i] == '"') { ++i; return s; }
        else s += j[i];
    }
    return s;
}

static void skip_ws(const std::string& j, size_t& i)
{
    while (i < j.size() && (j[i] == ' ' || j[i] == '\t' || j[i] == '\n' || j[i] == '\r'))
        ++i;
}

bool DBPS_ApplyConfigOverrides(const std::string& json)
{
    size_t i = 0;
    skip_ws(json, i);
    if (i >= json.size() || json[i] != '{') { OutputDebugStringA("[dbps] ApplyConfigOverrides: no '{'"); return false; }
    ++i;

    bool applied = false;
    while (i < json.size())
    {
        skip_ws(json, i);
        if (i >= json.size() || json[i] == '}') break;

        if (json[i] != '"') { ++i; continue; }
        std::string key = json_str(json, i);
        if (key.empty()) { ++i; continue; }

        skip_ws(json, i);
        if (i < json.size() && json[i] == ':') ++i;
        skip_ws(json, i);

        std::string val;
        if (i < json.size() && json[i] == '"')
        {
            val = json_str(json, i);
        }
        else
        {
            // numeric or boolean value: read until , or }
            while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ' ')
            {
                val += json[i];
                ++i;
            }
        }

#ifdef MOUSE_SUPPORT
        RetroCore::SetOptionValue(key.c_str(), val.c_str());
#endif
        applied = true;

        skip_ws(json, i);
        if (i < json.size() && json[i] == ',') ++i;
    }

    OutputDebugStringA(("[dbps] ApplyConfigOverrides: " + std::to_string(applied ? 1 : 0) + " entries").c_str());
    return applied;
}

bool DBPS_IsConfigOverride(const char*) { return false; }
void DBPS_ToggleConfigOverride(const char*, const char*) {}
std::string DBPS_GetConfigOverrideJSON() { return std::string(); }
