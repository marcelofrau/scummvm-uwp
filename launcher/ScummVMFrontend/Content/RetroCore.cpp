#include "pch.h"
#include "RetroCore.h"
#include "CoreDll.h"
#include "DataPaths.h"
#include "Bootstrap.h"
#include "libretro.h"
#include "XAudio2Output.h"
#include <vfs/vfs.h>
#include <vfs/vfs_implementation.h>

#include <cstring>
#include <algorithm>
#include <cstdarg>
#include <string>
#include <vector>
#include <chrono>
#include <windows.h>

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

using namespace scummvm_uwp;

// --- static state ---
std::atomic<bool> RetroCore::s_loaded{ false };
std::atomic<bool> RetroCore::s_initialized{ false };
std::atomic<bool> RetroCore::s_stopRequested{ false };
std::atomic<bool> RetroCore::s_shutdownRequested{ false };
std::atomic<bool> RetroCore::s_exitRequested{ false };
std::atomic<bool> RetroCore::s_paused{ false };
std::atomic<bool> RetroCore::s_emuUnloaded{ false };
std::atomic<bool> RetroCore::s_loadInProgress{ false };
std::atomic<int> RetroCore::s_loadResult{ -1 };
std::atomic<uint64_t> RetroCore::s_emulatedFrameCount{ 0 };
std::atomic<double> RetroCore::s_targetFps{ 60.0 };
std::atomic<unsigned> RetroCore::s_latestW{ 0 };
std::atomic<unsigned> RetroCore::s_latestH{ 0 };

std::atomic<bool> RetroCore::s_keyboardState[RETROK_LAST] = {};
std::atomic<retro_keyboard_event_t> RetroCore::s_keyboardCallback{ nullptr };
retro_log_printf_t RetroCore::s_logCallback = nullptr;
std::atomic<bool> RetroCore::s_joypadState[16] = {};
std::atomic<int16_t> RetroCore::s_analogState[4] = {};
std::atomic<int> RetroCore::s_mouseRelX{ 0 };
std::atomic<int> RetroCore::s_mouseRelY{ 0 };
std::atomic<int> RetroCore::s_mouseWheel{ 0 };
std::atomic<int> RetroCore::s_frameMouseX{ 0 };
std::atomic<int> RetroCore::s_frameMouseY{ 0 };
std::atomic<int> RetroCore::s_frameMouseWheel{ 0 };
#ifdef MOUSE_SUPPORT
std::atomic<bool> RetroCore::s_mouseBtnLeft{ false };
std::atomic<bool> RetroCore::s_mouseBtnRight{ false };
std::atomic<bool> RetroCore::s_mouseBtnMiddle{ false };
#endif
std::atomic<float> RetroCore::s_ptrX{ 0.0f };
std::atomic<float> RetroCore::s_ptrY{ 0.0f };
std::atomic<bool> RetroCore::s_ptrDown{ false };

std::mutex RetroCore::s_optionMutex;
std::map<std::string, std::string> RetroCore::s_optionValues;
bool RetroCore::s_optionValuesChanged = false;

std::mutex RetroCore::s_cmdMutex;
std::condition_variable RetroCore::s_cmdCv;
std::deque<RetroCore::Command> RetroCore::s_cmdQueue;

RetroCore::FrameSlot RetroCore::s_frameSlots[RetroCore::FRAME_SLOTS] = {};
std::atomic<uint8_t> RetroCore::s_frameState[RetroCore::FRAME_SLOTS] = { 0, 0, 0 };
std::atomic<uint64_t> RetroCore::s_frameSeq[RetroCore::FRAME_SLOTS] = { 0, 0, 0 };
std::atomic<int> RetroCore::s_readSlot{ -1 };

// Seq of the last frame the UI actually presented. AcquireFrame only returns
// frames newer than this, so when the producer runs slower than the consumer
// (game < UI presentation rate) the screen never jumps backwards through
// stale published frames ("moves forward, then returns" blink).
static std::atomic<uint64_t> s_lastPresentedSeq{ 0 };

std::atomic<bool> RetroCore::s_vsyncEnabled{ false };
std::atomic<float> RetroCore::s_displayRefreshRate{ 60.0f };
XAudio2Output* RetroCore::s_audioOutput = nullptr;

static retro_pixel_format s_pixelFormat = RETRO_PIXEL_FORMAT_RGB565;

RetroCore::RetroCore() {}
RetroCore::~RetroCore() { Shutdown(); }

static bool retro_env_wrap(unsigned cmd, void* data)
{
    return RetroCore::retro_env(cmd, data) != 0;
}

bool RetroCore::Init()
{
    if (m_threadStarted.exchange(true))
        return true;

    s_stopRequested.store(false);
    m_emuThread = std::thread(&RetroCore::EmulationThreadMain, this);
    return true;
}

bool RetroCore::InitCore()
{
    OutputDebugStringA("[scummvm-uwp] RetroCore::InitCore enter\n");

    OutputDebugStringA("[scummvm-uwp] retro_set_environment\n");
    CoreDll::retro_set_environment(retro_env_wrap);
    OutputDebugStringA("[scummvm-uwp] retro_set_video_refresh\n");
    CoreDll::retro_set_video_refresh(&RetroCore::retro_video);
    OutputDebugStringA("[scummvm-uwp] retro_set_audio_sample\n");
    CoreDll::retro_set_audio_sample(&RetroCore::retro_audio_mono);
    OutputDebugStringA("[scummvm-uwp] retro_set_audio_sample_batch\n");
    CoreDll::retro_set_audio_sample_batch(&RetroCore::retro_audio);
    OutputDebugStringA("[scummvm-uwp] retro_set_input_poll\n");
    CoreDll::retro_set_input_poll(&RetroCore::retro_input_poll);
    OutputDebugStringA("[scummvm-uwp] retro_set_input_state\n");
    CoreDll::retro_set_input_state(&RetroCore::retro_input_state);

    OutputDebugStringA("[scummvm-uwp] retro_init call\n");
    CoreDll::retro_init();

    OutputDebugStringA("[scummvm-uwp] RetroCore::InitCore exit OK\n");
    return true;
}

// RetroArch-style frame pacer for the emulation thread. RetroArch's runloop
// is single-threaded: Present(vsync) blocks the loop at the display rate,
// GET_THROTTLE_STATE reports that rate, and the core generates exactly
// sample_rate/rate samples per frame — matching the 48 kHz device, so the
// audio ring stays balanced and never bangs (see XAudio2Output.h). Our
// emulation runs on its own thread, so we reproduce the same cadence with a
// QPC accumulator.
//
// Rate = the core's own fps (RetroArch audio-sync model, runloop.c:8037):
// the emulation runs at 100% game speed regardless of display refresh. A 60Hz
// display simply presents the newest frame from the video ring (dup/skip),
// exactly like RetroArch. Self-consistency: the core generates
// sample_rate/rate samples per frame → ring stays balanced. Pacing at the
// display refresh instead would force the game to display_refresh/core_fps
// speed (85.6% on a 60Hz display) and trip the core's internal frame-skip.
//
// The wait uses a high-resolution waitable timer
// (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) because timeBeginPeriod() is not
// available to UWP apps and plain Sleep() granularity is ~15.6ms — too
// coarse for 60/70Hz pacing.
void RetroCore::PaceFrame()
{
    static HANDLE s_timer = [] {
        return CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }();
    static LARGE_INTEGER s_freq{};
    static LARGE_INTEGER s_next{};
    static bool s_inited = false;

    double rate = s_targetFps.load();
    if (!(rate > 1.0))
        rate = 60.0;

    if (!s_inited)
    {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_next);
        s_inited = true;
        return; // first call starts the cadence now (no delay at load)
    }

    LARGE_INTEGER period;
    period.QuadPart = (LONGLONG)(s_freq.QuadPart / rate);
    s_next.QuadPart += period.QuadPart;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (s_next.QuadPart <= now.QuadPart)
    {
        // Behind schedule (heavy frame): restart the cadence, no wait.
        s_next.QuadPart = now.QuadPart + period.QuadPart;
        return;
    }

    long long waitUs = (s_next.QuadPart - now.QuadPart) * 1000000LL / s_freq.QuadPart;
    if (waitUs <= 0 || !s_timer)
        return;

    // Negative 100ns units = relative wait from now.
    LARGE_INTEGER due;
    due.QuadPart = -waitUs * 10;
    if (SetWaitableTimer(s_timer, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(s_timer, INFINITE);
}

void RetroCore::EmulationThreadMain()
{
    spdlog::info("[RetroCore] emulation thread started (tid=0x{:08X})", (uint32_t)GetCurrentThreadId());
    BootTrace(L"emu: thread enter");

    s_initialized.store(InitCore());
    if (!s_initialized.load())
    {
        spdlog::error("[RetroCore] InitCore FAILED on emulation thread");
        BootTrace(L"emu: retro_init FAILED");
        return;
    }
    BootTrace(L"emu: retro_init ok");

    while (!s_stopRequested.load())
    {
        bool didWork = ProcessCommands();
        if (s_stopRequested.load())
            break;

        if (s_loaded.load())
        {
            if (s_paused.load())
            {
                Sleep(2);
            }
            else
            {
                RunFrame();

                // Core requested shutdown (RETRO_ENVIRONMENT_SHUTDOWN).
                if (s_shutdownRequested.exchange(false))
                {
                    spdlog::info("[RetroCore] core requested SHUTDOWN — unloading game");
                    UnloadGameInternal();
                    s_emuUnloaded.store(true);
                }
                else
                {
                    // RetroArch-style frame pacing after the frame, before the
                    // next retro_run. The audio ring is now a follower — this
                    // QPC timer IS the frame clock.
                    PaceFrame();
                }
            }
        }
        else if (!didWork)
        {
            // No game loaded: idle until a command arrives (or stop).
            std::unique_lock<std::mutex> lk(s_cmdMutex);
            s_cmdCv.wait_for(lk, std::chrono::milliseconds(50),
                [this] { return s_stopRequested.load() || !s_cmdQueue.empty(); });
        }
    }

    if (s_loaded.load())
        UnloadGameInternal();
    if (s_initialized.load())
    {
        spdlog::info("[RetroCore] retro_deinit");
        CoreDll::retro_deinit();
        s_initialized.store(false);
    }
    spdlog::info("[RetroCore] emulation thread exiting");
}

void RetroCore::EnqueueCommand(Command cmd)
{
    {
        std::lock_guard<std::mutex> lk(s_cmdMutex);
        s_cmdQueue.push_back(std::move(cmd));
    }
    s_cmdCv.notify_one();
}

bool RetroCore::ProcessCommands()
{
    bool handled = false;
    std::deque<Command> batch;
    {
        std::lock_guard<std::mutex> lk(s_cmdMutex);
        batch.swap(s_cmdQueue);
    }

    for (auto& cmd : batch)
    {
        handled = true;
        switch (cmd.type)
        {
        case CoreCommand::LoadGame:
        {
            spdlog::info("[RetroCore] cmd: LoadGame");
            s_loadInProgress.store(true);
            bool ok = LoadGameInternal(cmd.path, cmd.romData);
            s_loadResult.store(ok ? 1 : 0);
            s_loadInProgress.store(false);
            break;
        }
        case CoreCommand::UnloadGame:
            spdlog::info("[RetroCore] cmd: UnloadGame");
            UnloadGameInternal();
            break;
        case CoreCommand::Shutdown:
            s_stopRequested.store(true);
            break;
        }
    }
    return handled;
}

void RetroCore::LoadGame(const std::wstring& uwpPath, const std::vector<uint8_t>& romData)
{
    if (!m_threadStarted.load())
        Init();

    Command cmd;
    cmd.type = CoreCommand::LoadGame;
    cmd.path = uwpPath;
    cmd.romData = romData;
    EnqueueCommand(std::move(cmd));
}

void RetroCore::UnloadGame()
{
    if (!m_threadStarted.load())
        return;

    Command cmd;
    cmd.type = CoreCommand::UnloadGame;
    EnqueueCommand(std::move(cmd));
}

void RetroCore::Shutdown()
{
    if (m_threadJoined.exchange(true))
        return;

    if (m_threadStarted.load())
    {
        s_stopRequested.store(true);
        s_cmdCv.notify_all();
        if (m_emuThread.joinable())
            m_emuThread.join();
    }
}

void RetroCore::Pause()  { s_paused.store(true); }
void RetroCore::Resume() { s_paused.store(false); }

bool RetroCore::IsLoaded() const { return s_loaded.load(); }
bool RetroCore::IsInitialized() const { return s_initialized.load(); }

bool RetroCore::LoadGameInternal(const std::wstring& uwpPath, const std::vector<uint8_t>& romData)
{
    OutputDebugStringA("[scummvm-uwp] LoadGameInternal enter\n");
    BootTrace(L"emu: load_game begin");
    if (!s_initialized.load())
    {
        OutputDebugStringA("[scummvm-uwp] LoadGameInternal FAILED: not initialized\n");
        BootTrace(L"emu: load_game FAILED (not initialized)");
        return false;
    }

    if (uwpPath.empty() && romData.empty())
    {
        // No-game boot: ScummVM shows its own GUI (SET_SUPPORT_NO_GAME).
        OutputDebugStringA("[scummvm-uwp] CoreDll::retro_load_game(NULL) no-game boot\n");
        if (!CoreDll::retro_load_game(nullptr))
        {
            spdlog::error("[RetroCore] CoreDll::retro_load_game(NULL) FAILED");
            BootTrace(L"emu: retro_load_game(NULL) FAILED");
            return false;
        }
    }
    else
    {
        char buf[512];
        int len = WideCharToMultiByte(CP_UTF8, 0, uwpPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0)
        {
            OutputDebugStringA("[scummvm-uwp] LoadGameInternal FAILED: WideCharToMultiByte len=0\n");
            return false;
        }
        std::string pathUtf8(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, uwpPath.c_str(), -1, &pathUtf8[0], len, nullptr, nullptr);
        sprintf_s(buf, "[scummvm-uwp] LoadGameInternal path: %s data=%zu bytes\n", pathUtf8.c_str(), romData.size());
        OutputDebugStringA(buf);

        retro_game_info info = {};
        info.path = pathUtf8.c_str();
        info.data = romData.empty() ? nullptr : romData.data();
        info.size = romData.size();

        OutputDebugStringA("[scummvm-uwp] retro_load_game call\n");
        if (!CoreDll::retro_load_game(&info))
        {
            spdlog::error("[RetroCore] retro_load_game FAILED");
            return false;
        }
    }

    // Fetch AV info: populates core's internal av_info (fps, sample_rate)
    retro_system_av_info av = {};
    CoreDll::retro_get_system_av_info(&av);
    {
        char buf2[256];
        sprintf_s(buf2, "[scummvm-uwp] av_info: %dx%d @ %.2fHz, sample_rate=%.0f\n",
            av.geometry.base_width, av.geometry.base_height,
            av.timing.fps, av.timing.sample_rate);
        OutputDebugStringA(buf2);
    }
    if (av.timing.fps > 0)
        s_targetFps.store(av.timing.fps);

    s_loaded.store(true);
    s_lastPresentedSeq.store(0, std::memory_order_release);
    OutputDebugStringA("[scummvm-uwp] retro_load_game SUCCESS\n");
    BootTrace(L"emu: load_game ok");
    return true;
}

void RetroCore::UnloadGameInternal()
{
    if (!s_loaded.load())
    {
        s_shutdownRequested.store(false);
        return;
    }
    OutputDebugStringA("[scummvm-uwp] UnloadGameInternal\n");
    s_loaded.store(false);
    s_lastPresentedSeq.store(0, std::memory_order_release);
    CoreDll::retro_unload_game();
    // Reset all per-game state so next load starts clean
    s_shutdownRequested.store(false);
    for (auto& k : s_keyboardState)
        k.store(false);
    for (auto& j : s_joypadState)
        j.store(false);
    {
        std::lock_guard<std::mutex> lk(s_optionMutex);
        s_optionValues.clear();
        s_optionValuesChanged = false;
    }
}

void RetroCore::RunFrame()
{
    if (!s_loaded.load())
        return;

    // DIAGNOSTIC: nrun.txt skips retro_run (load_game ran) — isolates
    // load_game vs run as the process killer.
    static bool s_runSkipped = false;
    if (!s_runSkipped && scummvm_uwp::Bootstrap::FileExistsInLocalState(L"nrun.txt"))
    {
        s_runSkipped = true;
        spdlog::warn("[RetroCore] nrun.txt present — skipping retro_run (diagnostic)");
    }
    if (s_runSkipped)
    {
        Sleep(2);
        return;
    }

    static int frameCount = 0;
    frameCount++;
    if (frameCount == 1)
        BootTrace(L"emu: first retro_run begin");
    if (frameCount == 1 || (frameCount % 60) == 0)
    {
        spdlog::info("[RetroCore] RunFrame #{}", frameCount);
        char hb[120];
        sprintf_s(hb, "emu: frame #%d (tid=0x%04X)", frameCount, (unsigned)GetCurrentThreadId());
        BootTrace(hb);
    }
    if ((frameCount % 600) == 0)
    {
        char hb[80];
        sprintf_s(hb, "emu: run heartbeat #%d", frameCount);
        BootTrace(hb);
    }

    // DIAGNOSTIC: pause.txt holds the emu thread 120s before the first
    // retro_run so a debugger can attach and set death breakpoints.
    static bool s_pauseDone = false;
    if (!s_pauseDone && scummvm_uwp::Bootstrap::FileExistsInLocalState(L"pause.txt"))
    {
        s_pauseDone = true;
        spdlog::info("[RetroCore] pause.txt present — holding 120s before first retro_run");
        for (int i = 0; i < 120 && !s_stopRequested.load(); i++)
            Sleep(1000);
        spdlog::info("[RetroCore] pause done — resuming");
    }

    // Snapshot per-frame mouse deltas accumulated by the UI thread since the
    // last frame; the core reads these during retro_run via retro_input_state.
    s_frameMouseX.store(s_mouseRelX.exchange(0));
    s_frameMouseY.store(s_mouseRelY.exchange(0));
    s_frameMouseWheel.store(s_mouseWheel.exchange(0));

    LARGE_INTEGER t1, t2, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t1);
    // SEH wrapper: a libco coroutine overflow (0xC00000FD) or AV raised inside
    // the core's scummvm_main propagates here through co_switch/retro_run.
    static int sehRunCount = 0;
    __try
    {
        if (frameCount <= 3 || (frameCount % 600) == 0)
            BootTrace(L"emu: retro_run() calling");
        CoreDll::retro_run();
        if (frameCount <= 3 || (frameCount % 600) == 0)
            BootTrace(L"emu: retro_run() returned");
    }
    __except (sehRunCount++, 1)
    {
        spdlog::error("[RetroCore] retro_run SEH exception code=0x{:08X} (count={})", (unsigned)GetExceptionCode(), sehRunCount);
        char seh[128];
        sprintf_s(seh, "emu: retro_run SEH exception code=0x%08X count=%d", (unsigned)GetExceptionCode(), sehRunCount);
        BootTrace(seh);
    }    QueryPerformanceCounter(&t2);

    s_emulatedFrameCount.fetch_add(1);

    if ((frameCount % 600) == 0)
    {
        double ms = (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;
        char buf[128];
        sprintf_s(buf, "[scummvm-uwp] RunFrame #%d took %.1fms\n", frameCount, ms);
        OutputDebugStringA(buf);
    }
}

bool RetroCore::AcquireFrame(FrameView& out)
{
    // Pick the NEWEST published frame (highest seq) so the UI never displays
    // an older frame after a newer one (producer overwrites stale slots).
    // Additionally, only accept frames NEWER than the last presented one:
    // when the producer runs slower than the consumer, published slots hold
    // frames older than what is already on screen — presenting them would
    // make the picture move backwards. Returning false keeps the current
    // swap-chain content on screen until a genuinely newer frame arrives.
    int best = -1;
    uint64_t bestSeq = 0;
    uint64_t lastShown = s_lastPresentedSeq.load(std::memory_order_acquire);
    for (int i = 0; i < FRAME_SLOTS; i++)
    {
        if (s_frameState[i].load(std::memory_order_acquire) == 2)
        {
            uint64_t s = s_frameSeq[i].load(std::memory_order_acquire);
            if (s > lastShown && (best < 0 || s > bestSeq))
            {
                best = i;
                bestSeq = s;
            }
        }
    }
    if (best >= 0)
    {
        uint8_t expected = 2;
        if (s_frameState[best].compare_exchange_strong(expected, 3))
        {
            s_readSlot.store(best);
            s_lastPresentedSeq.store(bestSeq, std::memory_order_release);
            out.data = s_frameSlots[best].data.empty() ? nullptr : s_frameSlots[best].data.data();
            out.w = s_frameSlots[best].w;
            out.h = s_frameSlots[best].h;
            out.pitch = s_frameSlots[best].pitch;
            return out.data != nullptr;
        }
    }
    return false;
}

void RetroCore::ReleaseFrame()
{
    int i = s_readSlot.exchange(-1);
    if (i >= 0 && i < FRAME_SLOTS)
        s_frameState[i].store(0);
}

bool RetroCore::IsShutdownRequested() { return s_exitRequested.load(); }
bool RetroCore::ConsumeUnloadEvent() { return s_emuUnloaded.exchange(false); }
bool RetroCore::IsLoadInProgress() { return s_loadInProgress.load(); }
int RetroCore::ConsumeLoadResult() { return s_loadResult.exchange(-1); }
uint64_t RetroCore::GetEmulatedFrameCount() { return s_emulatedFrameCount.load(); }
double RetroCore::GetTargetFps() const { return s_targetFps.load(); }
unsigned RetroCore::GetFrameWidth() { return s_latestW.load(); }
unsigned RetroCore::GetFrameHeight() { return s_latestH.load(); }
retro_pixel_format RetroCore::GetPixelFormat() { return s_pixelFormat; }

static void RETRO_CALLCONV uwp_log(enum retro_log_level level, const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    spdlog::info("[core] {}", buf);
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
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: return "GET_LIBRETRO_PATH";
    case RETRO_ENVIRONMENT_GET_LANGUAGE: return "GET_LANGUAGE";
    case RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY: return "GET_PLAYLIST_DIRECTORY";
    case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE: return "GET_MIDI_INTERFACE";
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: return "GET_AUDIO_VIDEO_ENABLE";
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return "GET_INPUT_BITMASKS";
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return "SET_SUPPORT_NO_GAME";
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return "SET_CONTROLLER_INFO";
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return "SET_INPUT_DESCRIPTORS";
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: return "SET_CORE_OPTIONS_DISPLAY";
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY: return "SET_MINIMUM_AUDIO_LATENCY";
    case RETRO_ENVIRONMENT_SET_GEOMETRY: return "SET_GEOMETRY";
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: return "SET_SYSTEM_AV_INFO";
    default: return "UNKNOWN";
    }
}

void RetroCore::SetKeyState(unsigned key, bool down)
{
    if (key < RETROK_LAST)
    {
        s_keyboardState[key].store(down);
        auto cb = s_keyboardCallback.load();
        if (cb)
            cb(down, key, 0, 0);
    }
}

void RetroCore::SetJoypadButton(unsigned id, bool held)
{
    if (id < 16)
        s_joypadState[id].store(held);
}

void RetroCore::SetAnalogAxis(unsigned index, unsigned id, int16_t val)
{
    if (index < 2 && id < 2)
        s_analogState[index * 2 + id].store(val);
}

void RetroCore::SetMouseMove(int relX, int relY)
{
    s_mouseRelX.fetch_add(relX);
    s_mouseRelY.fetch_add(relY);
}

void RetroCore::SetPointer(float x, float y, bool down)
{
    s_ptrX.store(x);
    s_ptrY.store(y);
    s_ptrDown.store(down);
}

#ifdef MOUSE_SUPPORT
void RetroCore::SetMouseButton(unsigned btn, bool down)
{
    switch (btn)
    {
    case 1: s_mouseBtnLeft.store(down); break;
    case 2: s_mouseBtnRight.store(down); break;
    case 3: s_mouseBtnMiddle.store(down); break;
    }
}

void RetroCore::SetMouseWheel(int delta)
{
    s_mouseWheel.fetch_add(delta);
}

void RetroCore::GetPointer(short& mx, short& my)
{
    mx = (short)((s_ptrX.load() * 2.0f - 1.0f) * 0x7fff);
    my = (short)((s_ptrY.load() * 2.0f - 1.0f) * 0x7fff);
}
#endif

void RetroCore::SetOptionValue(const char* key, const char* value)
{
    if (key)
    {
        std::lock_guard<std::mutex> lk(s_optionMutex);
        s_optionValues[key] = (value ? value : "");
        s_optionValuesChanged = true;
    }
}

int RetroCore::retro_env(unsigned cmd, void* data)
{
    char buf[256];
#ifdef FRAME_TRACE
    sprintf_s(buf, "[scummvm-uwp] retro_env cmd=%d(%s)\n", cmd, retro_env_name(cmd));
    OutputDebugStringA(buf);
#endif

    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
    {
        auto* vfs_info = static_cast<retro_vfs_interface_info*>(data);
        vfs_info->required_interface_version = 3;
        vfs_info->iface = &uwp_vfs_iface;
        sprintf_s(buf, "[scummvm-uwp]   VFS interface provided (v3, iface=%p)\n", (void*)vfs_info->iface);
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    {
        // Set by Bootstrap before core init: E:\scummvm\system on Xbox,
        // LocalState\system as fallback.
        *static_cast<const char**>(data) = DataPaths::g_systemDirUtf8.c_str();
        sprintf_s(buf, "[scummvm-uwp]   SYSTEM_DIR=%s\n", DataPaths::g_systemDirUtf8.c_str());
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    {
        // Set by Bootstrap before core init: E:\scummvm\saves on Xbox,
        // LocalState\saves as fallback.
        *static_cast<const char**>(data) = DataPaths::g_saveDirUtf8.c_str();
        sprintf_s(buf, "[scummvm-uwp]   SAVE_DIR=%s\n", DataPaths::g_saveDirUtf8.c_str());
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
        auto fmt = *static_cast<retro_pixel_format*>(data);
        s_pixelFormat = fmt;
        const char* fmtName = (fmt == RETRO_PIXEL_FORMAT_0RGB1555 ? "0RGB1555" :
            fmt == RETRO_PIXEL_FORMAT_XRGB8888 ? "XRGB8888" :
            fmt == RETRO_PIXEL_FORMAT_RGB565 ? "RGB565" : "UNKNOWN");
        sprintf_s(buf, "[scummvm-uwp]   SET_PIXEL_FORMAT=%s(%d)\n", fmtName, fmt);
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
        OutputDebugStringA("[scummvm-uwp]   SET_HW_RENDER=REJECTED (return 0)\n");
        return 0;
    }
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
    {
        // audio + video both enabled (never muted while running)
        *static_cast<int*>(data) = 3;
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
    {
        static std::string corePath;
        if (corePath.empty() && CoreDll::GetPath())
        {
            const wchar_t* w = CoreDll::GetPath();
            int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0)
            {
                corePath.resize(len);
                WideCharToMultiByte(CP_UTF8, 0, w, -1, &corePath[0], len, nullptr, nullptr);
            }
        }
        *static_cast<const char**>(data) = corePath.c_str();
        sprintf_s(buf, "[scummvm-uwp]   LIBRETRO_PATH=%s\n", corePath.c_str());
        OutputDebugStringA(buf);
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        // English default
        return 0;
    case RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE:
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return 0;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        return 1;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
    {
        auto* msg = static_cast<const retro_message_ext*>(data);
        OutputDebugStringA("[scummvm-uwp] ");
        OutputDebugStringA(msg->msg);
        OutputDebugStringA("\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
    {
        auto* cb = static_cast<const retro_keyboard_callback*>(data);
        s_keyboardCallback.store(cb ? cb->callback : nullptr);
        OutputDebugStringA("[scummvm-uwp]   SET_KEYBOARD_CALLBACK: stored\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE:
    {
        auto* state = static_cast<retro_throttle_state*>(data);
        float refresh = s_displayRefreshRate.load();
        double core = s_targetFps.load();
        // Self-consistent with PaceFrame(). VSync caps the rate only when
        // the display is SLOWER than the core fps (RetroArch runloop.c:3410);
        // otherwise the core fps governs. The core then generates exactly
        // sample_rate/rate samples per frame, matching the 48 kHz device, so
        // the audio ring stays balanced. Mode is VSYNC in the capped case,
        // NONE otherwise — both mean the emulation thread is the pace clock.
        if (s_vsyncEnabled.load() && refresh > 0.0f && (double)refresh < core)
        {
            state->mode = RETRO_THROTTLE_VSYNC;
            state->rate = refresh;
        }
        else
        {
            state->mode = RETRO_THROTTLE_NONE;
            state->rate = (float)core;
        }
        return 1;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    {
        auto* cb = static_cast<retro_log_callback*>(data);
        cb->log = uwp_log;
        OutputDebugStringA("[scummvm-uwp]   GET_LOG_INTERFACE: log callback provided\n");
        return 1;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
        OutputDebugStringA("[scummvm-uwp] SHUTDOWN requested\n");
        s_shutdownRequested.store(true);
        // Persistent exit signal: consumed by the UI thread (Update) to call
        // CoreApplication::Exit(). Never reset — the emu thread's RunFrame
        // consumes s_shutdownRequested (not this) for the graceful unload.
        s_exitRequested.store(true);
        return 1;
    case RETRO_ENVIRONMENT_SET_VARIABLE:
    {
        auto* var = static_cast<const retro_variable*>(data);
        if (var && var->key)
        {
            const char* val = var->value ? var->value : "(default)";
            char kbuf[512];
            sprintf_s(kbuf, "[scummvm-uwp]   SET_VARIABLE: %s = %s\n", var->key, val);
            OutputDebugStringA(kbuf);
            {
                std::lock_guard<std::mutex> lk(s_optionMutex);
                s_optionValues[var->key] = (var->value ? var->value : "");
                s_optionValuesChanged = true;
            }
            return 1;
        }
        return 0;
    }
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
    {
        int* version = static_cast<int*>(data);
        if (version)
        {
            *version = 2;  // Support SET_CORE_OPTIONS_V2
            OutputDebugStringA("[scummvm-uwp]   GET_CORE_OPTIONS_VERSION: 2\n");
        }
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    {
        auto* opts = static_cast<const retro_core_options_v2*>(data);
        if (opts && opts->definitions)
        {
            std::lock_guard<std::mutex> lk(s_optionMutex);
            for (int i = 0; opts->definitions[i].key; ++i)
            {
                const auto& def = opts->definitions[i];
                const char* key = def.key;
                const char* defVal = def.default_value;
                // Only store if not already set by the user
                if (s_optionValues.find(key) == s_optionValues.end() && defVal)
                {
                    s_optionValues[key] = defVal;
                }
            }
            OutputDebugStringA("[scummvm-uwp]   SET_CORE_OPTIONS_V2: registered defaults\n");
        }
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    {
        auto* intl = static_cast<const retro_core_options_v2_intl*>(data);
        if (intl && intl->us && intl->us->definitions)
        {
            std::lock_guard<std::mutex> lk(s_optionMutex);
            for (int i = 0; intl->us->definitions[i].key; ++i)
            {
                const auto& def = intl->us->definitions[i];
                const char* key = def.key;
                const char* defVal = def.default_value;
                if (s_optionValues.find(key) == s_optionValues.end() && defVal)
                {
                    s_optionValues[key] = defVal;
                }
            }
            OutputDebugStringA("[scummvm-uwp]   SET_CORE_OPTIONS_V2_INTL: registered defaults\n");
        }
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        return 1;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return 1;
     case RETRO_ENVIRONMENT_GET_VARIABLE:
     {
         auto* var = static_cast<retro_variable*>(data);
         if (var && var->key)
         {
             std::lock_guard<std::mutex> lk(s_optionMutex);
            auto it = s_optionValues.find(var->key);
            if (it != s_optionValues.end())
            {
                if (!it->second.empty())
                {
                    var->value = it->second.c_str();
#ifdef FRAME_TRACE
                    char kbuf[256];
                    sprintf_s(kbuf, "[scummvm-uwp]   GET_VARIABLE(%s) = %s\n", var->key, it->second.c_str());
                    OutputDebugStringA(kbuf);
#endif
                    return 1;
                }
                // Empty value means "use default" — fall through to the
                // built-in defaults below.
            }
            // The core never declares its options via SET_VARIABLES/SET_CORE_OPTIONS —
            // it only issues ~30 GET_VARIABLE queries at boot and expects a RetroArch
            // run to answer them from its config. Deliver the core's own defaults
            // (matching libretro-core-options.h) so option-driven behavior matches a
            // RetroArch run. In particular scummvm_video_hw_acceleration=disabled
            // keeps the core in SOFTWARE mode (RGB565 real-pitch frames); without it
            // the core emits RETRO_HW_FRAME_BUFFER_VALID frames with pitch=0, which
            // this frontend cannot present.
            static const struct { const char* key; const char* value; } kCoreDefaults[] = {
                { "scummvm_pointer_device", "retropad" },
                { "scummvm_gamepad_cursor_speed", "1.0" },
                { "scummvm_gamepad_cursor_acceleration_time", "0.2" },
                { "scummvm_analog_response", "linear" },
                { "scummvm_analog_deadzone", "15" },
                { "scummvm_mouse_speed", "1.0" },
                { "scummvm_mouse_fine_control_speed_reduction", "4" },
                { "scummvm_framerate", "disabled" },
                { "scummvm_samplerate", "48000" },
                { "scummvm_mapper_up", "RETROKE_UP" },
                { "scummvm_mapper_down", "RETROKE_DOWN" },
                { "scummvm_mapper_left", "RETROKE_LEFT" },
                { "scummvm_mapper_right", "RETROKE_RIGHT" },
                { "scummvm_mapper_a", "RETROK_SPACE" },
                { "scummvm_mapper_b", "RETROK_RETURN" },
                { "scummvm_mapper_x", "RETROK_F5" },
                { "scummvm_mapper_y", "RETROK_ESCAPE" },
                { "scummvm_mapper_select", "RETROKE_VKBD" },
                { "scummvm_mapper_start", "RETROKE_SCUMMVM_GUI" },
                { "scummvm_mapper_l", "RETROKE_LEFT_BUTTON" },
                { "scummvm_mapper_r", "RETROKE_RIGHT_BUTTON" },
                { "scummvm_mapper_l2", "---" },
                { "scummvm_mapper_r2", "RETROKE_FINE_CONTROL" },
                { "scummvm_mapper_l3", "---" },
                { "scummvm_mapper_r3", "---" },
                { "scummvm_mapper_lu", "RETROKE_UP" },
                { "scummvm_mapper_ld", "RETROKE_DOWN" },
                { "scummvm_mapper_ll", "RETROKE_LEFT" },
                { "scummvm_mapper_lr", "RETROKE_RIGHT" },
                { "scummvm_mapper_ru", "RETROK_UP" },
                { "scummvm_mapper_rd", "RETROK_DOWN" },
                { "scummvm_mapper_rl", "RETROK_LEFT" },
                { "scummvm_mapper_rr", "RETROK_RIGHT" },
                { "scummvm_video_hw_acceleration", "disabled" },
                { "scummvm_gui_h_res", "720" },
                { "scummvm_gui_aspect_ratio", "1" },
            };
            for (const auto& d : kCoreDefaults)
            {
                if (strcmp(d.key, var->key) == 0)
                {
                    var->value = d.value;
#ifdef FRAME_TRACE
                    char kbuf[256];
                    sprintf_s(kbuf, "[scummvm-uwp]   GET_VARIABLE(%s) = %s (default)\n", var->key, d.value);
                    OutputDebugStringA(kbuf);
#endif
                    return 1;
                }
            }
#ifdef FRAME_TRACE
            char kbuf[256];
            sprintf_s(kbuf, "[scummvm-uwp]   GET_VARIABLE(%s) = NOT FOUND\n", var->key);
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
            std::lock_guard<std::mutex> lk(s_optionMutex);
            *changed = s_optionValuesChanged;
            s_optionValuesChanged = false;
        }
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    {
        auto* av = static_cast<const retro_system_av_info*>(data);
        if (av && av->timing.fps > 0)
            s_targetFps.store(av->timing.fps);
        char buf2[256];
        sprintf_s(buf2, "[scummvm-uwp]   SET_SYSTEM_AV_INFO: %dx%d @ %.2fHz sample_rate=%.0f\n",
            av->geometry.base_width, av->geometry.base_height,
            av->timing.fps, av->timing.sample_rate);
        OutputDebugStringA(buf2);
        return 1;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    {
        auto* geom = static_cast<const retro_game_geometry*>(data);
        char buf2[256];
        sprintf_s(buf2, "[scummvm-uwp]   SET_GEOMETRY: %dx%d aspect=%.2f\n",
            geom->base_width, geom->base_height, geom->aspect_ratio);
        OutputDebugStringA(buf2);
        return 1;
    }
    default:
#ifdef FRAME_TRACE
        sprintf_s(buf, "[scummvm-uwp]   UNSUPPORTED env cmd=%d\n", cmd);
        OutputDebugStringA(buf);
#endif
        return 0;
    }
}

static int g_videoFrameCount = 0;
static uint64_t g_frameSeqCounter = 0;

void RetroCore::retro_video(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0 || pitch == 0)
    {
        static int rejectCount = 0;
        rejectCount++;
        if (rejectCount == 1 || (rejectCount % 600) == 0)
        {
            spdlog::info("[scummvm-uwp] retro_video REJECTED #{}: data={:p} w={} h={} pitch={}", rejectCount, data, w, h, pitch);
        }
        return;
    }

    g_videoFrameCount++;
    if (g_videoFrameCount == 1)
    {
        spdlog::info("[scummvm-uwp] retro_video FIRST REAL FRAME: {}x{} pitch={}", w, h, pitch);
        char vf[128];
        sprintf_s(vf, "emu: first video frame %ux%u pitch=%zu", w, h, pitch);
        BootTrace(vf);
    }

    // Log resolution changes
    {
        static unsigned prevW = 0, prevH = 0;
        if (w != prevW || h != prevH)
        {
            prevW = w; prevH = h;
            spdlog::info("[scummvm-uwp] retro_video RESOLUTION CHANGE: {}x{} (frame #{})", w, h, g_videoFrameCount);
        }
    }

    // 1) Prefer a free slot (state 0). 2) Otherwise overwrite the OLDEST
    // published slot (lowest seq) so the newest frame stays available.
    // Monotonic seq lets AcquireFrame pick the newest published frame.
    int target = -1;
    for (int i = 0; i < FRAME_SLOTS; i++)
    {
        uint8_t expected = 0;
        if (s_frameState[i].compare_exchange_strong(expected, 1))
        {
            target = i;
            break;
        }
    }
    if (target < 0)
    {
        uint64_t oldestSeq = UINT64_MAX;
        for (int i = 0; i < FRAME_SLOTS; i++)
        {
            if (s_frameState[i].load() == 2)
            {
                uint64_t s = s_frameSeq[i].load();
                if (s < oldestSeq)
                {
                    oldestSeq = s;
                    target = i;
                }
            }
        }
        if (target >= 0)
        {
            uint8_t expected = 2;
            if (!s_frameState[target].compare_exchange_strong(expected, 1))
                target = -1; // UI grabbed it between scan and claim — drop this frame
        }
    }
    if (target < 0)
        return; // all slots busy reading/writing — drop this frame

    unsigned bpp = (unsigned)(pitch / w);
    if (bpp == 0)
        bpp = 4;

    // Renderers consume 4bpp BGRA8888. The ScummVM core produces RGB565 in
    // software mode (SET_PIXEL_FORMAT), so convert rows here on the emu thread.
    bool convert565 = (s_pixelFormat == RETRO_PIXEL_FORMAT_RGB565 && bpp == 2);
    unsigned dstBpp = convert565 ? 4 : bpp;
    unsigned rowBytes = w * dstBpp;
    size_t needed = (size_t)rowBytes * h;

    auto& slot = s_frameSlots[target];
    if (slot.data.size() < needed)
        slot.data.resize(needed);

    const uint8_t* src = (const uint8_t*)data;
    uint8_t* dst = slot.data.data();

    if (convert565)
    {
        for (unsigned y = 0; y < h; y++)
        {
            const uint16_t* srow = (const uint16_t*)(src + (size_t)y * pitch);
            uint8_t* drow = dst + (size_t)y * rowBytes;
            for (unsigned x = 0; x < w; x++)
            {
                uint16_t px = srow[x];
                uint8_t r5 = (uint8_t)(px >> 11);
                uint8_t g6 = (uint8_t)((px >> 5) & 0x3F);
                uint8_t b5 = (uint8_t)(px & 0x1F);
                drow[x*4 + 0] = (uint8_t)((b5 << 3) | (b5 >> 2));
                drow[x*4 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
                drow[x*4 + 2] = (uint8_t)((r5 << 3) | (r5 >> 2));
                drow[x*4 + 3] = 0xFF;
            }
        }
    }
    else if (pitch == rowBytes)
    {
        memcpy(dst, src, needed);
    }
    else
    {
        for (unsigned y = 0; y < h; y++)
            memcpy(dst + (size_t)y * rowBytes, src + (size_t)y * pitch, rowBytes);
    }

    slot.w = w;
    slot.h = h;
    slot.pitch = rowBytes;
    s_latestW.store(w);
    s_latestH.store(h);
    s_frameSeq[target].store(++g_frameSeqCounter, std::memory_order_release);
    s_frameState[target].store(2, std::memory_order_release);
}

void RetroCore::SetAudioOutput(XAudio2Output* output)
{
    s_audioOutput = output;
}

size_t RetroCore::retro_audio(const int16_t* data, size_t frames)
{
    static bool first = true;
    if (first)
    {
        first = false;
        spdlog::info("[scummvm-uwp] first audio_sample_batch: {} frames", (int)frames);
    }
    // RetroArch audio-backpressure model: Write() blocks inside retro_run
    // when the ring is full, so the audio clock paces the emulator.
    if (s_audioOutput)
        s_audioOutput->Write(data, (uint32_t)frames);
    return frames;
}

void RetroCore::retro_audio_mono(int16_t left, int16_t right)
{
    // Safety net: some cores call the mono callback instead of the batch one.
    int16_t frame[2] = { left, right };
    if (s_audioOutput)
        s_audioOutput->Write(frame, 1);
}

void RetroCore::retro_input_poll(void)
{
}

int16_t RetroCore::retro_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0) return 0;

    if (device == RETRO_DEVICE_KEYBOARD)
    {
        return (id < RETROK_LAST && s_keyboardState[id].load()) ? 1 : 0;
    }

    // WARNING: RETROK values (0-323) numerically overlap JOYPAD button IDs (0-15).
    // Previously this branch read from s_keyboardState, causing keyboard presses
    // to leak into JOYPAD queries. E.g. RETROK_RETURN=13 == RETRO_DEVICE_ID_JOYPAD_R2,
    // so Enter also registered as R2, which dosbox-pure maps to KBD_4 ("4").
    // Fixed by using a separate s_joypadState[] array — populated from physical
    // gamepad state in dosbox_uwpMain::Update().
    if (device == RETRO_DEVICE_JOYPAD)
    {
        if (id < 16 && s_joypadState[id].load())
            return 1;
        return 0;
    }

    if (device == RETRO_DEVICE_ANALOG)
    {
        if (index < 2 && id < 2)
            return s_analogState[index * 2 + id].load();
        return 0;
    }

    if (device == RETRO_DEVICE_MOUSE)
    {
        switch (id)
        {
        case RETRO_DEVICE_ID_MOUSE_X: return (int16_t)s_frameMouseX.load();
        case RETRO_DEVICE_ID_MOUSE_Y: return (int16_t)s_frameMouseY.load();
#ifdef MOUSE_SUPPORT
        case RETRO_DEVICE_ID_MOUSE_LEFT: return s_mouseBtnLeft.load() ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return s_mouseBtnRight.load() ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_MIDDLE: return s_mouseBtnMiddle.load() ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_WHEELUP: return (s_frameMouseWheel.load() > 0) ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_WHEELDOWN: return (s_frameMouseWheel.load() < 0) ? 1 : 0;
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
        case RETRO_DEVICE_ID_POINTER_X: return (int16_t)((s_ptrX.load() * 2.0f - 1.0f) * 0x7fff);
        case RETRO_DEVICE_ID_POINTER_Y: return (int16_t)((s_ptrY.load() * 2.0f - 1.0f) * 0x7fff);
        case RETRO_DEVICE_ID_POINTER_PRESSED: return s_ptrDown.load() ? 1 : 0;
        default: return 0;
        }
    }

    return 0;
}
