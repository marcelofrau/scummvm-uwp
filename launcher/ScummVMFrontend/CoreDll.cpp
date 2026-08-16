#include "pch.h"
#include "CoreDll.h"
#include <string>

namespace CoreDll
{
    static HMODULE s_hCore = nullptr;
    static std::wstring s_path;

    retro_set_environment_t retro_set_environment = nullptr;
    retro_set_video_refresh_t retro_set_video_refresh = nullptr;
    retro_set_audio_sample_t retro_set_audio_sample = nullptr;
    retro_set_audio_sample_batch_t retro_set_audio_sample_batch = nullptr;
    retro_set_input_poll_t retro_set_input_poll = nullptr;
    retro_set_input_state_t retro_set_input_state = nullptr;
    retro_init_t retro_init = nullptr;
    retro_deinit_t retro_deinit = nullptr;
    retro_load_game_t retro_load_game = nullptr;
    retro_unload_game_t retro_unload_game = nullptr;
    retro_run_t retro_run = nullptr;
    retro_reset_t retro_reset = nullptr;
    retro_serialize_t retro_serialize = nullptr;
    retro_unserialize_t retro_unserialize = nullptr;
    retro_serialize_size_t retro_serialize_size = nullptr;
    retro_set_controller_port_device_t retro_set_controller_port_device = nullptr;
    retro_get_region_t retro_get_region = nullptr;
    retro_get_system_info_t retro_get_system_info = nullptr;
    retro_get_system_av_info_t retro_get_system_av_info = nullptr;
    retro_cheat_reset_t retro_cheat_reset = nullptr;
    retro_cheat_set_t retro_cheat_set = nullptr;
    retro_get_memory_data_t retro_get_memory_data = nullptr;
    retro_get_memory_size_t retro_get_memory_size = nullptr;
    retro_api_version_t retro_api_version = nullptr;

    template <typename Fn>
    static bool LoadSymbol(HMODULE h, const char* name, Fn& out)
    {
        FARPROC p = GetProcAddress(h, name);
        if (!p)
        {
            spdlog::error("[CoreDll] GetProcAddress FAILED: {}", name);
            return false;
        }
        out = reinterpret_cast<Fn>(p);
        return true;
    }

    bool Load(const wchar_t* dllPath)
    {
        if (s_hCore)
            return true;

        spdlog::info("[CoreDll] LoadLibrary: {}", dllPath);
        HMODULE h = LoadLibraryExW(dllPath, nullptr, 0);
        if (!h)
        {
            spdlog::error("[CoreDll] LoadLibrary FAILED, GetLastError=0x{:08X}", (unsigned)GetLastError());
            return false;
        }

        bool ok =
            LoadSymbol(h, "retro_set_environment", retro_set_environment) &&
            LoadSymbol(h, "retro_set_video_refresh", retro_set_video_refresh) &&
            LoadSymbol(h, "retro_set_audio_sample", retro_set_audio_sample) &&
            LoadSymbol(h, "retro_set_audio_sample_batch", retro_set_audio_sample_batch) &&
            LoadSymbol(h, "retro_set_input_poll", retro_set_input_poll) &&
            LoadSymbol(h, "retro_set_input_state", retro_set_input_state) &&
            LoadSymbol(h, "retro_init", retro_init) &&
            LoadSymbol(h, "retro_deinit", retro_deinit) &&
            LoadSymbol(h, "retro_load_game", retro_load_game) &&
            LoadSymbol(h, "retro_unload_game", retro_unload_game) &&
            LoadSymbol(h, "retro_run", retro_run) &&
            LoadSymbol(h, "retro_reset", retro_reset) &&
            LoadSymbol(h, "retro_serialize", retro_serialize) &&
            LoadSymbol(h, "retro_unserialize", retro_unserialize) &&
            LoadSymbol(h, "retro_serialize_size", retro_serialize_size) &&
            LoadSymbol(h, "retro_set_controller_port_device", retro_set_controller_port_device) &&
            LoadSymbol(h, "retro_get_region", retro_get_region) &&
            LoadSymbol(h, "retro_get_system_info", retro_get_system_info) &&
            LoadSymbol(h, "retro_get_system_av_info", retro_get_system_av_info) &&
            LoadSymbol(h, "retro_cheat_reset", retro_cheat_reset) &&
            LoadSymbol(h, "retro_cheat_set", retro_cheat_set) &&
            LoadSymbol(h, "retro_get_memory_data", retro_get_memory_data) &&
            LoadSymbol(h, "retro_get_memory_size", retro_get_memory_size) &&
            LoadSymbol(h, "retro_api_version", retro_api_version);

        if (!ok)
        {
            spdlog::error("[CoreDll] core exports incomplete, unloading");
            FreeLibrary(h);
            h = nullptr;
            return false;
        }

        s_hCore = h;
        s_path = dllPath;
        spdlog::info("[CoreDll] core loaded OK (api_version={})", (unsigned)retro_api_version());
        return true;
    }

    void Unload()
    {
        if (s_hCore)
        {
            FreeLibrary(s_hCore);
            s_hCore = nullptr;
            s_path.clear();
        }
    }

    bool IsLoaded()
    {
        return s_hCore != nullptr;
    }

    const wchar_t* GetPath()
    {
        return s_path.c_str();
    }
}
