#include "pch.h"
#include "dosbox_uwpMain.h"
#include "Content/SettingsManager.h"
#include "libretro.h"
#include "dosbox_pure_sta.h"
#include "Common\DirectXHelper.h"
#include <cmath>
#include <sstream>
#include <wincodec.h>
#include <wrl/client.h>
#include <SDL.h>

#ifdef XB_INSPECTOR_ENABLED
#include <xray/inspector.hpp>
// File-scope variables expostas ao Lua REPL do XB-Inspector
struct PerfStats {
    double frame_ms, poll_ms, hud_ms, render_ms, total_ms;
    double target_fps;
    float fps;
    int audio_queued, audio_underruns;
    long long audio_produced, audio_consumed;
    char rom_name[256];
};
static PerfStats s_perf{};
static float s_debug_fps = 0.0f;
static double s_debug_target_fps = 60.0;
static double s_debug_frame_ms = 0.0;
static double s_debug_poll_ms = 0.0;
static double s_debug_hud_ms = 0.0;
static double s_debug_render_ms = 0.0;
static double s_debug_total_ms = 0.0;
static char s_rom_name[256] = "(none)";
static std::atomic<bool> s_paused{false};
#endif

using namespace dosbox_uwp;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Windows::System::Profile;
using namespace Windows::ApplicationModel::Core;
using namespace Concurrency;

// Copy a file to LocalFolder\temp\ via Win32 kernel copy (0 heap allocation).
// Returns dest path on success, empty string on failure.
static std::wstring CopyFileToTemp(const std::wstring& srcPath)
{
    auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
    std::wstring tempDir = std::wstring(localFolder->Path->Data()) + L"\\temp";
    CreateDirectoryFromAppW(tempDir.c_str(), nullptr);

    auto slash = srcPath.find_last_of(L'\\');
    std::wstring filename = (slash != std::wstring::npos) ? srcPath.substr(slash + 1) : srcPath;
    std::wstring destPath = tempDir + L"\\" + filename;

    std::string srcUtf8(srcPath.begin(), srcPath.end());
    std::string destUtf8(destPath.begin(), destPath.end());

    BOOL ok = CopyFileFromAppW(srcPath.c_str(), destPath.c_str(), FALSE);
    if (!ok)
    {
        spdlog::error("[CopyFileToTemp] CopyFileFromAppW failed '{}' -> '{}', error={}",
            srcUtf8, destUtf8, GetLastError());
        return {};
    }

    spdlog::info("[CopyFileToTemp] '{}' -> '{}'", srcUtf8, destUtf8);
    return destPath;
}

dosbox_uwpMain::dosbox_uwpMain(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
    , m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f }
    , m_hasController(false)
{
    QueryPerformanceFrequency(&m_qpcFreq);
    m_deviceResources->RegisterDeviceNotify(this);

    m_retroScreen = std::unique_ptr<RetroScreenRenderer>(new RetroScreenRenderer(m_deviceResources));
    m_retroScreen->CreateDeviceDependentResources();

    m_retroCore = std::unique_ptr<RetroCore>(new RetroCore());

    m_xaudio2 = std::unique_ptr<XAudio2Output>(new XAudio2Output());
    if (m_xaudio2->Initialize())
    {
        RetroCore::SetAudioOutput(m_xaudio2.get());
        OutputDebugStringA("[dosbox-uwp] XAudio2: initialized\n");
    }
    else
    {
        OutputDebugStringA("[dosbox-uwp] XAudio2: FAILED to initialize\n");
    }

    m_sdlInput = std::unique_ptr<SdlInput>(new SdlInput());
    if (m_sdlInput->Initialize())
    {
        m_hasController = m_sdlInput->HasController();
        char buf[128];
        sprintf_s(buf, "SDL: controller=%s\n",
            m_hasController ? "CONNECTED" : "NONE (SPACE=btnA)");
        OutputDebugStringA(buf);
    }

    // Initialize settings manager (loads dosbox-pure-settings.json)
    {
        std::string settingsDir;
        auto family = AnalyticsInfo::VersionInfo->DeviceFamily;
        std::wstring fw(family->Data());
        if (fw == L"Windows.Xbox")
        {
            CreateDirectoryA("E:\\dosbox", NULL);
            settingsDir = "E:\\dosbox";
        }
        else
        {
            char tmp[MAX_PATH];
            if (GetTempPathA(MAX_PATH, tmp) != 0)
            {
                std::string dir(tmp);
                if (!dir.empty() && dir.back() == '\\')
                    dir.pop_back();
                CreateDirectoryA((dir + "\\dosbox-pure").c_str(), NULL);
                settingsDir = dir + "\\dosbox-pure";
            }
        }
        if (!settingsDir.empty())
        {
            SettingsManager::Initialize(settingsDir + "\\dosbox-pure-settings.json");
            m_menu.RefreshMenuItems();
        }
    }

#ifdef XB_INSPECTOR_ENABLED
    {
        std::string logPath;
        auto family = AnalyticsInfo::VersionInfo->DeviceFamily;
        std::wstring fw(family->Data());
        if (fw == L"Windows.Xbox")
        {
            CreateDirectoryA("E:\\dosbox", NULL);
            CreateDirectoryA("E:\\dosbox\\logs", NULL);
            logPath = "E:\\dosbox\\logs\\";
        }
        else
        {
            char tmp[MAX_PATH];
            if (GetTempPathA(MAX_PATH, tmp) != 0)
            {
                std::string dir(tmp);
                if (!dir.empty() && dir.back() == '\\')
                    dir.pop_back();
                CreateDirectoryA((dir + "\\dosbox-pure").c_str(), NULL);
                CreateDirectoryA((dir + "\\dosbox-pure\\logs").c_str(), NULL);
                logPath = dir + "\\dosbox-pure\\logs\\";
            }
        }
        xb::Xray::set_log_path(logPath.c_str());
        xb::Xray::start("dosbox-uwp");
        {
            auto family = Windows::System::Profile::AnalyticsInfo::VersionInfo->DeviceFamily;
            std::wstring fw(family->Data());
            char buf[64];
            WideCharToMultiByte(CP_UTF8, 0, fw.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
            xb::Xray::set_device_family(buf);
        }
        xb::Xray::bind("audio_queued", (long*)XAudio2Output::QueuedFramesPtr());
        xb::Xray::bind("audio_produced", (long*)XAudio2Output::TotalProducedPtr());
        xb::Xray::bind("audio_consumed", (long*)XAudio2Output::TotalConsumedPtr());
        xb::Xray::bind("fps", &s_debug_fps);
        xb::Xray::bind("target_fps", &s_debug_target_fps);
        xb::Xray::bind("frame_ms", &s_debug_frame_ms);
        xb::Xray::bind("poll_ms", &s_debug_poll_ms);
        xb::Xray::bind("hud_ms", &s_debug_hud_ms);
        xb::Xray::bind("render_ms", &s_debug_render_ms);
        xb::Xray::bind("total_ms", &s_debug_total_ms);
        xb::Xray::bind_string("rom_name", s_rom_name, sizeof(s_rom_name));

        // Grouped struct — demonstrates bind_struct API
        static const xb::struct_field perf_fields[] = {
            xb::field("frame_ms",     &PerfStats::frame_ms),
            xb::field("poll_ms",      &PerfStats::poll_ms),
            xb::field("hud_ms",       &PerfStats::hud_ms),
            xb::field("render_ms",    &PerfStats::render_ms),
            xb::field("total_ms",     &PerfStats::total_ms),
            xb::field("target_fps",   &PerfStats::target_fps),
            xb::field("fps",          &PerfStats::fps),
            xb::field("audio_queued",   &PerfStats::audio_queued),
            xb::field("audio_underruns",&PerfStats::audio_underruns),
            xb::field("audio_produced", &PerfStats::audio_produced),
            xb::field("audio_consumed", &PerfStats::audio_consumed),
            xb::field("rom_name",       &PerfStats::rom_name),
        };
        xb::Xray::bind_struct("perf", &s_perf, perf_fields,
            sizeof(perf_fields) / sizeof(perf_fields[0]));
        xb::Xray::set_on_terminate([]() {
            Windows::ApplicationModel::Core::CoreApplication::Exit();
        });
        xb::Xray::set_on_pause([]() {
            s_paused = true;
            while (s_paused) {
                Sleep(100);
                xb::Xray::update();
            }
        });
        xb::Xray::set_on_continue([]() {
            s_paused = false;
        });
        spdlog::info("{}", "--- XB-Inspector ---");
        spdlog::info("File log dir: {}", logPath.empty() ? "(none)" : logPath);
        uint16_t bp = xb::Xray::bound_port();
        spdlog::info("ODS: ON  |  TCP port: {} ({})  |  REPL: ON", bp, bp ? "OK" : "BIND FAILED");

        if (bp == 0) {
            spdlog::error("[xray] TCP bind failed, continuing without inspector");
        }
        else {
            spdlog::info("[xray] TCP port {} bound", bp);
        }
    }
#endif

    // Wire FrontendMenu callbacks
    m_menu.onOpenFile = [this]() { m_requestFilePicker = true; };
    m_menu.m_fileBrowser.onFileSelected = [this](const std::wstring& path) {
        spdlog::info("[FileBrowser] onFileSelected: '{}'", std::string(path.begin(), path.end()));
        m_menu.m_fileBrowser.Close();
        ActivateLoadingScreen();

        // Copy to LocalFolder\temp\ via kernel-level copy (no memory allocation).
        std::wstring srcPath = path;
        CleanupTempFile();
        create_task([srcPath]() -> std::wstring
        {
            return CopyFileToTemp(srcPath);
        }).then([this, srcPath](std::wstring tempPath)
        {
            if (tempPath.empty())
            {
                spdlog::error("[FileBrowser] Copy failed, re-opening picker");
                m_loadingActive = false;
                m_requestFilePicker = true;
                return;
            }
            QueueLoadRom(tempPath, {}, srcPath);
        });
    };
    m_menu.onOpenPuremenu = [this]() {
        if (m_retroCore && m_retroCore->IsLoaded()) {
            m_menu.Hide();
            m_retroCore->ToggleOSD();
        }
    };
    m_menu.onFileSelectedHistory = [this](const std::wstring& path) {
        spdlog::info("[History] Loading '{}'", std::string(path.begin(), path.end()));
        ActivateLoadingScreen();

        // History stores original path (e.g. E:\Games\Doom\doom.exe).
        // Re-copy to LocalFolder\temp\ just like file browser does.
        std::wstring srcPath = path;
        CleanupTempFile();
        create_task([srcPath]() -> std::wstring
        {
            return CopyFileToTemp(srcPath);
        }).then([this, srcPath](std::wstring tempPath)
        {
            if (tempPath.empty())
            {
                spdlog::error("[History] Copy failed from original path, file may be missing");
                m_loadingActive = false;
                m_menu.Show();
                m_menu.RebuildItems();
                return;
            }
            QueueLoadRom(tempPath, {}, srcPath);
        });
    };
    m_menu.onExit = []() {
        CoreApplication::Exit();
    };
    m_menu.onBeep = [this]() {
        // Realistic 90s PC POST beep: 1kHz square wave, 120ms, envelope
        const int sampleRate = 44100;
        const int numFrames = (int)(sampleRate * 0.12f);
        std::vector<int16_t> samples((size_t)numFrames * 2);
        const int halfPeriod = sampleRate / (1000 * 2);
        for (int i = 0; i < numFrames; i++)
        {
            float t = (float)i / sampleRate;
            int16_t v = ((i % (halfPeriod * 2)) < halfPeriod) ? 16000 : -16000;
            float env = 1.0f;
            if (t < 0.002f) env = t / 0.002f;
            else if (t > 0.115f) env = (0.12f - t) / 0.005f;
            v = (int16_t)(v * env);
            samples[i * 2] = v;
            samples[i * 2 + 1] = v;
        }
        m_xaudio2->Submit(samples.data(), numFrames);
    };
    m_menu.onOptionChanged = [this](const char* key, const char* value) {
        if (!strcmp(key, "frontend_vsync"))
        {
            bool enabled = !strcmp(value, "On");
            m_deviceResources->SetVSync(enabled);
            spdlog::info("[Settings] VSync = {} (syncInterval={})", value, m_deviceResources->GetSyncInterval());
        }
        else if (!strcmp(key, "frontend_scaler"))
        {
            D2D1_BITMAP_INTERPOLATION_MODE mode = !strcmp(value, "Nearest")
                ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
            m_retroScreen->SetInterpolationMode(mode);
            spdlog::info("[Settings] Scaler = {} (d2dMode={})", value, (int)mode);
        }
    };

        // Apply frontend-only settings at startup
        // VSync OFF by default — audio-driven pacing handles timing.
        // VSync ON causes stutter: 70fps core vs 60Hz display = every ~6th frame
        // delayed by extra 16.7ms VSync wait. Audio backpressure paces correctly.
        {
            auto vsync = SettingsManager::GetOption("frontend_vsync", "Off");
            m_deviceResources->SetVSync(!vsync.empty() && vsync != "Off");
        auto scaler = SettingsManager::GetOption("frontend_scaler", "Bilinear");
        D2D1_BITMAP_INTERPOLATION_MODE mode = (scaler == "Nearest")
            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
        m_retroScreen->SetInterpolationMode(mode);
    }

    BootCore();
}

dosbox_uwpMain::~dosbox_uwpMain()
{
#ifdef XB_INSPECTOR_ENABLED
    xb::Xray::stop();
#endif
    CleanupTempFile();
    m_retroCore->Shutdown();
    m_deviceResources->RegisterDeviceNotify(nullptr);
    // DoPacingSleep removed — visual frame rate (Present) handles timing; audio pacing via DRC
}

void dosbox_uwpMain::BootCore()
{
    if (!m_retroCore->Init())
    {
        OutputDebugStringA("[dosbox-uwp] retro_init FAILED\n");
        return;
    }

    auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
    std::wstring basePath = localFolder->Path->Data();

    _wmkdir((basePath + L"\\saves").c_str());
    _wmkdir((basePath + L"\\config").c_str());
    OutputDebugStringA("[dosbox-uwp] LocalFolder dirs: saves/, config/\n");

    OutputDebugStringA("[dosbox-uwp] Core initialized OK\n");
    OutputDebugStringA("[dosbox-uwp] Keyboard mapping active: VirtualKey->RETROK_\n");

    m_retroRunning = true;
}

void dosbox_uwpMain::LoadRom(const std::wstring& path, std::vector<uint8_t> romData, const std::wstring& originalPath)
{
    if (!m_retroCore->IsInitialized())
    {
        if (!m_retroCore->Init())
        {
            OutputDebugStringA("[dosbox-uwp] retro_init FAILED\n");
            return;
        }
    }
    else
    {
        OutputDebugStringA("[dosbox-uwp] LoadRom: core already initialized, skipping retro_init\n");
    }

    {
        char buf[512];
        int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string pathUtf8(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathUtf8[0], len, nullptr, nullptr);
        sprintf_s(buf, "[dosbox-uwp] LoadRom path=%s data=%zu\n", pathUtf8.c_str(), romData.size());
        OutputDebugStringA(buf);

        // Extract filename for xray binding
        auto slash = pathUtf8.find_last_of("/\\");
        std::string fname = (slash != std::string::npos) ? pathUtf8.substr(slash + 1) : pathUtf8;
#ifdef XB_INSPECTOR_ENABLED
        strncpy_s(s_rom_name, fname.c_str(), sizeof(s_rom_name) - 1);
        s_rom_name[sizeof(s_rom_name) - 1] = '\0';
        strncpy_s(s_perf.rom_name, fname.c_str(), sizeof(s_perf.rom_name) - 1);
        s_perf.rom_name[sizeof(s_perf.rom_name) - 1] = '\0';
#endif
    }

    m_currentTempPath = path;

    if (m_retroCore->LoadGame(path, romData))
    {
        OutputDebugStringA("[dosbox-uwp] Game loaded OK\n");
        m_retroRunning = true;
        m_clearColor = DirectX::Colors::Black;
        m_menu.Hide();

        // Push all saved option values to core so check_variables() picks them up
        SettingsManager::ForEachOption([](const char* key, const char* value) {
            if (strncmp(key, "frontend_", 9) != 0) // skip frontend-only options
                RetroCore::SetOptionValue(key, value);
        });

        // Add to history — store original path, not temp path
        {
            const std::wstring& histPath = originalPath.empty() ? path : originalPath;
            std::string pathUtf8;
            {
                int len = WideCharToMultiByte(CP_UTF8, 0, histPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                pathUtf8.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, histPath.c_str(), -1, &pathUtf8[0], len, nullptr, nullptr);
            }
            auto slash = pathUtf8.find_last_of("/\\");
            std::string fnameUtf8 = (slash != std::string::npos) ? pathUtf8.substr(slash + 1) : pathUtf8;
            SettingsManager::AddToHistory(fnameUtf8, pathUtf8);
            m_menu.SetCoreLoaded(true);
        }
    }
    else
    {
        OutputDebugStringA("[dosbox-uwp] retro_load_game FAILED\n");
    }
}

void dosbox_uwpMain::QueueLoadRom(const std::wstring& path, std::vector<uint8_t> romData, const std::wstring& originalPath)
{
    m_pendingLoad = std::make_unique<PendingLoad>();
    m_pendingLoad->path = path;
    m_pendingLoad->originalPath = originalPath;
    m_pendingLoad->data = std::move(romData);
}

void dosbox_uwpMain::CleanupTempFile()
{
    if (m_currentTempPath.empty())
        return;

    char buf[256];
    int len = WideCharToMultiByte(CP_UTF8, 0, m_currentTempPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathUtf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, m_currentTempPath.c_str(), -1, &pathUtf8[0], len, nullptr, nullptr);
    sprintf_s(buf, "[dosbox-uwp] Cleanup temp: %s\n", pathUtf8.c_str());
    OutputDebugStringA(buf);

    _wremove(m_currentTempPath.c_str());
    m_currentTempPath.clear();
}

void dosbox_uwpMain::CreateWindowSizeDependentResources()
{
}

void dosbox_uwpMain::Update()
{
    // Process queued load — set flag so Render() shows loading screen, defer actual load
    // Loading screen already activated in OpenFilePicker before I/O; this guards
    // edge cases like programmatic QueueLoadRom without picker path
    if (m_pendingLoad && !m_loadingActive)
        ActivateLoadingScreen();

#ifdef XB_INSPECTOR_ENABLED
    xb::Xray::update();
#endif

    m_timer.Tick([&]()
    {
        LARGE_INTEGER _t0, _t1, _t2, _t3, _freq;
        QueryPerformanceFrequency(&_freq);

        m_frameLate = false;
        m_sdlInput->PollEvents();
        QueryPerformanceCounter(&_t0);

        // Stop audio when core idle or menu visible — prevents underrun spam
        if (m_xaudio2 && m_xaudio2->IsStarted())
        {
            bool skipFlush = m_menu.IsVisible() && (m_menu.IsBootAnimComplete() ? m_menu.IsBeepGracePeriod() : true);
            if (!skipFlush)
            {
                if (!m_retroCore->IsLoaded() && !m_retroRunning)
                {
                    m_xaudio2->Flush();
                }
                else if (m_menu.IsVisible())
                {
                    m_xaudio2->Flush();
                }
            }
        }

        // Read gamepad analog stick always — used for splash cursor + in-game
        float sx = 0.0f, sy = 0.0f;
        m_sdlInput->GetLeftStick(sx, sy);
        const float GAMEPAD_DEADZONE = 0.15f;
        if (fabs(sx) < GAMEPAD_DEADZONE) sx = 0.0f;
        if (fabs(sy) < GAMEPAD_DEADZONE) sy = 0.0f;

        // Update frontend menu core-loaded state
        m_menu.SetCoreLoaded(m_retroCore && m_retroCore->IsLoaded());

        // Menu navigation (absorbs gamepad input while visible)
        if (m_menu.IsVisible())
        {
            // DPad auto-repeat: first press fires immediately, then repeats after delay
            const int DPAD_INITIAL_DELAY_MS = 300;
            const int DPAD_REPEAT_RATE_MS = 50;

            auto dpadRepeat = [&](int btn, bool up) {
                bool justPressed = m_sdlInput->WasButtonJustPressed(btn);
                bool held = m_sdlInput->IsButtonHeld(btn);
                ULONGLONG now = GetTickCount64();

                if (justPressed) {
                    // First press: fire immediately, start repeat timer
                    m_menu.OnDPad(up);
                    m_dpadRepeatBtn = btn;
                    m_dpadRepeatStart = now;
                    m_dpadRepeatNext = now + DPAD_INITIAL_DELAY_MS;
                    return;
                }
                if (held && m_dpadRepeatBtn == btn && now >= m_dpadRepeatNext) {
                    // Repeat: fire and schedule next
                    m_menu.OnDPad(up);
                    m_dpadRepeatNext = now + DPAD_REPEAT_RATE_MS;
                }
                if (!held && m_dpadRepeatBtn == btn) {
                    m_dpadRepeatBtn = -1;
                }
            };
            dpadRepeat(BUTTON_DPAD_UP, true);
            dpadRepeat(BUTTON_DPAD_DOWN, false);

            // DPad Left/Right: toggle option values
            auto dpadLeftRight = [&](int btn, bool left) {
                bool justPressed = m_sdlInput->WasButtonJustPressed(btn);
                bool held = m_sdlInput->IsButtonHeld(btn);
                ULONGLONG now = GetTickCount64();
                if (justPressed) {
                    if (left) m_menu.OnDPadLeft(); else m_menu.OnDPadRight();
                    m_dpadRepeatBtn = btn;
                    m_dpadRepeatStart = now;
                    m_dpadRepeatNext = now + DPAD_INITIAL_DELAY_MS;
                    return;
                }
                if (held && m_dpadRepeatBtn == btn && now >= m_dpadRepeatNext) {
                    if (left) m_menu.OnDPadLeft(); else m_menu.OnDPadRight();
                    m_dpadRepeatNext = now + DPAD_REPEAT_RATE_MS;
                }
                if (!held && m_dpadRepeatBtn == btn) {
                    m_dpadRepeatBtn = -1;
                }
            };
            dpadLeftRight(BUTTON_DPAD_LEFT, true);
            dpadLeftRight(BUTTON_DPAD_RIGHT, false);

            if (m_sdlInput->WasButtonJustPressed(BUTTON_A))
                m_menu.OnConfirm();
            if (m_sdlInput->WasButtonJustPressed(BUTTON_B))
                m_menu.OnBack();
            if (m_sdlInput->WasButtonJustPressed(BUTTON_L))
                m_menu.OnPageUp();
            if (m_sdlInput->WasButtonJustPressed(BUTTON_R))
                m_menu.OnPageDown();
        }
        else
        {
            // Reset repeat state when menu is hidden
            m_dpadRepeatBtn = -1;
        }

        // R3 -> PUREMENU toggle (after menu nav so WasButtonJustPressed not consumed)
        if (m_sdlInput->WasButtonJustPressed(BUTTON_R3) && m_retroCore && m_retroCore->IsLoaded()) {
            spdlog::info("[input] R3 -> toggle PUREMENU");
            m_retroCore->ToggleOSD();
        }

        // LB+RB+Select simultaneous press → toggle gamepad mouse mode
        // Mouse mode OFF (default): stick does NOT simulate mouse in DOSBox/Puremenu
        // Mouse mode ON: stick → relative mouse, A→Enter, B→Escape for Puremenu
        {
            bool lb = m_sdlInput->IsButtonHeld(BUTTON_L);
            bool rb = m_sdlInput->IsButtonHeld(BUTTON_R);
            bool sel = m_sdlInput->IsButtonHeld(BUTTON_SELECT);
            bool allThree = lb && rb && sel;
            if (allThree && !m_lbrbsPrevHeld && m_retroCore && m_retroCore->IsLoaded()) {
                m_gamepadMouseMode = !m_gamepadMouseMode;
                spdlog::info("[input] Gamepad mouse mode: {}", m_gamepadMouseMode ? "ON" : "OFF");
            }
            m_lbrbsPrevHeld = allThree;
        }

        // Log every gamepad button press (after all handlers consume their events)
        struct { int btn; const char* name; } btns[] = {
            { BUTTON_A, "A" }, { BUTTON_B, "B" }, { BUTTON_X, "X" }, { BUTTON_Y, "Y" },
            { BUTTON_L, "L" }, { BUTTON_R, "R" }, { BUTTON_L2, "L2" }, { BUTTON_R2, "R2" },
            { BUTTON_START, "START" }, { BUTTON_SELECT, "SELECT" },
            { BUTTON_L3, "L3" }, { BUTTON_R3, "R3" },
            { BUTTON_DPAD_UP, "DPAD_UP" }, { BUTTON_DPAD_DOWN, "DPAD_DOWN" },
            { BUTTON_DPAD_LEFT, "DPAD_LEFT" }, { BUTTON_DPAD_RIGHT, "DPAD_RIGHT" },
        };
        for (auto& b : btns)
            if (m_sdlInput->WasButtonJustPressed(b.btn))
                spdlog::info("[input] {} pressed", b.name);

#ifdef MOUSE_SUPPORT
        // Splash screen + menu: gamepad moves D2D cursor directly
        float dtSec = (float)m_timer.GetElapsedSeconds();
        if (!m_retroCore->IsLoaded() || m_menu.IsVisible())
        {
            m_pointerX += sx * 1.34f * dtSec;
            m_pointerY += sy * 1.34f * dtSec;
            if (m_pointerX < 0.0f) m_pointerX = 0.0f;
            if (m_pointerX > 1.0f) m_pointerX = 1.0f;
            if (m_pointerY < 0.0f) m_pointerY = 0.0f;
            if (m_pointerY > 1.0f) m_pointerY = 1.0f;
        }
#endif

        // Gamepad → RetroPad (GENERICKEYBOARD preset) for core to translate to DOS keys
        if (!m_menu.IsVisible() && m_retroRunning && m_retroCore->IsLoaded())
        {
            PollMouseButtons();
            PollKeyboard();
            for (unsigned i = 0; i < 16; i++)
                RetroCore::SetJoypadButton(i, false);

            // Generic keyboard preset: gamepad buttons → RetroPad IDs
            struct { int btn; unsigned retroId; } padMap[] = {
                { BUTTON_DPAD_UP,    RETRO_DEVICE_ID_JOYPAD_UP },
                { BUTTON_DPAD_DOWN,  RETRO_DEVICE_ID_JOYPAD_DOWN },
                { BUTTON_DPAD_LEFT,  RETRO_DEVICE_ID_JOYPAD_LEFT },
                { BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
                { BUTTON_A,          RETRO_DEVICE_ID_JOYPAD_A },
                { BUTTON_B,          RETRO_DEVICE_ID_JOYPAD_B },
                { BUTTON_X,          RETRO_DEVICE_ID_JOYPAD_X },
                { BUTTON_Y,          RETRO_DEVICE_ID_JOYPAD_Y },
                { BUTTON_SELECT,     RETRO_DEVICE_ID_JOYPAD_SELECT },
                { BUTTON_START,      RETRO_DEVICE_ID_JOYPAD_START },
                { BUTTON_L,          RETRO_DEVICE_ID_JOYPAD_L },
                { BUTTON_R,          RETRO_DEVICE_ID_JOYPAD_R },
                { BUTTON_L2,         RETRO_DEVICE_ID_JOYPAD_L2 },
                { BUTTON_R2,         RETRO_DEVICE_ID_JOYPAD_R2 },
                { BUTTON_L3,         RETRO_DEVICE_ID_JOYPAD_L3 },
                { BUTTON_R3,         RETRO_DEVICE_ID_JOYPAD_R3 },
            };
            for (auto& m : padMap)
                RetroCore::SetJoypadButton(m.retroId, m_sdlInput->IsButtonHeld(m.btn));

#ifdef MOUSE_SUPPORT
            // Gamepad mouse simulation — only when m_gamepadMouseMode is ON (LB+RB+Select toggle)
            if (m_gamepadMouseMode)
            {
                // Phase 2: Gamepad left stick → relative mouse
                if (sx != 0.0f || sy != 0.0f)
                {
                    float curveX = (sx > 0 ? 1.0f : -1.0f) * sx * sx;
                    float curveY = (sy > 0 ? 1.0f : -1.0f) * sy * sy;
                    float inPixelsPerSec = 800.0f;
                    int dx = (int)(curveX * inPixelsPerSec * dtSec);
                    int dy = (int)(curveY * inPixelsPerSec * dtSec);
                    if (dx == 0 && sx != 0) dx = (sx > 0 ? 1 : -1);
                    if (dy == 0 && sy != 0) dy = (sy > 0 ? 1 : -1);
                    m_retroCore->SetMouseMove(dx, dy);
                }

                // Gamepad buttons → PUREMENU keyboard (A=Enter, B=Escape)
                // Only when OSD visible and menu NOT visible — prevents spurious input in DOS
                if (DBPS_IsShowingOSD() && !m_menu.IsVisible())
                {
                    static bool prevA = false, prevB = false;
                    bool nowA = m_sdlInput->IsButtonHeld(BUTTON_A);
                    bool nowB = m_sdlInput->IsButtonHeld(BUTTON_B);
                    if (nowA != prevA) { RetroCore::SetKeyState(RETROK_RETURN, nowA); prevA = nowA; }
                    if (nowB != prevB) { RetroCore::SetKeyState(RETROK_ESCAPE, nowB); prevB = nowB; }
                }

                // Phase 3: Gamepad → absolute pointer for PUREMENU
                LARGE_INTEGER _gmnow;
                QueryPerformanceCounter(&_gmnow);
                double mouseIdleMs = 0.0;
                if (m_lastPointerTime.QuadPart != 0)
                    mouseIdleMs = (double)(_gmnow.QuadPart - m_lastPointerTime.QuadPart)
                                  * 1000.0 / m_qpcFreq.QuadPart;
                if (mouseIdleMs > 500.0 || m_lastPointerTime.QuadPart == 0)
                {
                    float cursorDx = sx * 0.80f * dtSec;
                    float cursorDy = sy * 0.80f * dtSec;
                    m_virtualCursorX += cursorDx;
                    m_virtualCursorY += cursorDy;
                    if (m_virtualCursorX < 0.0f) m_virtualCursorX = 0.0f;
                    if (m_virtualCursorX > 1.0f) m_virtualCursorX = 1.0f;
                    if (m_virtualCursorY < 0.0f) m_virtualCursorY = 0.0f;
                    if (m_virtualCursorY > 1.0f) m_virtualCursorY = 1.0f;
                    m_retroCore->SetPointer(m_virtualCursorX, m_virtualCursorY, false);
                }
            } // end m_gamepadMouseMode
#endif

            // No pacing wait — the dosbox-pure core self-paces via CPU_CycleMax
            // adjustment in GFX_AdvanceFrame. Just call RunFrame once per loop iteration.
            // XAudio2 buffer level stays stable because core produces ~44100 samples/sec
            // matching the hardware consumption rate.
#ifndef XB_INSPECTOR_ENABLED
            m_retroCore->RunFrame();
#else
            if (!s_paused)
                m_retroCore->RunFrame();
#endif
            m_lastRetroRuns = 1;

            if (RetroCore::IsShutdownRequested())
            {
                OutputDebugStringA("[dosbox-uwp] Shutdown requested by core, unloading game\n");
                m_retroCore->UnloadGame();
                m_retroRunning = false;
                m_clearColor = DirectX::Colors::Black;
                m_menu.Show();
                return; // return from Tick lambda, render will show menu
            }
        }
        QueryPerformanceCounter(&_t1);

        // Frame pacing stats
        {
            double frameMs = (double)(_t1.QuadPart - _t0.QuadPart) * 1000.0 / _freq.QuadPart;
            double targetFps = m_retroCore->IsLoaded() ? m_retroCore->GetTargetFps() : 60.0;
            double targetMs = 1000.0 / targetFps;
#ifdef XB_INSPECTOR_ENABLED
            s_debug_fps = m_timer.GetFramesPerSecond();
            s_debug_target_fps = targetFps;
            s_debug_frame_ms = frameMs;
            s_perf.fps = s_debug_fps;
            s_perf.target_fps = s_debug_target_fps;
            s_perf.frame_ms = s_debug_frame_ms;
            s_perf.audio_queued = m_xaudio2 ? m_xaudio2->GetQueuedFrames() : 0;
            s_perf.audio_underruns = 0; // per-frame reset, not yet wired from XA2
            s_perf.audio_produced = *XAudio2Output::TotalProducedPtr();
            s_perf.audio_consumed = *XAudio2Output::TotalConsumedPtr();
#endif
            static int pacingLogCounter = 0;
            if ((++pacingLogCounter % 600) == 0)
            {
                uint32_t audioQueued = 0;
                if (m_xaudio2 && m_xaudio2->IsStarted())
                    audioQueued = m_xaudio2->GetQueuedFrames();
                char buf[256];
                sprintf_s(buf, "[dosbox-uwp] PACE: target=%.0f frame=%.2fms audioQueued=%u retroRuns=%d\n",
                    targetFps, frameMs, audioQueued, m_lastRetroRuns);
                OutputDebugStringA(buf);
            }
        }

        {
            // Load state watchdog: if stuck in BOOTING >5s, log warning
            if (m_loadState == LOAD_BOOTING)
            {
                if (++m_loadTimer > 300)
                {
                    static bool warned = false;
                    if (!warned) { warned = true;
                        OutputDebugStringA("[dosbox-uwp] WARNING: Load stuck in BOOTING >5s (possible hang)\n");
                    }
                }
            }
            else
            {
                m_loadTimer = 0;
            }
        }
        QueryPerformanceCounter(&_t2);

        // loading screen animation (time-based, not frame-based)
        if (m_loadingActive)
        {
            LARGE_INTEGER _animNow;
            QueryPerformanceCounter(&_animNow);
            double animElapsed = (double)(_animNow.QuadPart - m_loadingStart.QuadPart)
                                 * 1000.0 / m_qpcFreq.QuadPart;
            m_loadingAngle = fmod((float)(animElapsed * 0.36f), 360.0f);
            m_loadingDots = ((int)(animElapsed / 200.0)) % 4;
        }

        QueryPerformanceCounter(&_t3);
#ifdef XB_INSPECTOR_ENABLED
        s_debug_poll_ms = (double)(_t0.QuadPart) * 1000.0 / _freq.QuadPart;
        s_debug_hud_ms = (double)(_t2.QuadPart - _t1.QuadPart) * 1000.0 / _freq.QuadPart;
        s_debug_render_ms = (double)(_t3.QuadPart - _t2.QuadPart) * 1000.0 / _freq.QuadPart;
        s_debug_total_ms = (double)(_t3.QuadPart) * 1000.0 / _freq.QuadPart;
        s_perf.poll_ms = s_debug_poll_ms;
        s_perf.hud_ms = s_debug_hud_ms;
        s_perf.render_ms = s_debug_render_ms;
        s_perf.total_ms = s_debug_total_ms;
#endif
        {
            static unsigned _tc = 0;
            if ((++_tc % 600) == 0)
            {
                double poll_ms  = (double)(_t0.QuadPart) * 1000.0 / _freq.QuadPart;
                double frame_ms = (double)(_t1.QuadPart - _t0.QuadPart) * 1000.0 / _freq.QuadPart;
                double hud_ms   = (double)(_t2.QuadPart - _t1.QuadPart) * 1000.0 / _freq.QuadPart;
                double scene_ms = (double)(_t3.QuadPart - _t2.QuadPart) * 1000.0 / _freq.QuadPart;
                double total_ms = (double)(_t3.QuadPart) * 1000.0 / _freq.QuadPart;
                float fps       = m_timer.GetFramesPerSecond();
                uint32_t xa2Frames = 0;
                if (m_xaudio2 && m_xaudio2->IsStarted())
                    xa2Frames = m_xaudio2->GetQueuedFrames();
                unsigned long long memBytes = 0;
                try { memBytes = Windows::System::MemoryManager::AppMemoryUsage; } catch (...) { }
                char _dbg[512];
                sprintf_s(_dbg, "[dosbox-uwp] TICK #%u: frame=%.1fms hud=%.1f scene=%.1f fps=%.0f "
                    "XA2frames=%u MEM=%lluMB total=%.1f\n",
                    _tc, frame_ms, hud_ms, scene_ms, fps,
                    xa2Frames, memBytes / (1024*1024), total_ms);
                OutputDebugStringA(_dbg);
            }
        }
    });
}

bool dosbox_uwpMain::Render()
{
    if (m_timer.GetFrameCount() == 0)
        return false;

    auto context = m_deviceResources->GetD3DDeviceContext();
    ID3D11RenderTargetView *const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
    context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());
    context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), m_clearColor);
    context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    if (m_retroCore && m_retroCore->IsLoaded())
    {
        bool haveFrame = m_retroCore->HasFrame();
        if (haveFrame)
        {
            m_retroScreen->UpdateVideoFrame(
                (const uint8_t*)m_retroCore->GetFrameData(),
                m_retroCore->GetFrameWidth(),
                m_retroCore->GetFrameHeight(),
                m_retroCore->GetFramePitch());
            m_retroCore->ClearFrame();
        }
        m_retroScreen->Render();
    }

    // D2D rendering: single BeginDraw/EndDraw for all overlays
    auto d2dContext = m_deviceResources->GetD2DDeviceContext();
    auto logicalSize = m_deviceResources->GetLogicalSize();
    auto dwrite = m_deviceResources->GetDWriteFactory();
    d2dContext->BeginDraw();

    // Loading screen (full black, spinning disc + Loading...)
    if (m_loadingActive)
    {
        RenderLoadingScreen(d2dContext, dwrite, D2D1::SizeF(logicalSize.Width, logicalSize.Height));
    }
    else
    {
        // Full-screen FrontendMenu (DOS style)
        if (m_menu.IsVisible())
        {
            m_menu.RenderFullScreen(d2dContext, dwrite, logicalSize.Width, logicalSize.Height);
        }

#ifdef MOUSE_SUPPORT
        // Draw simple cursor overlay when menu visible or on splash screen
        if (m_menu.IsVisible() || !m_retroCore->IsLoaded())
    {
        if (!m_cursorBrush)
        {
            d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White, 0.85f), &m_cursorBrush);
        }

        float cx = m_pointerX * logicalSize.Width;
        float cy = m_pointerY * logicalSize.Height;
        const float CS = 10.0f;

        d2dContext->DrawLine(
            D2D1::Point2F(cx - CS, cy), D2D1::Point2F(cx + CS, cy),
            m_cursorBrush.Get(), 1.5f);
        d2dContext->DrawLine(
            D2D1::Point2F(cx, cy - CS), D2D1::Point2F(cx, cy + CS),
            m_cursorBrush.Get(), 1.5f);
    }
#endif
    } // end else (not loading)

    HRESULT hr = d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        m_cursorBrush.Reset();
    }

    return true;
}

void dosbox_uwpMain::ToggleOSD()
{
    if (!m_retroCore)
        return;
    m_retroCore->ToggleOSD();
}

void dosbox_uwpMain::OnKeyEvent(Windows::System::VirtualKey key, bool down, uint32_t scanCode, bool isExtended)
{
    if (!m_retroCore)
        return;

    // Route keys to FrontendMenu when visible
    if (m_menu.IsVisible())
    {
        if (down)
        {
            switch ((int)key)
            {
            case 0x26: m_menu.OnDPad(true); return;  // Up
            case 0x28: m_menu.OnDPad(false); return; // Down
            case 0x25: m_menu.OnDPadLeft(); return;  // Left
            case 0x27: m_menu.OnDPadRight(); return; // Right
            case 0x0D: m_menu.OnConfirm(); return;   // Enter
            case 0x1B: m_menu.OnBack();  return;     // Escape
            case 0x21: m_menu.OnPageUp(); return;    // PageUp
            case 0x22: m_menu.OnPageDown(); return;  // PageDown
            case 0x45: m_menu.OnEasterEgg(); return;  // E = next quote
            }
        }
        return; // absorb all keyboard while menu is visible
    }

    int vk = (int)key;
    unsigned retroKey = RETROK_UNKNOWN;

    switch (vk)
    {
    // Modifiers — UWP VirtualKey enum (not Win32 VK_)
    case 0x10:
        retroKey = (scanCode == 0x36) ? RETROK_RSHIFT : RETROK_LSHIFT;
        break;
    case 0x11:
        retroKey = isExtended ? RETROK_RCTRL : RETROK_LCTRL;
        break;
    case 0x12:
        retroKey = isExtended ? RETROK_RALT : RETROK_LALT;
        break;

    // Control chars — VirtualKey value matches RETROK_
    case 0x08: retroKey = RETROK_BACKSPACE; break;
    case 0x09: retroKey = RETROK_TAB;       break;
    case 0x0C: retroKey = RETROK_CLEAR;     break;
    case 0x0D: retroKey = RETROK_RETURN;    break;
    case 0x1B: retroKey = RETROK_ESCAPE;    break;
    case 0x20: retroKey = RETROK_SPACE;     break;

    // Digits 0-9 — Direct ASCII match
    case 0x30: retroKey = RETROK_0; break;
    case 0x31: retroKey = RETROK_1; break;
    case 0x32: retroKey = RETROK_2; break;
    case 0x33: retroKey = RETROK_3; break;
    case 0x34: retroKey = RETROK_4; break;
    case 0x35: retroKey = RETROK_5; break;
    case 0x36: retroKey = RETROK_6; break;
    case 0x37: retroKey = RETROK_7; break;
    case 0x38: retroKey = RETROK_8; break;
    case 0x39: retroKey = RETROK_9; break;

    // Letters A-Z (VK uppercase 65-90 → RETROK lowercase 97-122)
    case 0x41: retroKey = RETROK_a; break; case 0x42: retroKey = RETROK_b; break;
    case 0x43: retroKey = RETROK_c; break; case 0x44: retroKey = RETROK_d; break;
    case 0x45: retroKey = RETROK_e; break; case 0x46: retroKey = RETROK_f; break;
    case 0x47: retroKey = RETROK_g; break; case 0x48: retroKey = RETROK_h; break;
    case 0x49: retroKey = RETROK_i; break; case 0x4A: retroKey = RETROK_j; break;
    case 0x4B: retroKey = RETROK_k; break; case 0x4C: retroKey = RETROK_l; break;
    case 0x4D: retroKey = RETROK_m; break; case 0x4E: retroKey = RETROK_n; break;
    case 0x4F: retroKey = RETROK_o; break; case 0x50: retroKey = RETROK_p; break;
    case 0x51: retroKey = RETROK_q; break; case 0x52: retroKey = RETROK_r; break;
    case 0x53: retroKey = RETROK_s; break; case 0x54: retroKey = RETROK_t; break;
    case 0x55: retroKey = RETROK_u; break; case 0x56: retroKey = RETROK_v; break;
    case 0x57: retroKey = RETROK_w; break; case 0x58: retroKey = RETROK_x; break;
    case 0x59: retroKey = RETROK_y; break; case 0x5A: retroKey = RETROK_z; break;

    // Navigation (VirtualKey values — NOT printable)
    case 0x21: retroKey = RETROK_PAGEUP;   break;
    case 0x22: retroKey = RETROK_PAGEDOWN; break;
    case 0x23: retroKey = RETROK_END;      break;
    case 0x24: retroKey = RETROK_HOME;     break;
    case 0x25: retroKey = RETROK_LEFT;     break;
    case 0x26: retroKey = RETROK_UP;       break;
    case 0x27: retroKey = RETROK_RIGHT;    break;
    case 0x28: retroKey = RETROK_DOWN;     break;
    case 0x2D: retroKey = RETROK_INSERT;   break;
    case 0x2E: retroKey = RETROK_DELETE;   break;
    case 0x2F: retroKey = RETROK_HELP;     break;

    // Modifiers
    case 0xA0: retroKey = RETROK_LSHIFT; break;
    case 0xA1: retroKey = RETROK_RSHIFT; break;
    case 0xA2: retroKey = RETROK_LCTRL;  break;
    case 0xA3: retroKey = RETROK_RCTRL;  break;
    case 0xA4: retroKey = RETROK_LALT;   break;
    case 0xA5: retroKey = RETROK_RALT;   break;

    // Super / Menu
    case 0x5B: retroKey = RETROK_LSUPER; break;
    case 0x5C: retroKey = RETROK_RSUPER; break;
    case 0x5D: retroKey = RETROK_MENU;   break;

    // Numpad
    case 0x60: retroKey = RETROK_KP0;          break;
    case 0x61: retroKey = RETROK_KP1;          break;
    case 0x62: retroKey = RETROK_KP2;          break;
    case 0x63: retroKey = RETROK_KP3;          break;
    case 0x64: retroKey = RETROK_KP4;          break;
    case 0x65: retroKey = RETROK_KP5;          break;
    case 0x66: retroKey = RETROK_KP6;          break;
    case 0x67: retroKey = RETROK_KP7;          break;
    case 0x68: retroKey = RETROK_KP8;          break;
    case 0x69: retroKey = RETROK_KP9;          break;
    case 0x6A: retroKey = RETROK_KP_MULTIPLY;  break;
    case 0x6B: retroKey = RETROK_KP_PLUS;      break;
    case 0x6D: retroKey = RETROK_KP_MINUS;     break;
    case 0x6E: retroKey = RETROK_KP_PERIOD;    break;
    case 0x6F: retroKey = RETROK_KP_DIVIDE;    break;
    case 0x6C: retroKey = RETROK_KP_ENTER;     break;

    // Function keys F1-F12
    case 0x70: retroKey = RETROK_F1;  break;
    case 0x71: retroKey = RETROK_F2;  break;
    case 0x72: retroKey = RETROK_F3;  break;
    case 0x73: retroKey = RETROK_F4;  break;
    case 0x74: retroKey = RETROK_F5;  break;
    case 0x75: retroKey = RETROK_F6;  break;
    case 0x76: retroKey = RETROK_F7;  break;
    case 0x77: retroKey = RETROK_F8;  break;
    case 0x78: retroKey = RETROK_F9;  break;
    case 0x79: retroKey = RETROK_F10; break;
    case 0x7A: retroKey = RETROK_F11; break;
    case 0x7B: retroKey = RETROK_F12; break;

    // Lock / special
    case 0x13: retroKey = RETROK_PAUSE;     break;
    case 0x14: retroKey = RETROK_CAPSLOCK;  break;
    case 0x90: retroKey = RETROK_NUMLOCK;   break;
    case 0x91: retroKey = RETROK_SCROLLOCK; break;
    case 0x2C: retroKey = RETROK_PRINT;     break;
    case 0x2A: retroKey = RETROK_PRINT;     break;
    case 0xB7: retroKey = RETROK_SYSREQ;    break;
    case 0x1C: retroKey = RETROK_BREAK;     break;

    // OEM keys — US layout physical position
    case 0xBA: retroKey = RETROK_SEMICOLON;    break;
    case 0xBB: retroKey = RETROK_EQUALS;       break;
    case 0xBC: retroKey = RETROK_COMMA;        break;
    case 0xBD: retroKey = RETROK_MINUS;        break;
    case 0xBE: retroKey = RETROK_PERIOD;       break;
    case 0xBF: retroKey = RETROK_SLASH;        break;
    case 0xC0: retroKey = RETROK_BACKQUOTE;    break;
    case 0xDB: retroKey = RETROK_LEFTBRACKET;  break;
    case 0xDC: retroKey = RETROK_BACKSLASH;    break;
    case 0xDD: retroKey = RETROK_RIGHTBRACKET; break;
    case 0xDE: retroKey = RETROK_QUOTE;        break;

    default: break;
    }

    if (vk >= 0 && vk < 256)
        m_activeVKeyState[vk] = down;

    if (retroKey != RETROK_UNKNOWN)
    {
        if (!m_retroCore->IsLoaded())
        {
            char buf[128];
            sprintf_s(buf, "[dosbox-uwp] Key: VK=0x%02X ignored — core not loaded\n", vk);
            OutputDebugStringA(buf);
        }
        else
        {
#ifdef DEBUG_KEYBOARD
            char buf[128];
            sprintf_s(buf, "[dosbox-uwp] Key: VK=0x%02X down=%d retroKey=%u\n", vk, down, retroKey);
            OutputDebugStringA(buf);
#endif
            RetroCore::SetKeyState(retroKey, down);
        }
    }
}

void dosbox_uwpMain::PollKeyboard()
{
    if (!m_retroCore) return;
    try
    {
        auto window = Windows::UI::Core::CoreWindow::GetForCurrentThread();
        if (!window) return;
        for (int vk = 0; vk < 256; vk++)
        {
            if (!m_activeVKeyState[vk]) continue;
            auto state = window->GetKeyState(static_cast<Windows::System::VirtualKey>(vk));
            bool stillDown = (state & Windows::UI::Core::CoreVirtualKeyStates::Down) != Windows::UI::Core::CoreVirtualKeyStates::None;
            if (!stillDown)
            {
                m_activeVKeyState[vk] = false;
                OnKeyEvent(static_cast<Windows::System::VirtualKey>(vk), false);
            }
        }
    }
    catch (Platform::Exception^) { }
}

#ifdef MOUSE_SUPPORT
void dosbox_uwpMain::OnPointerMove(float nx, float ny, float px, float py)
{
    if (!m_retroCore) return;

    m_pointerX = nx;
    m_pointerY = ny;

    // Route to FrontendMenu when visible
    if (m_menu.IsVisible())
    {
        auto logicalSize = m_deviceResources->GetLogicalSize();
        m_menu.HandlePointerMove(nx * logicalSize.Width, ny * logicalSize.Height);
        return;
    }

    int relX = (int)(px - m_lastPointerPX);
    int relY = (int)(py - m_lastPointerPY);
    m_lastPointerPX = px;
    m_lastPointerPY = py;

    QueryPerformanceCounter(&m_lastPointerTime);

    m_retroCore->SetMouseMove(relX, relY);
    m_retroCore->SetPointer(nx, ny, m_pointerDown);
}

void dosbox_uwpMain::OnPointerDown(float nx, float ny, unsigned btn)
{
    if (!m_retroCore) return;

    // Route to FrontendMenu when visible
    if (m_menu.IsVisible())
    {
        auto logicalSize = m_deviceResources->GetLogicalSize();
        m_menu.HandlePointerDown(nx * logicalSize.Width, ny * logicalSize.Height, btn);
        return;
    }

    m_pointerX = nx;
    m_pointerY = ny;
    m_lastPointerX = nx;
    m_lastPointerY = ny;
    m_pointerDown = true;

    m_retroCore->SetPointer(nx, ny, true);
    m_retroCore->SetMouseButton(btn, true);
}

void dosbox_uwpMain::OnPointerUp(unsigned btn)
{
    if (!m_retroCore) return;
    m_retroCore->SetMouseButton(btn, false);
}

void dosbox_uwpMain::OnPointerRelease()
{
    if (!m_retroCore) return;
    m_pointerDown = false;
    m_retroCore->SetPointer(m_pointerX, m_pointerY, false);
}

void dosbox_uwpMain::OnPointerWheel(int delta)
{
    if (m_menu.IsVisible())
    {
        m_menu.HandlePointerWheel(delta);
        return;
    }
    if (!m_retroCore) return;
    m_retroCore->SetMouseWheel(delta);
}

void dosbox_uwpMain::SetMousePointerId(uint32_t id)
{
    m_mousePointerId = id;
}

void dosbox_uwpMain::PollMouseButtons()
{
    if (m_mousePointerId == 0 || !m_retroCore) return;
    try
    {
        auto pt = Windows::UI::Input::PointerPoint::GetCurrentPoint(m_mousePointerId);
        if (!pt) return;
        auto props = pt->Properties;
        m_retroCore->SetMouseButton(1, props->IsLeftButtonPressed);
        m_retroCore->SetMouseButton(2, props->IsRightButtonPressed);
        m_retroCore->SetMouseButton(3, props->IsMiddleButtonPressed);
    }
    catch (Platform::Exception^) { }
}
#endif

void dosbox_uwpMain::OnDeviceLost()
{
    m_retroScreen->ReleaseDeviceDependentResources();
    m_loadingDisc.Reset();
}

void dosbox_uwpMain::EnsureLoadingDisc()
{
    if (m_loadingDisc) return;
    auto d2d = m_deviceResources->GetD2DDeviceContext();
    if (!d2d) return;

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return;

    wchar_t imgPath[MAX_PATH];
    // Path 1: InstalledLocation (AppX)
    auto installPath = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;
    wcscpy_s(imgPath, installPath->Data());
    size_t plen = wcslen(imgPath);
    if (plen + 30 >= MAX_PATH) return;
    if (imgPath[plen - 1] != L'\\') { imgPath[plen] = L'\\'; plen++; }
    wcscpy_s(imgPath + plen, MAX_PATH - plen, L"Assets\\disc.png");

    for (int attempt = 0; attempt < 2; attempt++)
    {
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = wic->CreateDecoderFromFilename(imgPath, nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(hr))
        {
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr))
            {
                Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
                hr = wic->CreateFormatConverter(&conv);
                if (SUCCEEDED(hr))
                {
                    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
                    if (FAILED(hr))
                        hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                            WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
                    if (SUCCEEDED(hr))
                    {
                        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bmp;
                        hr = d2d->CreateBitmapFromWicBitmap(conv.Get(), &bmp);
                        if (SUCCEEDED(hr) && bmp)
                        {
                            m_loadingDisc = bmp.Get();
                            spdlog::info("Disc loaded");
                            return;
                        }
                    }
                }
            }
        }
        // Path 2: parent of InstalledLocation (dev build)
        if (attempt == 0)
        {
            wchar_t* ls = wcsrchr(imgPath, L'\\');
            if (!ls) break;
            *ls = L'\0';
            ls = wcsrchr(imgPath, L'\\');
            if (!ls) break;
            *ls = L'\0';
            plen = wcslen(imgPath);
            if (plen + 30 >= MAX_PATH) break;
            if (imgPath[plen - 1] != L'\\') { imgPath[plen] = L'\\'; plen++; }
            wcscpy_s(imgPath + plen, MAX_PATH - plen, L"Assets\\disc.png");
        }
    }
    spdlog::warn("Disc: failed to load disc.png");
}

void dosbox_uwpMain::OnDeviceRestored()
{
    m_retroScreen->CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
    // disc loaded lazily in RenderLoadingScreen via EnsureLoadingDisc()
}

void dosbox_uwpMain::ProcessPendingLoad()
{
    if (!m_pendingLoad) return;
    // Debug delay: 10 seconds so loading screen visible for testing
    LARGE_INTEGER _now;
    QueryPerformanceCounter(&_now);
    double elapsedMs = (double)(_now.QuadPart - m_loadingStart.QuadPart) * 1000.0 / m_qpcFreq.QuadPart;
    if (elapsedMs < 2000.0) return;
    LoadRom(m_pendingLoad->path, std::move(m_pendingLoad->data), m_pendingLoad->originalPath);
    m_pendingLoad.reset();
    m_loadState = m_retroCore && m_retroCore->IsLoaded() ? LOAD_DONE : LOAD_FAILED;

    // Force-start XAudio2 voice after load — bypass auto-start which needs TARGET_QUEUE frames
    if (m_xaudio2)
    {
        m_xaudio2->Flush();
        // Submit 2 frames of silence to pre-fill, then start immediately
        static const int16_t silence[4] = {0, 0, 0, 0};
        m_xaudio2->Submit(silence, 2);
        m_xaudio2->Start();
        spdlog::info("[dosbox-uwp] XA2 force-started after load");
    }
    m_loadingActive = false;
    m_loadingDisc.Reset();
}

void dosbox_uwpMain::RenderLoadingScreen(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, D2D1_SIZE_F logicalSize)
{
    const auto& theme = SettingsManager::GetTheme();
    float w = logicalSize.width;
    float h = logicalSize.height;

    float dpiscale;
    { FLOAT dx, dy; d2d->GetDpi(&dx, &dy); dpiscale = dx / 96.0f; }

    // Semi-transparent fullscreen background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blackBg;
    d2d->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, theme.overlay_alpha), &blackBg);
    if (blackBg) d2d->FillRectangle(D2D1::RectF(0, 0, w, h), blackBg.Get());

    // Centered dialog panel
    float panelW = min(w * 0.50f, 360.0f);
    float panelH = min(h * 0.40f, 260.0f);
    float panelX = (w - panelW) * 0.5f;
    float panelY = (h - panelH) * 0.5f;

    // Panel background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_panel), &bgBrush);
    if (bgBrush) d2d->FillRectangle(D2D1::RectF(panelX, panelY, panelX + panelW, panelY + panelH), bgBrush.Get());

    // Panel border (double border like AboutDialog)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> frameBrush;
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.frame), &frameBrush);
    if (frameBrush)
    {
        d2d->DrawRectangle(D2D1::RectF(panelX, panelY, panelX + panelW, panelY + panelH), frameBrush.Get(), 2.0f);
        d2d->DrawRectangle(D2D1::RectF(panelX + 4, panelY + 4, panelX + panelW - 4, panelY + panelH - 4), frameBrush.Get(), 1.0f);
    }

    // Title bar (animated alpha pulse)
    float TITLE_H = 38.0f * dpiscale;
    float titleTop = panelY + 8;
    D2D1_RECT_F titleBg = { panelX + 8, titleTop, panelX + panelW - 8, titleTop + TITLE_H };
    float t = (float)(GetTickCount64() % 3000) / 3000.0f;
    float alpha = 0.70f + 0.30f * (0.5f + 0.5f * sinf(t * 6.283185f));
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> titlePulseBrush;
    D2D1_COLOR_F titleCol = D2D1::ColorF(theme.title_bg);
    titleCol.a = alpha;
    d2d->CreateSolidColorBrush(titleCol, &titlePulseBrush);
    if (titlePulseBrush) d2d->FillRectangle(titleBg, titlePulseBrush.Get());

    // Title text "LOADING" centered
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFmt;
    dwrite->CreateTextFormat(L"VCR OSD Mono", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 28.0f * dpiscale, L"en-US", &titleFmt);
    if (titleFmt)
    {
        titleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        titleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        float titleBarW = panelW - 32.0f;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> titleLayout;
        dwrite->CreateTextLayout(L"LOADING", 7, titleFmt.Get(), titleBarW, TITLE_H, &titleLayout);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> titleTextBrush;
        d2d->CreateSolidColorBrush(D2D1::ColorF(theme.text_title), &titleTextBrush);
        if (titleLayout && titleTextBrush)
            d2d->DrawTextLayout(D2D1::Point2F(panelX + 16, titleTop), titleLayout.Get(), titleTextBrush.Get());
    }

    // Content area
    float contentTop = panelY + 8 + TITLE_H + 16.0f;
    float contentBottom = panelY + panelH - 16.0f;
    float contentCenterX = panelX + panelW * 0.5f;

    // Spinning disc centered
    float discSize = 80.0f;
    float discCenterY = (contentTop + contentBottom) * 0.5f - 20.0f;
    EnsureLoadingDisc();

    if (m_loadingDisc)
    {
        D2D1_RECT_F discRect = {
            contentCenterX - discSize * 0.5f, discCenterY - discSize * 0.5f,
            contentCenterX + discSize * 0.5f, discCenterY + discSize * 0.5f
        };
        d2d->SetTransform(D2D1::Matrix3x2F::Rotation(m_loadingAngle,
            D2D1::Point2F(contentCenterX, discCenterY)));
        d2d->DrawBitmap(m_loadingDisc.Get(), discRect, 1.0f);
        d2d->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    else
    {
        // Fallback: orange circle
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> orangeBrush;
        d2d->CreateSolidColorBrush(D2D1::ColorF(0xff8800), &orangeBrush);
        if (orangeBrush)
            d2d->FillEllipse(D2D1::Ellipse(D2D1::Point2F(contentCenterX, discCenterY),
                discSize * 0.5f, discSize * 0.5f), orangeBrush.Get());
    }

    // "Loading..." text — bottom-left of panel, fixed width so dots don't shift
    static const wchar_t* dotStates[] = { L".", L"..", L"...", L"" };
    wchar_t loadingText[64];
    swprintf_s(loadingText, L"Loading%s", dotStates[m_loadingDots]);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFmt;
    dwrite->CreateTextFormat(L"VCR OSD Mono", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 22.0f * dpiscale, L"en-US", &textFmt);
    if (textFmt)
    {
        textFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        float fixedTextW = 160.0f * dpiscale;
        float textX = panelX + 16.0f;
        float textY = panelY + panelH - 40.0f;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
        dwrite->CreateTextLayout(loadingText, (UINT32)wcslen(loadingText), textFmt.Get(),
            fixedTextW, 30.0f, &textLayout);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        d2d->CreateSolidColorBrush(D2D1::ColorF(0xcccccc), &textBrush);
        if (textLayout && textBrush)
            d2d->DrawTextLayout(D2D1::Point2F(textX, textY), textLayout.Get(), textBrush.Get());
    }
}
