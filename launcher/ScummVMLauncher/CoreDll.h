#pragma once

#include <windows.h>
#include <string>
#include "libretro.h"

// This libretro.h (bundled with the ScummVM core) defines the *callback*
// types but not the core API export types, so declare those locally.
using retro_init_t = void (RETRO_CALLCONV*)();
using retro_deinit_t = void (RETRO_CALLCONV*)();
using retro_load_game_t = bool (RETRO_CALLCONV*)(const retro_game_info*);
using retro_unload_game_t = void (RETRO_CALLCONV*)();
using retro_run_t = void (RETRO_CALLCONV*)();
using retro_reset_t = void (RETRO_CALLCONV*)();
using retro_serialize_t = bool (RETRO_CALLCONV*)(void*, size_t);
using retro_unserialize_t = bool (RETRO_CALLCONV*)(const void*, size_t);
using retro_serialize_size_t = size_t (RETRO_CALLCONV*)();
using retro_set_controller_port_device_t = void (RETRO_CALLCONV*)(unsigned, unsigned);
using retro_get_region_t = unsigned (RETRO_CALLCONV*)();
using retro_get_system_info_t = void (RETRO_CALLCONV*)(retro_system_info*);
using retro_get_system_av_info_t = void (RETRO_CALLCONV*)(retro_system_av_info*);
using retro_cheat_reset_t = void (RETRO_CALLCONV*)();
using retro_cheat_set_t = void (RETRO_CALLCONV*)(unsigned, bool, const char*);
using retro_get_memory_data_t = void* (RETRO_CALLCONV*)(unsigned);
using retro_get_memory_size_t = size_t (RETRO_CALLCONV*)(unsigned);
using retro_api_version_t = unsigned (RETRO_CALLCONV*)();
using retro_set_environment_t = void (RETRO_CALLCONV*)(retro_environment_t);
using retro_set_video_refresh_t = void (RETRO_CALLCONV*)(retro_video_refresh_t);
using retro_set_audio_sample_t = void (RETRO_CALLCONV*)(retro_audio_sample_t);
using retro_set_audio_sample_batch_t = void (RETRO_CALLCONV*)(retro_audio_sample_batch_t);
using retro_set_input_poll_t = void (RETRO_CALLCONV*)(retro_input_poll_t);
using retro_set_input_state_t = void (RETRO_CALLCONV*)(retro_input_state_t);

// Dynamic loader for the libretro core (scummvm_libretro.dll).
// All retro_* API symbols are function pointers defined in CoreDll.cpp;
// code that wants to call them can `using namespace CoreDll;` and use the
// plain retro_* names, keeping call sites unchanged vs a static core.
namespace CoreDll
{
    bool Load(const wchar_t* dllPath);
    void Unload();
    bool IsLoaded();
    // Full path to the loaded core DLL (for RETRO_ENVIRONMENT_GET_LIBRETRO_PATH).
    const wchar_t* GetPath();

    extern retro_set_environment_t retro_set_environment;
    extern retro_set_video_refresh_t retro_set_video_refresh;
    extern retro_set_audio_sample_t retro_set_audio_sample;
    extern retro_set_audio_sample_batch_t retro_set_audio_sample_batch;
    extern retro_set_input_poll_t retro_set_input_poll;
    extern retro_set_input_state_t retro_set_input_state;
    extern retro_init_t retro_init;
    extern retro_deinit_t retro_deinit;
    extern retro_load_game_t retro_load_game;
    extern retro_unload_game_t retro_unload_game;
    extern retro_run_t retro_run;
    extern retro_reset_t retro_reset;
    extern retro_serialize_t retro_serialize;
    extern retro_unserialize_t retro_unserialize;
    extern retro_serialize_size_t retro_serialize_size;
    extern retro_set_controller_port_device_t retro_set_controller_port_device;
    extern retro_get_region_t retro_get_region;
    extern retro_get_system_info_t retro_get_system_info;
    extern retro_get_system_av_info_t retro_get_system_av_info;
    extern retro_cheat_reset_t retro_cheat_reset;
    extern retro_cheat_set_t retro_cheat_set;
    extern retro_get_memory_data_t retro_get_memory_data;
    extern retro_get_memory_size_t retro_get_memory_size;
    extern retro_api_version_t retro_api_version;
}
