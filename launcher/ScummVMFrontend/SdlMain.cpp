// SdlMain.cpp — SDL_WinRTRunApp GL frontend for Xbox UWP
// Compiled WITHOUT /ZW (no C++/CX). Pure Win32 + SDL2 + Mesa WGL.
// Called from main.cpp via SDL_WinRTRunApp(sdl_main, NULL).

#include <windows.h>
#include <xaudio2.h>
#include <appmodel.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstdarg>
#include <cstdio>

#include "SDL2/SDL.h"

#include "libretro.h"
#include "LogHelper.h"

// ─── XAudio2 ring buffer ────────────────────────────────────────────────
static const int XA2_RING_FRAMES = 4096;
static const int XA2_MAX_SUBMIT = 2048;

struct Xa2Voice : IXAudio2VoiceCallback
{
    IXAudio2SourceVoice* voice = nullptr;
    CRITICAL_SECTION cs{};
    std::vector<int16_t> ring;
    size_t writePos = 0;
    size_t count = 0;
    size_t capacity = 0;

    void Init(IXAudio2* xa2, int rate)
    {
        InitializeCriticalSection(&cs);
        capacity = XA2_RING_FRAMES * 2;
        ring.resize(capacity);

        WAVEFORMATEX fx = {};
        fx.wFormatTag = WAVE_FORMAT_PCM;
        fx.nChannels = 2;
        fx.nSamplesPerSec = rate;
        fx.wBitsPerSample = 16;
        fx.nBlockAlign = 4;
        fx.nAvgBytesPerSec = rate * 4;

        xa2->CreateSourceVoice(&voice, &fx, 0, 1.0f, this);
        voice->Start(0);
        spdlog::info("[sdl] XAudio2 voice started ({}Hz)", rate);
    }

    void Push(const int16_t* data, size_t frames)
    {
        size_t samples = frames * 2;
        EnterCriticalSection(&cs);
        if (count + samples > capacity) { LeaveCriticalSection(&cs); return; }
        for (size_t i = 0; i < samples; i++)
            ring[(writePos + i) % capacity] = data[i];
        writePos = (writePos + samples) % capacity;
        count += samples;
        LeaveCriticalSection(&cs);
    }

    size_t Pull(int16_t* dst, size_t maxFrames)
    {
        EnterCriticalSection(&cs);
        size_t availFrames = count / 2;
        size_t frames = (availFrames < maxFrames) ? availFrames : maxFrames;
        size_t samples = frames * 2;
        size_t tail = (writePos + capacity - count) % capacity;
        for (size_t i = 0; i < samples; i++)
            dst[i] = ring[(tail + i) % capacity];
        count -= samples;
        LeaveCriticalSection(&cs);
        return frames;
    }

    // IXAudio2VoiceCallback — signatures from xaudio2.h
    STDMETHOD_(void, OnVoiceProcessingStart)(void*) {}
    STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32) {}
    STDMETHOD_(void, OnVoiceProcessingPassEnd)() {}
    STDMETHOD_(void, OnStreamEnd)() {}
    STDMETHOD_(void, OnBufferStart)(void*) {}
    STDMETHOD_(void, OnBufferEnd)(void*) {}
    STDMETHOD_(void, OnLoopEnd)(void*) {}
    STDMETHOD_(void, OnVoiceError)(void*, HRESULT) {}
};

static IXAudio2* g_xa2 = nullptr;
static IXAudio2MasteringVoice* g_masterVoice = nullptr;
static Xa2Voice g_xa2Voice;
static int g_audioRate = 48000;
static std::atomic<bool> g_audioThreadRunning{ false };

// ─── Data paths (resolved via Win32 — no /ZW, no WinRT) ──────────────────
static std::string g_systemDir;
static std::string g_saveDir;
static std::string g_libretroDir;

// ─── Core function pointers ─────────────────────────────────────────────
static struct {
    HMODULE dll = nullptr;

    void (__cdecl* init)(void) = nullptr;
    void (__cdecl* deinit)(void) = nullptr;
    void (__cdecl* run)(void) = nullptr;
    bool (__cdecl* load)(const retro_game_info*) = nullptr;
    void (__cdecl* unload)(void) = nullptr;

    void (__cdecl* set_environment)(retro_environment_t) = nullptr;
    void (__cdecl* set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (__cdecl* set_audio_batch)(retro_audio_sample_batch_t) = nullptr;
    void (__cdecl* set_input_poll)(retro_input_poll_t) = nullptr;
    void (__cdecl* set_input_state)(retro_input_state_t) = nullptr;

    unsigned (__cdecl* api_version)(void) = nullptr;
    void (__cdecl* get_system_info)(retro_system_info*) = nullptr;

    std::atomic<bool> loaded{ false };
    std::atomic<bool> running{ false };
    std::atomic<bool> shutdownRequested{ false };
    retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_RGB565;

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    std::atomic<bool> hwRenderAccepted{ false };
    retro_hw_context_reset_t contextReset = nullptr;

    std::atomic<bool> joypadState[16]{};
    std::atomic<int16_t> analogState[4]{};
} g_core;

// Forward decl for get_proc_address (needs to be free function, not lambda)
static retro_proc_address_t sdl_get_proc_address(const char* name);
static uintptr_t sdl_get_framebuffer(void);

#define LOAD_SYM(name, target) do { \
    g_core.target = decltype(g_core.target)(GetProcAddress(g_core.dll, "retro_" #name)); \
    if (!g_core.target) spdlog::warn("[sdl] retro_" #name " not found"); \
} while(0)

static bool LoadCoreDll()
{
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring exeDir;
    if (n > 0) {
        std::wstring p(buf, n);
        auto pos = p.find_last_of(L'\\');
        exeDir = (pos != std::wstring::npos) ? p.substr(0, pos) : L".";
    } else {
        exeDir = L".";
    }
    std::wstring corePath = exeDir + L"\\cores\\scummvm_libretro.dll";

    spdlog::info("[sdl] loading core: {}", corePath);
    g_core.dll = LoadLibraryW(corePath.c_str());
    if (!g_core.dll) {
        spdlog::error("[sdl] LoadLibrary FAILED gle={:08X}", GetLastError());
        return false;
    }

    LOAD_SYM(init, init);
    LOAD_SYM(deinit, deinit);
    LOAD_SYM(run, run);
    LOAD_SYM(load_game, load);
    LOAD_SYM(unload_game, unload);
    LOAD_SYM(set_environment, set_environment);
    LOAD_SYM(set_video_refresh, set_video_refresh);
    LOAD_SYM(set_audio_sample_batch, set_audio_batch);
    LOAD_SYM(set_input_poll, set_input_poll);
    LOAD_SYM(set_input_state, set_input_state);
    LOAD_SYM(api_version, api_version);
    LOAD_SYM(get_system_info, get_system_info);

    if (!g_core.init || !g_core.run || !g_core.load || !g_core.set_environment) {
        spdlog::error("[sdl] core missing critical exports");
        return false;
    }
    spdlog::info("[sdl] core loaded OK (api={})", g_core.api_version ? g_core.api_version() : -1);
    return true;
}
#undef LOAD_SYM

// ─── Retro GL callbacks (free functions for C function pointer compat) ───
static retro_proc_address_t sdl_get_proc_address(const char* name)
{
    auto p = (retro_proc_address_t)SDL_GL_GetProcAddress(name);
    static int s_procCount = 0;
    if (++s_procCount <= 10 || s_procCount % 50 == 0)
        spdlog::info("[sdl] get_proc_address #{}: {} = {}", s_procCount, name, (void*)p);
    return p;
}

static uintptr_t sdl_get_framebuffer(void)
{
    static int s_fbCount = 0;
    if (++s_fbCount <= 5)
        spdlog::info("[sdl] get_framebuffer #{}", s_fbCount);
    return 0;
}

// ─── Env handler ────────────────────────────────────────────────────────
static bool retro_env(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_ROTATION:
        return false;
    case RETRO_ENVIRONMENT_GET_OVERSCAN:
        return false;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool*)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        g_core.pixelFormat = *(retro_pixel_format*)data;
        spdlog::info("[sdl] SET_PIXEL_FORMAT={}", (int)g_core.pixelFormat);
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return false;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        return false;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    {
        auto cb = (retro_log_callback*)data;
        cb->log = [](enum retro_log_level level, const char* fmt, ...) {
            char buf[1024];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            switch (level) {
            case RETRO_LOG_ERROR: spdlog::error("[core] {}", buf); break;
            case RETRO_LOG_WARN:  spdlog::warn("[core] {}", buf); break;
            default:             spdlog::info("[core] {}", buf); break;
            }
        };
        return true;
    }
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        return true;
    case RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK:
        return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        auto var = (retro_variable*)data;
        if (!var || !var->key) return false;
        std::string key = var->key;
        if (key == "scummvm_video_hw_acceleration") {
            var->value = "enabled";
            spdlog::info("[sdl] GET_VARIABLE {} = enabled (GL)", key);
            return true;
        }
        if (key == "scummvm_gui_aspect_ratio") {
            var->value = "1";
            return true;
        }
        var->value = nullptr;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    {
        auto vars = (retro_core_option_definition*)data;
        if (vars) for (int i = 0; vars[i].key; i++)
            spdlog::info("[sdl] core option: {} = {}", vars[i].key,
                vars[i].default_value ? vars[i].default_value : "?");
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (data) *(unsigned*)data = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        if (data) *(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return false;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (data) *(const char**)data = g_systemDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (data) *(const char**)data = g_saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        if (data) *(const char**)data = g_libretroDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
        auto hw = (retro_hw_render_callback*)data;
        spdlog::info("[sdl] SET_HW_RENDER requested (ctx={})", (int)hw->context_type);
        if (hw->context_type == RETRO_HW_CONTEXT_OPENGL ||
            hw->context_type == RETRO_HW_CONTEXT_OPENGL_CORE)
        {
            hw->context_type = RETRO_HW_CONTEXT_OPENGL;
            hw->get_current_framebuffer = sdl_get_framebuffer;
            hw->get_proc_address = sdl_get_proc_address;
            g_core.contextReset = hw->context_reset;
            spdlog::info("[sdl] SET_HW_RENDER: context_reset={} get_proc={} get_fb={}",
                (void*)hw->context_reset, (void*)sdl_get_proc_address, (void*)sdl_get_framebuffer);
            if (g_core.window && g_core.glContext)
                SDL_GL_MakeCurrent(g_core.window, g_core.glContext);
            g_core.hwRenderAccepted = true;
            spdlog::info("[sdl] SET_HW_RENDER=ACCEPTED (OpenGL via SDL2/Mesa)");
            // Call context_reset immediately — GL context is already created and current.
            // The core needs this to initialize its GL resources (shaders, textures, FBOs).
            if (g_core.contextReset) {
                spdlog::info("[sdl] calling context_reset() NOW (in handler)");
                g_core.contextReset();
                spdlog::info("[sdl] context_reset() done");
            } else {
                spdlog::warn("[sdl] context_reset is NULL — core GL resources NOT initialized");
            }
            return true;
        }
        spdlog::warn("[sdl] SET_HW_RENDER REJECTED (ctx={})", (int)hw->context_type);
        return false;
    }
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if (data) *(int*)data = 3; // audio + video both enabled
        return true;
    default:
        return false;
    }
}

// ─── Video callback ─────────────────────────────────────────────────────
static int s_frameCount = 0;
static void retro_video_cb(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (++s_frameCount <= 5 || s_frameCount % 300 == 0) {
        spdlog::info("[sdl] retro_video_cb #{}: data={} w={} h={} pitch={} hw={}",
            s_frameCount, (uintptr_t)data, w, h, pitch, g_core.hwRenderAccepted.load());
    }
    if (data == RETRO_HW_FRAME_BUFFER_VALID && g_core.hwRenderAccepted.load()) {
        if (g_core.window) SDL_GL_SwapWindow(g_core.window);
        return;
    }
    // SW frame — log first few
    if (s_frameCount <= 5) {
        spdlog::info("[sdl] SW frame #{}: w={} h={} pitch={}", s_frameCount, w, h, pitch);
    }
}

// ─── Audio callback ─────────────────────────────────────────────────────
static size_t retro_audio_batch_cb(const int16_t* data, size_t frames)
{
    if (g_xa2Voice.voice) g_xa2Voice.Push(data, frames);
    return frames;
}

// ─── Input ──────────────────────────────────────────────────────────────
static void retro_input_poll_cb() {}

static int16_t retro_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    if (id < 16) return g_core.joypadState[id].load() ? 1 : 0;
    if (id == RETRO_DEVICE_ID_ANALOG_X && index == 0) return g_core.analogState[0].load();
    if (id == RETRO_DEVICE_ID_ANALOG_Y && index == 0) return g_core.analogState[1].load();
    if (id == RETRO_DEVICE_ID_ANALOG_X && index == 1) return g_core.analogState[2].load();
    if (id == RETRO_DEVICE_ID_ANALOG_Y && index == 1) return g_core.analogState[3].load();
    return 0;
}

// ─── Audio pull thread ──────────────────────────────────────────────────
static void AudioPullThread()
{
    spdlog::info("[sdl] audio pull thread started");
    std::vector<int16_t> buf(XA2_MAX_SUBMIT * 2);

    while (g_audioThreadRunning.load())
    {
        if (!g_xa2Voice.voice) { Sleep(10); continue; }

        XAUDIO2_VOICE_STATE state;
        g_xa2Voice.voice->GetState(&state);
        if (state.BuffersQueued >= 3) { Sleep(1); continue; }

        size_t frames = g_xa2Voice.Pull(buf.data(), XA2_MAX_SUBMIT);
        if (frames > 0) {
            XAUDIO2_BUFFER xbuf = {};
            xbuf.AudioBytes = (UINT32)(frames * 4);
            xbuf.pAudioData = (BYTE*)buf.data();
            g_xa2Voice.voice->SubmitSourceBuffer(&xbuf, nullptr);
        } else {
            Sleep(1);
        }
    }
    spdlog::info("[sdl] audio pull thread stopped");
}

// ─── SDL main (called by SDL_WinRTRunApp after CoreWindow creation) ─────
// Bridge: defined in main.cpp (compiled with /ZW) calling Bootstrap::Run()
extern "C" bool Bootstrap_Run();

extern "C" int sdl_main(int argc, char* argv[])
{
    spdlog::info("[sdl] sdl_main entered — " FRONTEND_VERSION);

    // Run Bootstrap (stages scummvm.zip, writes scummvm.ini, sets DataPaths).
    bool bootstrapped = Bootstrap_Run();
    spdlog::info("[sdl] Bootstrap::Run() = {}", bootstrapped);

    // Resolve LocalState from the log path already set by main.cpp
    // (main.cpp uses ApplicationData::Current->LocalFolder which is correct on Xbox).
    {
        std::wstring logPath = spdlog::g_logPath;
        std::wstring localState;
        if (!logPath.empty()) {
            // logPath = <LocalState>\scummvm-debug.log → strip filename
            auto pos = logPath.find_last_of(L'\\');
            if (pos != std::wstring::npos)
                localState = logPath.substr(0, pos);
        }
        if (localState.empty()) {
            // Fallback: LOCALAPPDATA on Xbox already points inside package\AC
            // LocalState is a sibling: go up from \AC to package root, then \LocalState
            wchar_t la[MAX_PATH] = { 0 };
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH - 1);
            if (n > 0 && n < MAX_PATH) {
                std::wstring s(la);
                auto ac = s.find(L"\\AC");
                if (ac != std::wstring::npos && ac + 3 == s.size())
                    localState = s.substr(0, ac) + L"\\LocalState";
                else
                    localState = s + L"\\LocalState";
            }
        }
        if (!localState.empty()) {
            g_systemDir = spdlog::detail::utf8_from_wide((localState + L"\\system").c_str());
            g_saveDir = spdlog::detail::utf8_from_wide((localState + L"\\saves").c_str());
        }
        // libretro path = exe directory
        wchar_t buf[MAX_PATH] = { 0 };
        DWORD m = GetModuleFileNameW(NULL, buf, MAX_PATH);
        if (m > 0) {
            std::wstring p(buf, m);
            auto pos = p.find_last_of(L'\\');
            std::wstring dir = (pos != std::wstring::npos) ? p.substr(0, pos) : L".";
            g_libretroDir = spdlog::detail::utf8_from_wide(dir.c_str());
        }
    }

    // Create dirs if needed
    {
        std::wstring ws(g_systemDir.begin(), g_systemDir.end());
        CreateDirectoryW(ws.c_str(), NULL);
        ws = std::wstring(g_saveDir.begin(), g_saveDir.end());
        CreateDirectoryW(ws.c_str(), NULL);
    }

    spdlog::info("[sdl] SYSTEM_DIR={}", g_systemDir);
    spdlog::info("[sdl] SAVE_DIR={}", g_saveDir);
    spdlog::info("[sdl] LIBRETRO_DIR={}", g_libretroDir);

    // SDL Init
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        spdlog::error("[sdl] SDL_Init FAILED: {}", SDL_GetError());
        return 1;
    }
    spdlog::info("[sdl] SDL2 initialized OK");

    // GL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // Window (FULLSCREEN_DESKTOP = auto-stretch)
    g_core.window = SDL_CreateWindow(
        "ScummVM",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        0, 0,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!g_core.window) {
        spdlog::error("[sdl] SDL_CreateWindow FAILED: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    spdlog::info("[sdl] SDL_CreateWindow OK (FULLSCREEN_DESKTOP)");

    // GL Context
    g_core.glContext = SDL_GL_CreateContext(g_core.window);
    if (!g_core.glContext) {
        spdlog::warn("[sdl] GL 4.6 compat FAILED: {}", SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        g_core.glContext = SDL_GL_CreateContext(g_core.window);
    }
    if (!g_core.glContext) {
        spdlog::error("[sdl] SDL_GL_CreateContext FAILED: {}", SDL_GetError());
        SDL_DestroyWindow(g_core.window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(g_core.window, g_core.glContext);
    {
        typedef const unsigned char* (__stdcall* FN_glGetString)(unsigned);
        auto _glGetString = (FN_glGetString)SDL_GL_GetProcAddress("glGetString");
        spdlog::info("[sdl] GL context active — vendor={} renderer={}",
            _glGetString ? (const char*)_glGetString(0x1F00) : "?",   // GL_VENDOR=0x1F00
            _glGetString ? (const char*)_glGetString(0x1F01) : "?");  // GL_RENDERER=0x1F01
    }

    // XAudio2 — XAudio2Create already initializes; no separate Initialize() call.
    // Note: COM Release() not available without /ZW — cleanup handled at process exit.
    {
        HRESULT hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (SUCCEEDED(hr)) {
            g_xa2->CreateMasteringVoice(&g_masterVoice);
            g_xa2Voice.Init(g_xa2, g_audioRate);
            spdlog::info("[sdl] XAudio2 OK");
        } else {
            spdlog::warn("[sdl] XAudio2Create FAILED: 0x{:08X}", (unsigned)hr);
        }
    }

    // Load core DLL
    if (!LoadCoreDll()) {
        spdlog::error("[sdl] core load FAILED");
        SDL_Quit(); // handles GL context + window teardown internally
        return 1;
    }

    // Init core
    g_core.set_environment(retro_env);
    g_core.set_video_refresh(retro_video_cb);
    g_core.set_audio_batch(retro_audio_batch_cb);
    g_core.set_input_poll(retro_input_poll_cb);
    g_core.set_input_state(retro_input_state_cb);

    // context_reset is already called in the SET_HW_RENDER handler above
    // (GL context is current at that point).

    g_core.init();
    spdlog::info("[sdl] retro_init OK");

    // Load game (no-game = ScummVM GUI)
    spdlog::info("[sdl] calling retro_load_game(NULL) for no-game GUI");
    if (!g_core.load(nullptr)) {
        spdlog::error("[sdl] retro_load_game FAILED");
    } else {
        spdlog::info("[sdl] retro_load_game OK");
        g_core.loaded = true;
    }
    g_core.running = true;

    // Audio pull thread
    g_audioThreadRunning = true;
    std::thread audioThread(AudioPullThread);
    audioThread.detach();

    // Gamepad
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) {
                spdlog::info("[sdl] gamepad: {}", SDL_GameControllerName(pad));
                break;
            }
        }
    }

    spdlog::info("[sdl] entering main loop");

    // Main loop
    bool quit = false;
    int runCount = 0;
    while (!quit)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            switch (ev.type)
            {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                bool down = (ev.type == SDL_KEYDOWN);
                switch (ev.key.keysym.sym) {
                case SDLK_UP:     g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_UP].store(down); break;
                case SDLK_DOWN:   g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_DOWN].store(down); break;
                case SDLK_LEFT:   g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_LEFT].store(down); break;
                case SDLK_RIGHT:  g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_RIGHT].store(down); break;
                case SDLK_z:      g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_A].store(down); break;
                case SDLK_x:      g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_B].store(down); break;
                case SDLK_a:      g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_X].store(down); break;
                case SDLK_s:      g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_Y].store(down); break;
                case SDLK_q:      g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_L].store(down); break;
                case SDLK_w:      g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_R].store(down); break;
                case SDLK_RETURN: g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_START].store(down); break;
                case SDLK_TAB:    g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_SELECT].store(down); break;
                default: break;
                }
                break;
            }
            case SDL_CONTROLLERAXISMOTION:
            {
                int16_t val = ev.caxis.value;
                switch (ev.caxis.axis) {
                case SDL_CONTROLLER_AXIS_LEFTX:  g_core.analogState[0].store(val); break;
                case SDL_CONTROLLER_AXIS_LEFTY:  g_core.analogState[1].store(val); break;
                case SDL_CONTROLLER_AXIS_RIGHTX: g_core.analogState[2].store(val); break;
                case SDL_CONTROLLER_AXIS_RIGHTY: g_core.analogState[3].store(val); break;
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            {
                bool down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:             g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_A].store(down); break;
                case SDL_CONTROLLER_BUTTON_B:             g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_B].store(down); break;
                case SDL_CONTROLLER_BUTTON_X:             g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_X].store(down); break;
                case SDL_CONTROLLER_BUTTON_Y:             g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_Y].store(down); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_L].store(down); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_R].store(down); break;
                case SDL_CONTROLLER_BUTTON_BACK:          g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_SELECT].store(down); break;
                case SDL_CONTROLLER_BUTTON_START:         g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_START].store(down); break;
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:     g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_L3].store(down); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_R3].store(down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:       g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_UP].store(down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_DOWN].store(down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_LEFT].store(down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    g_core.joypadState[RETRO_DEVICE_ID_JOYPAD_RIGHT].store(down); break;
                }
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad) {
                    pad = SDL_GameControllerOpen(ev.cdevice.which);
                    if (pad) spdlog::info("[sdl] gamepad connected: {}", SDL_GameControllerName(pad));
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                break;
            }
        }

        if (quit) break;

        if (g_core.loaded && g_core.running) {
            g_core.run();
            if (++runCount <= 3 || runCount % 300 == 0)
                spdlog::info("[sdl] retro_run #{}", runCount);
        }

        if (g_core.shutdownRequested.load()) {
            spdlog::info("[sdl] core requested SHUTDOWN");
            quit = true;
        }
    }

    // Cleanup
    spdlog::info("[sdl] shutting down...");
    g_audioThreadRunning = false;
    Sleep(100);

    if (g_core.loaded) {
        g_core.unload();
        g_core.deinit();
    }
    if (g_core.dll) FreeLibrary(g_core.dll);
    if (g_xa2Voice.voice) {
        g_xa2Voice.voice->Stop(0, 0);
        g_xa2Voice.voice->FlushSourceBuffers();
        // Release not available without /ZW; process exit handles cleanup
    }
    if (g_masterVoice) { /* ditto */ g_masterVoice = nullptr; }
    if (g_xa2) { /* ditto */ g_xa2 = nullptr; }
    if (g_core.glContext) SDL_GL_DeleteContext(g_core.glContext);
    if (g_core.window) SDL_DestroyWindow(g_core.window);
    SDL_Quit();
    spdlog::info("[sdl] cleanup done");
    return 0;
}
