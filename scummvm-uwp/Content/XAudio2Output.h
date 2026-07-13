#pragma once

#include <cstdint>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace dosbox_uwp
{
    struct XA2SubmittedBuffer
    {
        int16_t* data;
        uint32_t frames;
        long flushGen;
    };

    class XAudio2Output
    {
    public:
        XAudio2Output();
        ~XAudio2Output();

        bool Initialize();
        void Submit(const int16_t* data, uint32_t frames);
        void Flush();
        void Start();
        void Stop();
        bool IsReady() const { return m_initialized; }
        bool IsStarted() const { return m_started; }
        uint32_t GetQueuedFrames() const;
        void WaitForDrain(); // Block until queue drops below HIGH_WATERMARK (call from main loop, not from Submit)
        static const long TARGET_FRAMES = 6615; // ~150ms — pre-buffer threshold
        static const long HIGH_WATERMARK = 4410; // ~100ms — start waiting when queue exceeds this
        static const long LOW_WATERMARK = 3307;  // ~75ms  — resume when queue drops below this
        static volatile long* QueuedFramesPtr();
        static volatile long long* TotalProducedPtr();
        static volatile long long* TotalConsumedPtr();

    private:
        uint32_t GetSampleRate() const { return 44100; }
        void EnsureDrainEvent();

        IXAudio2* m_pXAudio2;
        IXAudio2MasteringVoice* m_pMasterVoice;
        IXAudio2SourceVoice* m_pSourceVoice;
        bool m_initialized;
        bool m_started;
        HANDLE m_drainEvent;   // signaled by OnBufferEnd when queue drops below LOW_WATERMARK
    };
}
