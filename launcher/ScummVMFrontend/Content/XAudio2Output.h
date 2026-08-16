#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <windows.h>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace scummvm_uwp
{
    // RetroArch xaudio.c model: fixed ring of MAX_BUFFERS sub-buffers,
    // voice starts at init, OnBufferEnd decrements the submitted counter
    // and signals the event.
    //
    // The ring is a pure FOLLOWING buffer, NOT the frame clock. RetroArch's
    // runloop is single-threaded: Present(vsync) blocks the loop at the
    // display rate, GET_THROTTLE_STATE reports that rate, and the core
    // generates exactly sample_rate/rate samples per frame — matching the
    // 48 kHz device, so the ring stays balanced and Write() almost never
    // blocks. Our emulation thread reproduces that cadence with a QPC timer
    // (PaceFrame in RetroCore.cpp) at the same self-consistent rate, so the
    // ring only absorbs drift/jitter. Blocking Write() here is a safety
    // valve for rare overrun, not the pacer.
    //
    // Sub-buffer granularity is LATENCY_MS each; the ring totals
    // MAX_BUFFERS * LATENCY_MS = 48ms of audio. RetroArch xaudio.c sizes the
    // ring to the driver latency itself (xa_init: bufsize = latency*rate/1000,
    // then split into MAX_BUFFERS=16 sub-buffers), so its total ring is the
    // ~64ms default, NOT 1.024s. Our port originally mistook 64ms for the
    // sub-buffer and queued ~960ms steady-state — the perceived latency. The
    // QPC pacer is the frame clock, so the ring only needs to absorb drift.
    // 48ms (4x12ms) is the stability floor on Xbox: CPU-bound games (Screamer
    // RunFrame ~15.9ms vs 16.7ms slot) stall on occasional 2-frame spikes;
    // tolerance = queued@block = 3*LATENCY. 24ms tolerance (4x8ms) cracked
    // (under=7-8/s), 48ms tolerance (4x16ms) was clean (under=0/s); 36ms is
    // the middle ground. Write() submits a sub-buffer as soon as it fills,
    // even when the input is exhausted (RetroArch xa_write parity), so a full
    // staging buffer is never deferred to the next retro_run. Blocking happens
    // at MAX_BUFFERS-1 queued (steady-state ~36-48ms). Wait in SubmitFullBuffer
    // is bounded by WRITE_TIMEOUT (XAUDIO_TIMEOUT parity) then drops the rest.
    class XAudio2Output
    {
    public:
        static const int MAX_BUFFERS = 4;
        static const int MAX_BUFFERS_MASK = MAX_BUFFERS - 1;
        static const uint32_t SAMPLE_RATE = 48000;
        static const uint32_t LATENCY_MS = 12;
        static const uint32_t SUBBUF_FRAMES = (LATENCY_MS * SAMPLE_RATE) / 1000; // 576
        static const uint32_t SUBBUF_BYTES = SUBBUF_FRAMES * 4; // stereo int16
        static const DWORD WRITE_TIMEOUT = 256; // ms, mirrors XAUDIO_TIMEOUT

        XAudio2Output();
        ~XAudio2Output();

        bool Initialize();
        // Blocking. Copies into the current sub-buffer; when it fills, waits
        // until fewer than MAX_BUFFERS-1 buffers are submitted, then submits.
        // Returns frames actually written (0..frames).
        uint32_t Write(const int16_t* data, uint32_t frames);
        // Bytes available in the ring (xaudio2_write_available equivalent).
        size_t write_avail();
        // Drop all queued audio (game switch). Voice state preserved; the
        // next Write() auto-starts it if stopped.
        void Flush();
        void Start();
        void Stop();
        bool IsReady() const { return m_initialized; }
        bool IsStarted() const { return m_started; }
        uint32_t GetQueuedFrames();
        long GetBuffers() { return InterlockedCompareExchange(&m_buffers, 0, 0); }
        long long GetTotalConsumed() { return InterlockedCompareExchange64(&m_totalConsumed, 0, 0); }
        long GetUnderruns() { return InterlockedCompareExchange(&m_underruns, 0, 0); }
        long GetOverruns() { return InterlockedCompareExchange(&m_overruns, 0, 0); }

        // Called by the XAudio2 engine thread (OnBufferEnd).
        void OnBufferEnded();

    private:
        uint32_t GetSampleRate() const { return SAMPLE_RATE; }
        // Submits the full current sub-buffer, blocking for a free slot
        // (waits with the mutex released). False on timeout → caller drops.
        bool SubmitFullBuffer();

        IXAudio2* m_pXAudio2;
        IXAudio2MasteringVoice* m_pMasterVoice;
        IXAudio2SourceVoice* m_pSourceVoice;
        bool m_initialized;
        bool m_started;

        // Serializes producer (emulation thread Write) vs UI thread
        // (Flush/Stop/Start/GetQueuedFrames). The engine-thread callback
        // (OnBufferEnded) only uses interlocked ops + SetEvent — no lock.
        std::mutex m_mutex;

        // Producer-only (emulator thread).
        uint8_t* m_buf;            // SUBBUF_BYTES * MAX_BUFFERS
        unsigned m_bufptr;         // bytes filled into current sub-buffer
        unsigned m_writeBuffer;    // index of sub-buffer being filled

        // Touched by the XAudio2 engine thread (OnBufferEnd): decrements
        // m_buffers and signals m_hEvent. Producer reads/writes both.
        HANDLE m_hEvent;
        volatile long m_buffers;   // sub-buffers submitted, not yet ended
        long long m_totalConsumed;

        // Health counters for the 1Hz [HEALTH] diagnostic (monotonic since
        // Initialize/Flush). m_underruns: device starved (ring empty while
        // playback had already consumed data). m_overruns: Write timed out in
        // SubmitFullBuffer and dropped audio. Both Interlocked, read from the
        // UI thread.
        volatile long m_underruns;
        volatile long m_overruns;
    };
}
