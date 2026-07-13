#include "pch.h"
#include "RetroCore.h"
#include "SettingsManager.h"
#include "libretro.h"
#include "XAudio2Output.h"
#include <vfs/vfs.h>
#include <vfs/vfs_implementation.h>

#include <cstring>
#include <algorithm>
#include <cstdarg>
#include <string>
#include <vector>

static retro_vfs_interface uwp_vfs_iface = {
    reinterpret_cast<retro_vfs_get_path_t>(retro_vfs_file_get_path_impl),
    reinterpret_cast<retro_vfs_open_t>(retro_vfs_file_open_impl),
    reinterpret_cast<retro_vfs_close_t>(retro_vfs_file_close_impl),
    reinterpret_cast<retro_vfs_size_t>(retro_vfs_file_size_impl),
    reinterpret_cast<retro_vfs_tell_t>(retro_vfs_file_tell_impl),
    reinterpret_cast<retro_vfs_seek_t>(retro_vfs_file_seek_impl),
    reinterpret_cast<retro_vfs_read_t>(retro_vfs_file_read_impl),
    reinterpret_cast<retro_vfs_write_t>(retro_vfs_file_write_impl),
    reinterpret_cast<retro_vfs_flush_t>(retro_vfs_file_flush_impl),
    reinterpret_cast<retro_vfs_remove_t>(retro_vfs_file_remove_impl),
    reinterpret_cast<retro_vfs_rename_t>(retro_vfs_file_rename_impl),
    reinterpret_cast<retro_vfs_truncate_t>(retro_vfs_file_truncate_impl),
    reinterpret_cast<retro_vfs_stat_t>(retro_vfs_stat_impl),
    reinterpret_cast<retro_vfs_mkdir_t>(retro_vfs_mkdir_impl),
    reinterpret_cast<retro_vfs_opendir_t>(retro_vfs_opendir_impl),
    reinterpret_cast<retro_vfs_readdir_t>(retro_vfs_readdir_impl),
    reinterpret_cast<retro_vfs_dirent_get_name_t>(retro_vfs_dirent_get_name_impl),
    reinterpret_cast<retro_vfs_dirent_is_dir_t>(retro_vfs_dirent_is_dir_impl),
    reinterpret_cast<retro_vfs_closedir_t>(retro_vfs_closedir_impl)
};

#include "dosbox_pure_sta.h"

using namespace dosbox_uwp;

const void* RetroCore::s_frameData = nullptr;
unsigned RetroCore::s_frameWidth = 0;
unsigned RetroCore::s_frameHeight = 0;
unsigned RetroCore::s_framePitch = 0;
bool RetroCore::s_frameValid = false;
bool RetroCore::s_keyboardState[RETROK_LAST] = {};
retro_keyboard_event_t RetroCore::s_keyboardCallback = nullptr;
retro_log_printf_t RetroCore::s_logCallback = nullptr;
int RetroCore::s_mouseRelX = 0;
int RetroCore::s_mouseRelY = 0;
#ifdef MOUSE_SUPPORT
bool RetroCore::s_mouseBtnLeft = false;
bool RetroCore::s_mouseBtnRight = false;
bool RetroCore::s_mouseBtnMiddle = false;
int RetroCore::s_mouseWheel = 0;
#endif
float RetroCore::s_ptrX = 0;
float RetroCore::s_ptrY = 0;
bool RetroCore::s_ptrDown = false;
double RetroCore::s_targetFps = 60.0;
bool RetroCore::s_shutdownRequested = false;
bool RetroCore::s_joypadState[16] = {};
XAudio2Output* RetroCore::s_audioOutput = nullptr;
std::map<std::string, std::string> RetroCore::s_optionValues;
bool RetroCore::s_optionValuesChanged = false;
static const char* OVERRIDE_MENU_TIME = "-1";

RetroCore::RetroCore() {}
RetroCore::~RetroCore() { Shutdown(); }

static bool retro_env_wrap(unsigned cmd, void* data)
{
    return RetroCore::retro_env(cmd, data) != 0;
}

bool RetroCore::Init()
{
    OutputDebugStringA("[dosbox-uwp] RetroCore::Init enter\n");
    s_frameValid = false;
    s_frameData = nullptr;

    OutputDebugStringA("[dosbox-uwp] retro_set_environment\n");
    retro_set_environment(retro_env_wrap);
    OutputDebugStringA("[dosbox-uwp] retro_set_video_refresh\n");
    retro_set_video_refresh(&RetroCore::retro_video);
    OutputDebugStringA("[dosbox-uwp] retro_set_audio_sample_batch\n");
    retro_set_audio_sample_batch(&RetroCore::retro_audio);
    OutputDebugStringA("[dosbox-uwp] retro_set_input_poll\n");
    retro_set_input_poll(&RetroCore::retro_input_poll);
    OutputDebugStringA("[dosbox-uwp] retro_set_input_state\n");
    retro_set_input_state(&RetroCore::retro_input_state);

    OutputDebugStringA("[dosbox-uwp] retro_init call\n");
    retro_init();
    m_initialized = true;

    // Apply theme colors from settings to PUREMENU statics
    SettingsManager::ApplyThemeToPUREMENU();

    OutputDebugStringA("[dosbox-uwp] RetroCore::Init exit OK\n");
    return true;
}

bool RetroCore::LoadGame(const std::wstring& uwpPath, const std::vector<uint8_t>& romData)
{
    OutputDebugStringA("[dosbox-uwp] RetroCore::LoadGame enter\n");
    if (!m_initialized)
    {
        OutputDebugStringA("[dosbox-uwp] LoadGame FAILED: not initialized\n");
        return false;
    }

    char buf[512];
    int len = WideCharToMultiByte(CP_UTF8, 0, uwpPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
    {
        OutputDebugStringA("[dosbox-uwp] LoadGame FAILED: WideCharToMultiByte len=0\n");
        return false;
    }
    std::string pathUtf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, uwpPath.c_str(), -1, &pathUtf8[0], len, nullptr, nullptr);
    sprintf_s(buf, "[dosbox-uwp] LoadGame path: %s data=%zu bytes\n", pathUtf8.c_str(), romData.size());
    OutputDebugStringA(buf);

    retro_game_info info = {};
    info.path = pathUtf8.c_str();
    info.data = romData.empty() ? nullptr : romData.data();
    info.size = romData.size();

    OutputDebugStringA("[dosbox-uwp] retro_load_game call\n");
    if (!retro_load_game(&info))
    {
        OutputDebugStringA("[dosbox-uwp] retro_load_game FAILED\n");
        return false;
    }

    // Fetch AV info: populates core's internal av_info (fps, sample_rate)
    retro_system_av_info av = {};
    retro_get_system_av_info(&av);
    {
        char buf[256];
        sprintf_s(buf, "[dosbox-uwp] av_info: %dx%d @ %.2fHz, sample_rate=%.0f\n",
            av.geometry.base_width, av.geometry.base_height,
            av.timing.fps, av.timing.sample_rate);
        OutputDebugStringA(buf);
    }
    s_targetFps = av.timing.fps > 0 ? av.timing.fps : 60.0;

    m_loaded = true;
    OutputDebugStringA("[dosbox-uwp] retro_load_game SUCCESS\n");

    // Apply saved core options from settings
    {
        auto transparency = SettingsManager::GetOption("dosbox_pure_menu_transparency", "70");
        SetOptionValue("dosbox_pure_menu_transparency", transparency.c_str());
    }

    return true;
}

void RetroCore::RunFrame()
{
    if (!m_loaded) return;
    static int frameCount = 0;
    frameCount++;
    if ((frameCount % 600) == 0)
    {
        char buf[128];
        sprintf_s(buf, "[dosbox-uwp] RunFrame #%d\n", frameCount);
        OutputDebugStringA(buf);
    }

    LARGE_INTEGER t1, t2, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t1);
    retro_run();
    QueryPerformanceCounter(&t2);

#ifdef MOUSE_SUPPORT
    // Reset per-frame mouse state
    s_mouseRelX = 0;
    s_mouseRelY = 0;
    s_mouseWheel = 0;
#endif

    if ((frameCount % 600) == 0)
    {
        double ms = (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;
        char buf[128];
        sprintf_s(buf, "[dosbox-uwp] RunFrame #%d took %.1fms\n", frameCount, ms);
        OutputDebugStringA(buf);
    }
}

void RetroCore::UnloadGame()
{
    if (!m_loaded) return;
    OutputDebugStringA("[dosbox-uwp] UnloadGame\n");
    m_loaded = false;
    if (s_audioOutput)
        s_audioOutput->Flush();
    retro_unload_game();
    // Reset all per-game state so next load starts clean
    s_shutdownRequested = false;
    memset(s_keyboardState, 0, sizeof(s_keyboardState));
    memset(s_joypadState, 0, sizeof(s_joypadState));
    s_optionValues.clear();
    s_optionValuesChanged = false;
}

void RetroCore::Shutdown()
{
    OutputDebugStringA("[dosbox-uwp] Shutdown\n");
    UnloadGame();
    if (m_initialized)
    {
        m_initialized = false;
        OutputDebugStringA("[dosbox-uwp] retro_deinit\n");
        retro_deinit();
    }
}

void RetroCore::ToggleOSD()
{
    OutputDebugStringA("[dosbox-uwp] ToggleOSD\n");
    DBPS_ToggleOSD();
}

static void RETRO_CALLCONV uwp_log(enum retro_log_level level, const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef XB_INSPECTOR_ENABLED
    auto lv = static_cast<spdlog::level::level_enum>(level);
    spdlog::log(lv, "{}", buf);
#else
    OutputDebugStringA(buf);
#endif
}

static const char* retro_env_name(unsigned cmd)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: return "GET_VFS_INTERFACE";
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: return "GET_SYSTEM_DIRECTORY";
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: return "GET_SAVE_DIRECTORY";
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return "SET_PIXEL_FORMAT";
    case RETRO_ENVIRONMENT_SET_HW_RENDER: return "SET_HW_RENDER";
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: return "SET_MESSAGE_EXT";
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: return "SET_KEYBOARD_CALLBACK";
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: return "GET_THROTTLE_STATE";
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: return "GET_LOG_INTERFACE";
    case RETRO_ENVIRONMENT_SHUTDOWN: return "SHUTDOWN";
    case RETRO_ENVIRONMENT_SET_VARIABLE: return "SET_VARIABLE";
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: return "SET_CORE_OPTIONS_V2";
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: return "SET_CORE_OPTIONS";
    case RETRO_ENVIRONMENT_SET_VARIABLES: return "SET_VARIABLES";
    case RETRO_ENVIRONMENT_GET_VARIABLE: return "GET_VARIABLE";
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: return "GET_VARIABLE_UPDATE";
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK: return "SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK";
    default: return "UNKNOWN";
    }
}

void RetroCore::SetKeyState(unsigned key, bool down)
{
    if (key < RETROK_LAST)
    {
        s_keyboardState[key] = down;
        if (s_keyboardCallback)
            s_keyboardCallback(down, key, 0, 0);
    }
}

void RetroCore::SetJoypadButton(unsigned id, bool held)
{
    if (id < 16)
        s_joypadState[id] = held;
}

void RetroCore::SetMouseMove(int relX, int relY)
{
    s_mouseRelX += relX;
    s_mouseRelY += relY;
}

void RetroCore::SetPointer(float x, float y, bool down)
{
    s_ptrX = x;
    s_ptrY = y;
    s_ptrDown = down;
}

#ifdef MOUSE_SUPPORT
void RetroCore::SetMouseButton(unsigned btn, bool down)
{
    switch (btn)
    {
    case 1: s_mouseBtnLeft = down; break;
    case 2: s_mouseBtnRight = down; break;
    case 3: s_mouseBtnMiddle = down; break;
    }
}

void RetroCore::SetMouseWheel(int delta)
{
    s_mouseWheel += delta;
}

void RetroCore::GetPointer(short& mx, short& my)
{
    mx = (short)((s_ptrX * 2.0f - 1.0f) * 0x7fff);
    my = (short)((s_ptrY * 2.0f - 1.0f) * 0x7fff);
}
#endif

void RetroCore::SetAudioOutput(XAudio2Output* output)
{
    s_audioOutput = output;
}

void RetroCore::SetOptionValue(const char* key, const char* value)
{
    if (key)
    {
        s_optionValues[key] = (value ? value : "");
        s_optionValuesChanged = true;
    }
}

int RetroCore::retro_env(unsigned cmd, void* data)
{
    char buf[256];
#ifdef FRAME_TRACE
    sprintf_s(buf, "[dosbox-uwp] retro_env cmd=%d(%s)\n", cmd, retro_env_name(cmd));
    OutputDebugStringA(buf);
#endif

    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
    {
        auto* vfs_info = static_cast<retro_vfs_interface_info*>(data);
        vfs_info->required_interface_version = 3;
        vfs_info->iface = &uwp_vfs_iface;
        OutputDebugStringA("[dosbox-uwp]   VFS interface provided (v3)\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    {
        static std::string sysDir;
        if (sysDir.empty())
        {
            auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
            auto path = localFolder->Path->Data();
            int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
            sysDir.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, &sysDir[0], len, nullptr, nullptr);
        }
        *static_cast<const char**>(data) = sysDir.c_str();
        sprintf_s(buf, "[dosbox-uwp]   SYSTEM_DIR=%s\n", sysDir.c_str());
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    {
        static std::string saveDir;
        if (saveDir.empty())
        {
            auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
            auto path = localFolder->Path->Data();
            int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
            saveDir.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, &saveDir[0], len, nullptr, nullptr);
        }
        *static_cast<const char**>(data) = saveDir.c_str();
        sprintf_s(buf, "[dosbox-uwp]   SAVE_DIR=%s\n", saveDir.c_str());
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
        auto fmt = *static_cast<retro_pixel_format*>(data);
        const char* fmtName = (fmt == RETRO_PIXEL_FORMAT_0RGB1555 ? "0RGB1555" :
            fmt == RETRO_PIXEL_FORMAT_XRGB8888 ? "XRGB8888" :
            fmt == RETRO_PIXEL_FORMAT_RGB565 ? "RGB565" : "UNKNOWN");
        sprintf_s(buf, "[dosbox-uwp]   SET_PIXEL_FORMAT=%s(%d)\n", fmtName, fmt);
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
        OutputDebugStringA("[dosbox-uwp]   SET_HW_RENDER=REJECTED (return 0)\n");
        return 0;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
    {
        auto* msg = static_cast<const retro_message_ext*>(data);
        OutputDebugStringA("[dosbox-pure] ");
        OutputDebugStringA(msg->msg);
        OutputDebugStringA("\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
    {
        auto* cb = static_cast<const retro_keyboard_callback*>(data);
        s_keyboardCallback = cb ? cb->callback : nullptr;
        OutputDebugStringA("[dosbox-uwp]   SET_KEYBOARD_CALLBACK: stored\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE:
    {
        auto* state = static_cast<retro_throttle_state*>(data);
        // Report VSYNC throttle when vsync is enabled (syncInterval=1)
        // so core knows there's external frame pacing
        state->mode = RETRO_THROTTLE_VSYNC;
        state->rate = (float)s_targetFps;
#ifdef FRAME_TRACE
        char buf[128];
        sprintf_s(buf, "[dosbox-uwp]   THROTTLE_STATE: VSYNC rate=%.0f\n", s_targetFps);
        OutputDebugStringA(buf);
#endif
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    {
        auto* cb = static_cast<retro_log_callback*>(data);
        cb->log = uwp_log;
        OutputDebugStringA("[dosbox-uwp]   GET_LOG_INTERFACE: log callback provided\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
        OutputDebugStringA("[dosbox-pure] SHUTDOWN requested\n");
        s_shutdownRequested = true;
        return 1;
    case RETRO_ENVIRONMENT_SET_VARIABLE:
    {
        auto* var = static_cast<const retro_variable*>(data);
        if (var && var->key)
        {
            const char* val = var->value ? var->value : "(default)";
            char kbuf[512];
            sprintf_s(kbuf, "[dosbox-uwp]   SET_VARIABLE: %s = %s\n", var->key, val);
            OutputDebugStringA(kbuf);
            s_optionValues[var->key] = (var->value ? var->value : "");
            s_optionValuesChanged = true;
            return 1;
        }
        return 0;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return 1;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        return 1;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return 1;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        auto* var = static_cast<retro_variable*>(data);
        if (var && var->key)
        {
            if (!strcmp(var->key, "dosbox_pure_menu_time"))
            {
                var->value = OVERRIDE_MENU_TIME;
                OutputDebugStringA("[dosbox-uwp]   GET_VARIABLE(menu_time) = -1\n");
                return 1;
            }

            auto it = s_optionValues.find(var->key);
            if (it != s_optionValues.end())
            {
                if (!it->second.empty())
                {
                    var->value = it->second.c_str();
#ifdef FRAME_TRACE
                    char kbuf[256];
                    sprintf_s(kbuf, "[dosbox-uwp]   GET_VARIABLE(%s) = %s\n", var->key, it->second.c_str());
                    OutputDebugStringA(kbuf);
#endif
                    return 1;
                }
                // Empty value means "use default" — fall through to return 0
            }
#ifdef FRAME_TRACE
            char kbuf[256];
            sprintf_s(kbuf, "[dosbox-uwp]   GET_VARIABLE(%s) = NOT FOUND\n", var->key);
            OutputDebugStringA(kbuf);
#endif
        }
        return 0;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    {
        bool* changed = static_cast<bool*>(data);
        if (changed)
        {
            *changed = s_optionValuesChanged;
            s_optionValuesChanged = false;
        }
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    {
        auto* av = static_cast<const retro_system_av_info*>(data);
        if (av && av->timing.fps > 0)
            s_targetFps = av->timing.fps;
        char buf[256];
        sprintf_s(buf, "[dosbox-uwp]   SET_SYSTEM_AV_INFO: %dx%d @ %.2fHz sample_rate=%.0f\n",
            av->geometry.base_width, av->geometry.base_height,
            av->timing.fps, av->timing.sample_rate);
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    {
        auto* geom = static_cast<const retro_game_geometry*>(data);
        char buf[256];
        sprintf_s(buf, "[dosbox-uwp]   SET_GEOMETRY: %dx%d aspect=%.2f\n",
            geom->base_width, geom->base_height, geom->aspect_ratio);
        OutputDebugStringA(buf);
        return 1;
    }
    default:
#ifdef FRAME_TRACE
        sprintf_s(buf, "[dosbox-uwp]   UNSUPPORTED env cmd=%d\n", cmd);
        OutputDebugStringA(buf);
#endif
        return 0;
    }
}

static int g_videoFrameCount = 0;

void RetroCore::retro_video(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0 || pitch == 0)
    {
        static int rejectCount = 0;
        rejectCount++;
        if ((rejectCount % 600) == 0)
        {
            char buf[128];
            sprintf_s(buf, "[dosbox-uwp] retro_video REJECTED #%d: data=%p w=%u h=%u pitch=%zu\n",
                rejectCount, data, w, h, pitch);
            OutputDebugStringA(buf);
        }
        return;
    }

    g_videoFrameCount++;

    // Log resolution changes
    {
        static unsigned prevW = 0, prevH = 0;
        if (w != prevW || h != prevH)
        {
            char buf[128];
            prevW = w; prevH = h;
            sprintf_s(buf, "[dosbox-uwp] retro_video RESOLUTION CHANGE: %ux%u (frame #%d)\n",
                w, h, g_videoFrameCount);
            OutputDebugStringA(buf);
        }
    }

    s_frameData = data;
    s_frameWidth = w;
    s_frameHeight = h;
    s_framePitch = (unsigned)pitch;
    s_frameValid = true;
}


size_t RetroCore::retro_audio(const int16_t* data, size_t frames)
{
    if (!data || frames == 0)
        return frames;

    if (s_audioOutput)
        s_audioOutput->Submit(data, (uint32_t)frames);

    return frames;
}



void RetroCore::retro_input_poll(void)
{
}

int16_t RetroCore::retro_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0) return 0;

    if (device == RETRO_DEVICE_KEYBOARD)
    {
        return (id < RETROK_LAST && s_keyboardState[id]) ? 1 : 0;
    }

    // WARNING: RETROK values (0-323) numerically overlap JOYPAD button IDs (0-15).
    // Previously this branch read from s_keyboardState, causing keyboard presses
    // to leak into JOYPAD queries. E.g. RETROK_RETURN=13 == RETRO_DEVICE_ID_JOYPAD_R2,
    // so Enter also registered as R2, which dosbox-pure maps to KBD_4 ("4").
    // Fixed by using a separate s_joypadState[] array — populated from physical
    // gamepad state in dosbox_uwpMain::Update().
    if (device == RETRO_DEVICE_JOYPAD)
    {
        if (id < 16 && s_joypadState[id])
            return 1;
        return 0;
    }

    if (device == RETRO_DEVICE_MOUSE)
    {
        switch (id)
        {
        case RETRO_DEVICE_ID_MOUSE_X: return s_mouseRelX;
        case RETRO_DEVICE_ID_MOUSE_Y: return s_mouseRelY;
#ifdef MOUSE_SUPPORT
        case RETRO_DEVICE_ID_MOUSE_LEFT: return s_mouseBtnLeft ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return s_mouseBtnRight ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_MIDDLE: return s_mouseBtnMiddle ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_WHEELUP: return (s_mouseWheel > 0) ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_WHEELDOWN: return (s_mouseWheel < 0) ? 1 : 0;
#else
        case RETRO_DEVICE_ID_MOUSE_LEFT: return 0;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return 0;
#endif
        default: return 0;
        }
    }

    if (device == RETRO_DEVICE_POINTER)
    {
        switch (id)
        {
        case RETRO_DEVICE_ID_POINTER_X: return (int16_t)((s_ptrX * 2.0f - 1.0f) * 0x7fff);
        case RETRO_DEVICE_ID_POINTER_Y: return (int16_t)((s_ptrY * 2.0f - 1.0f) * 0x7fff);
        case RETRO_DEVICE_ID_POINTER_PRESSED: return s_ptrDown ? 1 : 0;
        default: return 0;
        }
    }

    return 0;
}
