#include "pch.h"
#include "XAudio2Output.h"
#include <xaudio2.h>
#include <cstring>
#include <new>
#include "LogHelper.h"

using namespace scummvm_uwp;

namespace
{
    class XA2VoiceCallback : public IXAudio2VoiceCallback
    {
    public:
        XA2VoiceCallback() : m_owner(nullptr) {}
        void SetOwner(XAudio2Output* owner) { m_owner = owner; }

        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 BytesRequired) override
        {
            (void)BytesRequired;
        }
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void* pBufferContext) override
        {
            (void)pBufferContext;
        }
        void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) override
        {
            (void)pBufferContext;
            if (m_owner)
                m_owner->OnBufferEnded();
        }
        void STDMETHODCALLTYPE OnLoopEnd(void* pBufferContext) override
        {
            (void)pBufferContext;
        }
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnVoiceError(void* pBufferContext, HRESULT Error) override
        {
            (void)pBufferContext;
            spdlog::error("[XA2] voice error: 0x{:08X}", (unsigned)Error);
        }

    private:
        XAudio2Output* m_owner;
    };

    XA2VoiceCallback s_callback;
}

XAudio2Output::XAudio2Output()
    : m_pXAudio2(nullptr)
    , m_pMasterVoice(nullptr)
    , m_pSourceVoice(nullptr)
    , m_initialized(false)
    , m_started(false)
    , m_buf(nullptr)
    , m_bufptr(0)
    , m_writeBuffer(0)
    , m_hEvent(nullptr)
    , m_buffers(0)
    , m_totalConsumed(0)
    , m_underruns(0)
    , m_overruns(0)
{
}

XAudio2Output::~XAudio2Output()
{
    if (m_pSourceVoice)
    {
        m_pSourceVoice->Stop();
        m_pSourceVoice->FlushSourceBuffers();
        Sleep(50);
        m_pSourceVoice->DestroyVoice();
        m_pSourceVoice = nullptr;
    }
    if (m_pMasterVoice)
    {
        m_pMasterVoice->DestroyVoice();
        m_pMasterVoice = nullptr;
    }
    if (m_pXAudio2)
    {
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
    }
    if (m_hEvent)
    {
        CloseHandle(m_hEvent);
        m_hEvent = nullptr;
    }
    if (m_buf)
    {
        delete[] m_buf;
        m_buf = nullptr;
    }
}

bool XAudio2Output::Initialize()
{
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = SAMPLE_RATE * 4;

    m_hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_hEvent)
    {
        spdlog::error("[XA2] CreateEvent FAILED");
        return false;
    }

    HRESULT hr = XAudio2Create(&m_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr))
    {
        spdlog::error("[XA2] XAudio2Create FAILED: 0x{:08X}", (unsigned)hr);
        return false;
    }

    hr = m_pXAudio2->CreateMasteringVoice(
        &m_pMasterVoice, 2, SAMPLE_RATE, 0, nullptr, nullptr, AudioCategory_GameEffects);
    if (FAILED(hr))
    {
        spdlog::error("[XA2] CreateMasteringVoice FAILED: 0x{:08X}", (unsigned)hr);
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
        return false;
    }

    s_callback.SetOwner(this);
    hr = m_pXAudio2->CreateSourceVoice(
        &m_pSourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
        &s_callback, nullptr, nullptr);
    if (FAILED(hr))
    {
        spdlog::error("[XA2] CreateSourceVoice FAILED: 0x{:08X}", (unsigned)hr);
        m_pMasterVoice->DestroyVoice();
        m_pMasterVoice = nullptr;
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
        return false;
    }

    m_buf = new (std::nothrow) uint8_t[(size_t)SUBBUF_BYTES * MAX_BUFFERS];
    if (!m_buf)
    {
        spdlog::error("[XA2] ring buffer allocation FAILED");
        return false;
    }
    memset(m_buf, 0, (size_t)SUBBUF_BYTES * MAX_BUFFERS);

    // Voice starts immediately with no data, exactly like RetroArch's
    // xaudio2_new(): the first submitted sub-buffer plays right away,
    // giving `LATENCY_MS` of buffered audio.
    hr = m_pSourceVoice->Start(0);
    if (FAILED(hr))
    {
        spdlog::error("[XA2] Start FAILED: 0x{:08X}", (unsigned)hr);
        return false;
    }

    m_initialized = true;
    m_started = true;
    m_bufptr = 0;
    m_writeBuffer = 0;
    m_buffers = 0;
    m_totalConsumed = 0;
    m_underruns = 0;
    m_overruns = 0;

    spdlog::info("[XA2] initialized: {} sub-buffers x {} frames ({}ms), ring {} bytes, voice started",
        MAX_BUFFERS, (unsigned)SUBBUF_FRAMES, (unsigned)LATENCY_MS,
        (unsigned)SUBBUF_BYTES * MAX_BUFFERS);
    return true;
}

size_t XAudio2Output::write_avail()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    long buffers = InterlockedCompareExchange(&m_buffers, 0, 0);
    if (buffers < MAX_BUFFERS - 1)
        return (size_t)SUBBUF_BYTES * (MAX_BUFFERS - (int)buffers - 1) - m_bufptr;
    return 0;
}

uint32_t XAudio2Output::Write(const int16_t* data, uint32_t frames)
{
    if (!m_initialized || !data || frames == 0 || !m_pSourceVoice)
        return 0;

    const uint8_t* buffer = (const uint8_t*)data;
    uint32_t bytes = frames * 4;
    uint32_t written = 0;

    while (bytes)
    {
        size_t need;
        {
            // Serialize with UI-thread Flush/Stop/Start. The blocking slot
            // wait happens outside the lock (SubmitFullBuffer) so the UI
            // thread can still flush/stop during a slow engine.
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!m_started)
            {
                m_pSourceVoice->Start(0);
                m_started = true;
            }

            need = (bytes < (uint32_t)(SUBBUF_BYTES - m_bufptr))
                ? bytes
                : (size_t)(SUBBUF_BYTES - m_bufptr);
            if (need > 0)
            {
                memcpy(m_buf + (size_t)m_writeBuffer * SUBBUF_BYTES + m_bufptr,
                    buffer, need);
                m_bufptr += (unsigned)need;
                buffer += need;
                written += (unsigned)need;
                bytes -= (unsigned)need;
            }
        }

        // Submit whenever the staging sub-buffer fills, even if the input is
        // now exhausted (RetroArch xa_write parity). The previous code only
        // submitted when bytes remained, deferring a full sub-buffer to the
        // next retro_run and adding up to a frame of latency.
        if (m_bufptr == SUBBUF_BYTES)
        {
            // Underrun: ring empty AND playback already consumed data (guards
            // the initial prefill, where nothing has played yet).
            if (InterlockedCompareExchange(&m_buffers, 0, 0) == 0 &&
                InterlockedCompareExchange64(&m_totalConsumed, 0, 0) > 0)
            {
                InterlockedIncrement(&m_underruns);
            }
            if (!SubmitFullBuffer())
                break;
        }
    }

    return written / 4;
}

bool XAudio2Output::SubmitFullBuffer()
{
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (InterlockedCompareExchange(&m_buffers, 0, 0) < MAX_BUFFERS - 1)
            {
                XAUDIO2_BUFFER xa2buf = {};
                xa2buf.AudioBytes = SUBBUF_BYTES;
                xa2buf.pAudioData = m_buf + (size_t)m_writeBuffer * SUBBUF_BYTES;
                HRESULT hr = m_pSourceVoice->SubmitSourceBuffer(&xa2buf);
                if (FAILED(hr))
                {
                    spdlog::warn("[XA2] SubmitSourceBuffer FAILED: 0x{:08X}", (unsigned)hr);
                    return false;
                }

                InterlockedIncrement(&m_buffers);
                m_bufptr = 0;
                m_writeBuffer = (m_writeBuffer + 1) & MAX_BUFFERS_MASK;
                return true;
            }
        }

        // Wait outside the lock so the UI thread can Flush/Stop meanwhile.
        // Safety valve only: the ring is a follower (see XAudio2Output.h).
        // RetroArch xaudio.c does the same via WaitForSingleObject in
        // xa_write when the queue is full. Bounded by WRITE_TIMEOUT.
        if (WaitForSingleObject(m_hEvent, WRITE_TIMEOUT) != WAIT_OBJECT_0)
        {
            spdlog::warn("[XA2] Write timeout: engine not consuming ({} queued), dropping remainder",
                (long)InterlockedCompareExchange(&m_buffers, 0, 0));
            InterlockedIncrement(&m_overruns);
            return false;
        }
    }
}

void XAudio2Output::Flush()
{
    if (!m_initialized || !m_pSourceVoice)
        return;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pSourceVoice->FlushSourceBuffers();
    }

    // Wait for OnBufferEnd callbacks so m_buffers reaches 0 before the ring
    // is reused (otherwise the stale callbacks would corrupt the counter).
    int waited = 0;
    while (InterlockedCompareExchange(&m_buffers, 0, 0) > 0 && waited < 100)
    {
        WaitForSingleObject(m_hEvent, 10);
        waited++;
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_bufptr = 0;
        m_writeBuffer = 0;
        m_totalConsumed = 0;
        m_underruns = 0;
        m_overruns = 0;
    }
    spdlog::info("[XA2] Flush: ring reset ({} waits)", waited);
}

void XAudio2Output::Start()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_initialized || !m_pSourceVoice || m_started)
        return;
    if (SUCCEEDED(m_pSourceVoice->Start(0)))
        m_started = true;
}

void XAudio2Output::Stop()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_initialized || !m_pSourceVoice || !m_started)
        return;
    m_pSourceVoice->Stop();
    m_started = false;
}

void XAudio2Output::OnBufferEnded()
{
    InterlockedDecrement(&m_buffers);
    InterlockedIncrement64(&m_totalConsumed);
    SetEvent(m_hEvent);
}

uint32_t XAudio2Output::GetQueuedFrames()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    long buffers = InterlockedCompareExchange(&m_buffers, 0, 0);
    return (uint32_t)(buffers * SUBBUF_FRAMES + m_bufptr / 4);
}
