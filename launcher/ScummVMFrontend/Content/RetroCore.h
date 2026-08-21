#pragma once

#include "libretro.h"
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

struct retro_vfs_interface;

namespace scummvm_uwp
{
    // Threaded video model (RetroArch-style): the emulation thread owns
    // retro_run/retro_load_game/retro_deinit and is paced by the blocking
    // audio Write(); the UI (CoreWindow) thread only reads the latest frame
    // through a 3-slot ring and presents it. All shared state is atomic or
    // mutex-guarded. The core's own callbacks (retro_video, retro_audio,
    // retro_input_state, DBPS_*) run on the emulation thread.
    class RetroCore
    {
    public:
        RetroCore();
        ~RetroCore();

        // Starts the emulation thread (core init runs on it). Idempotent.
        bool Init();
        // Enqueue a game load. Completion is tracked via IsLoadInProgress()
        // + ConsumeLoadResult() (polled by the UI thread).
        void LoadGame(const std::wstring& uwpPath, const std::vector<uint8_t>& romData);
        void UnloadGame();
        // Stops and joins the emulation thread. Idempotent. Blocks until the
        // thread exits (audio Write timeout is the worst case, ~256ms).
        void Shutdown();
        void Pause();
        void Resume();

        bool IsLoaded() const;
        bool IsInitialized() const;

        // Frame ring — UI thread only.
        struct FrameView { const uint8_t* data; unsigned w; unsigned h; unsigned pitch; };
        static bool AcquireFrame(FrameView& out);
        static void ReleaseFrame();

        double GetTargetFps() const;

        static bool IsShutdownRequested();
        // True once the emulation thread unloaded the game after the core
        // requested SHUTDOWN. Consumed (cleared) by the UI thread.
        static bool ConsumeUnloadEvent();
        static bool IsLoadInProgress();
        // 1 = load ok, 0 = load failed, -1 = no result pending. Consumed.
        static int ConsumeLoadResult();
        static uint64_t GetEmulatedFrameCount();

        static std::atomic<bool> s_vsyncEnabled;
        static std::atomic<float> s_displayRefreshRate;

        // Latest published frame dimensions (HUD/DIAG display only).
        static unsigned GetFrameWidth();
        static unsigned GetFrameHeight();
        // Active core pixel format (from SET_PIXEL_FORMAT).
        static retro_pixel_format GetPixelFormat();

        // UI thread writes, emulation thread reads.
        static void SetKeyState(unsigned key, bool down);
        static void SetJoypadButton(unsigned id, bool held);
        static void SetAnalogAxis(unsigned index, unsigned id, int16_t val);
        static void SetOptionValue(const char* key, const char* value);
        static void SetMouseMove(int relX, int relY);
        static void SetPointer(float x, float y, bool down);
        static void SetAudioOutput(class XAudio2Output* output);
        static XAudio2Output* s_audioOutput;

        // GL context support — called by ScummVMMain during boot
        static std::atomic<bool> s_glContextReady;
        static void SetGLProcFunc(void* (*func)(const char*));

#ifdef MOUSE_SUPPORT
        static void SetMouseButton(unsigned btn, bool down);
        static void SetMouseWheel(int delta);
        static void GetPointer(short& mx, short& my);
#endif

    private:
        // --- emulation thread ---
        void EmulationThreadMain();
        bool InitCore();
        bool LoadGameInternal(const std::wstring& path, const std::vector<uint8_t>& romData);
        void UnloadGameInternal();
        void RunFrame();
        bool ProcessCommands();
        static void PaceFrame();

        enum class CoreCommand { LoadGame, UnloadGame, Shutdown };
        struct Command
        {
            CoreCommand type;
            std::wstring path;
            std::vector<uint8_t> romData;
        };
        void EnqueueCommand(Command cmd);

        std::thread m_emuThread;
        std::atomic<bool> m_threadStarted{ false };
        std::atomic<bool> m_threadJoined{ false };

        // Shared state (UI <-> emulation threads).
        static std::atomic<bool> s_loaded;
        static std::atomic<bool> s_initialized;
        static std::atomic<bool> s_stopRequested;
        static std::atomic<bool> s_shutdownRequested;
        static std::atomic<bool> s_exitRequested;
        static std::atomic<bool> s_paused;
        static std::atomic<bool> s_emuUnloaded;
        static std::atomic<bool> s_loadInProgress;
        static std::atomic<int> s_loadResult;
        static std::atomic<bool> s_osdActive;
        static std::atomic<bool> s_hwRenderAccepted;

        // GL callbacks
        static uintptr_t s_glGetFramebuffer();
        static void* s_glGetProcAddress(const char* name);
        static void s_glContextReset();
        static void s_glContextDestroy();
        static void* (*s_wglGetProcFunc)(const char*);
        static std::atomic<uint64_t> s_emulatedFrameCount;
        static std::atomic<double> s_targetFps;
        static std::atomic<unsigned> s_latestW;
        static std::atomic<unsigned> s_latestH;

        static std::atomic<bool> s_keyboardState[RETROK_LAST];
        static std::atomic<retro_keyboard_event_t> s_keyboardCallback;
        static retro_log_printf_t s_logCallback;
        static std::atomic<bool> s_joypadState[16];
        static std::atomic<int16_t> s_analogState[4]; // [L_X, L_Y, R_X, R_Y]
        static std::atomic<int> s_mouseRelX;
        static std::atomic<int> s_mouseRelY;
        static std::atomic<int> s_mouseWheel;
        static std::atomic<int> s_frameMouseX;
        static std::atomic<int> s_frameMouseY;
        static std::atomic<int> s_frameMouseWheel;
#ifdef MOUSE_SUPPORT
        static std::atomic<bool> s_mouseBtnLeft;
        static std::atomic<bool> s_mouseBtnRight;
        static std::atomic<bool> s_mouseBtnMiddle;
#endif
        static std::atomic<float> s_ptrX;
        static std::atomic<float> s_ptrY;
        static std::atomic<bool> s_ptrDown;

        // Options map — guarded because SET_VARIABLE/GET_VARIABLE run on the
        // emulation thread while the UI thread calls SetOptionValue().
        static std::mutex s_optionMutex;
        static std::map<std::string, std::string> s_optionValues;
        static bool s_optionValuesChanged;

        // Command queue (UI -> emulation thread).
        static std::mutex s_cmdMutex;
        static std::condition_variable s_cmdCv;
        static std::deque<Command> s_cmdQueue;

        // Frame ring. Slot states: 0=free, 1=writing, 2=published, 3=reading.
        // s_frameSeq is a monotonic per-slot counter (emulation thread writes,
        // UI reads) so the consumer always picks the NEWEST published frame —
        // without it, producer overwrite + lowest-index read can present frames
        // out of order (background "moves then returns").
        static const int FRAME_SLOTS = 3;
        struct FrameSlot { std::vector<uint8_t> data; unsigned w; unsigned h; unsigned pitch; };
        static FrameSlot s_frameSlots[FRAME_SLOTS];
        static std::atomic<uint8_t> s_frameState[FRAME_SLOTS];
        static std::atomic<uint64_t> s_frameSeq[FRAME_SLOTS];
        static std::atomic<int> s_readSlot;

    public:
        static int retro_env(unsigned cmd, void* data);
        static void retro_video(const void* data, unsigned w, unsigned h, size_t pitch);
        static size_t retro_audio(const int16_t* data, size_t frames);
        static void retro_audio_mono(int16_t left, int16_t right);
        static void retro_input_poll(void);
        static int16_t retro_input_state(unsigned port, unsigned device, unsigned index, unsigned id);
    };
}
