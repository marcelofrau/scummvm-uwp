#include "pch.h"
#include "XAudio2Output.h"
#include <xaudio2.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <windows.h>

using namespace dosbox_uwp;

static volatile long s_queuedFrames = 0;
static volatile long long s_totalProduced = 0;
static volatile long long s_totalConsumed = 0;
static volatile long s_flushGen = 1;
static volatile long s_underrunCount = 0;
static const long TARGET_QUEUE = 6615; // ~150ms@44100Hz — match TARGET_FRAMES
static const long MAX_QUEUE = 22050;   // ~500ms — generous cap to prevent flush-during-normal-operation

static LARGE_INTEGER s_qpcFreq = {};
static LARGE_INTEGER s_lastSubmit = {};
static LARGE_INTEGER s_lastSampleClock = {};
static ULONGLONG s_lastSamplesPlayed = 0;
static bool s_haveBaseline = false;

static void init_qpc()
{
    if (s_qpcFreq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&s_qpcFreq);
        QueryPerformanceCounter(&s_lastSubmit);
    }
}
static double elapsed_ms()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ms = (double)(now.QuadPart - s_lastSubmit.QuadPart) * 1000.0 / s_qpcFreq.QuadPart;
    s_lastSubmit = now;
    return ms;
}

static void log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

static HANDLE s_drainEvent = nullptr; // manual-reset, signaled when queue < LOW_WATERMARK

class XA2VoiceCallback : public IXAudio2VoiceCallback
{
public:
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 BytesRequired) override
    {
        if (BytesRequired > 0)
        {
            InterlockedIncrement(&s_underrunCount);
            log("XA2 UNDERRUN: BytesRequired=%lu queue=%ld (underrun#%ld)",
                BytesRequired, (long)s_queuedFrames, (long)s_underrunCount);
        }
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) override
    {
        if (!pBufferContext) return;
        auto* sb = static_cast<XA2SubmittedBuffer*>(pBufferContext);
        if (sb->data) delete[] sb->data;
        if (sb->flushGen == s_flushGen)
        {
            InterlockedExchangeAdd(&s_queuedFrames, -(long)sb->frames);
            InterlockedExchangeAdd64(&s_totalConsumed, (long long)sb->frames);
            // Signal drain event when queue drops below low watermark
            if (s_drainEvent && s_queuedFrames < XAudio2Output::LOW_WATERMARK)
                SetEvent(s_drainEvent);
        }
        delete sb;
    }
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT Error) override
    {
        log("XAudio2 voice error: 0x%08lX", (unsigned long)Error);
    }
};

static XA2VoiceCallback s_callback;

XAudio2Output::XAudio2Output()
    : m_pXAudio2(nullptr)
    , m_pMasterVoice(nullptr)
    , m_pSourceVoice(nullptr)
    , m_initialized(false)
    , m_started(false)
    , m_drainEvent(nullptr)
{
}

XAudio2Output::~XAudio2Output()
{
    if (m_pSourceVoice)
    {
        m_pSourceVoice->Stop();
        m_pSourceVoice->FlushSourceBuffers();
    }
    Sleep(80);

    if (m_pSourceVoice) { m_pSourceVoice->DestroyVoice(); m_pSourceVoice = nullptr; }
    if (m_pMasterVoice) { m_pMasterVoice->DestroyVoice(); m_pMasterVoice = nullptr; }
    if (m_pXAudio2)     { m_pXAudio2->Release(); m_pXAudio2 = nullptr; }
    if (m_drainEvent)   { CloseHandle(m_drainEvent); m_drainEvent = nullptr; s_drainEvent = nullptr; }
}

void XAudio2Output::EnsureDrainEvent()
{
    if (!m_drainEvent)
    {
        m_drainEvent = CreateEventW(nullptr, TRUE /*manual-reset*/, TRUE /*initially signaled*/, nullptr);
        s_drainEvent = m_drainEvent;
    }
}

bool XAudio2Output::Initialize()
{
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = 176400;

    EnsureDrainEvent();

    HRESULT hr = XAudio2Create(&m_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr))
    {
        log("XAudio2Create FAILED: 0x%08lX", (unsigned long)hr);
        return false;
    }

    hr = m_pXAudio2->CreateMasteringVoice(
        &m_pMasterVoice, 2, 44100, 0, nullptr, nullptr, AudioCategory_GameEffects);
    if (FAILED(hr))
    {
        log("CreateMasteringVoice FAILED: 0x%08lX", (unsigned long)hr);
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
        return false;
    }

    hr = m_pXAudio2->CreateSourceVoice(
        &m_pSourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
        &s_callback, nullptr, nullptr);
    if (FAILED(hr))
    {
        log("CreateSourceVoice FAILED: 0x%08lX", (unsigned long)hr);
        m_pMasterVoice->DestroyVoice();
        m_pMasterVoice = nullptr;
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
        return false;
    }

    m_initialized = true;
    m_started = false;
    s_queuedFrames = 0;
    s_underrunCount = 0;
    s_haveBaseline = false;

    log("XAudio2Output: initialized OK, voice stopped");
    return true;
}

#define TREND_SAMPLES 30
static long s_trendBuf[TREND_SAMPLES] = {};
static int s_trendIdx = 0;
static int s_trendCount = 0;

static void trend_push(long val)
{
    s_trendBuf[s_trendIdx] = val;
    s_trendIdx = (s_trendIdx + 1) % TREND_SAMPLES;
    if (s_trendCount < TREND_SAMPLES)
        s_trendCount++;
}
static double trend_avg()
{
    if (s_trendCount == 0) return 0;
    long sum = 0;
    int n = s_trendCount;
    for (int i = 0; i < n; i++)
        sum += s_trendBuf[i];
    return (double)sum / n;
}

void XAudio2Output::Submit(const int16_t* data, uint32_t frames)
{
    if (!m_initialized || !data || frames == 0 || !m_pSourceVoice)
        return;

    init_qpc();
    double sinceLast = elapsed_ms();

    bool underrun = (s_underrunCount > 0);
    if (m_started && !underrun && s_queuedFrames < 200)
    {
        log("XA2 LOW: queue=%ld (<200), frames=%lu  gap=%.1fms",
            (long)s_queuedFrames, frames, sinceLast);
    }

    auto* sb = new XA2SubmittedBuffer();
    sb->frames = frames;
    sb->flushGen = s_flushGen;
    sb->data = new int16_t[(size_t)frames * 2];
    memcpy(sb->data, data, (size_t)frames * 2 * sizeof(int16_t));

    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = (UINT32)(frames * 2 * sizeof(int16_t));
    buf.pAudioData = (const BYTE*)sb->data;
    buf.pContext = sb;

    m_pSourceVoice->SubmitSourceBuffer(&buf);
    InterlockedExchangeAdd(&s_queuedFrames, (long)frames);
    InterlockedExchangeAdd64(&s_totalProduced, (LONGLONG)frames);

    if (!m_started && s_queuedFrames >= TARGET_QUEUE)
    {
        m_pSourceVoice->Start(0);
        m_started = true;
        log("XA2 START after pre-buffer: %ld frames (%.0fms)",
            (long)s_queuedFrames, (double)s_queuedFrames * 1000.0 / 44100.0);
    }

    // FLUSH cap: bound max latency at ~500ms
    if (m_started && s_queuedFrames > MAX_QUEUE)
    {
        m_pSourceVoice->Stop();
        m_pSourceVoice->FlushSourceBuffers();
        s_queuedFrames = 0;
        InterlockedIncrement(&s_flushGen);
        m_started = false;
        log("XA2 FLUSH cap at %ld frames (%.0fms) — resetting",
            (long)MAX_QUEUE, (double)MAX_QUEUE * 1000.0 / 44100.0);
    }

    static uint32_t submitCounter = 0;
    if ((++submitCounter % 100) == 0)
    {
        XAUDIO2_VOICE_STATE vs;
        m_pSourceVoice->GetState(&vs);

        double consumptionRate = 0.0;
        double driftMs = 0.0;
        if (s_haveBaseline)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double elapsedSec = (double)(now.QuadPart - s_lastSampleClock.QuadPart) / s_qpcFreq.QuadPart;
            if (elapsedSec > 0)
            {
                ULONGLONG deltaSamples = vs.SamplesPlayed - s_lastSamplesPlayed;
                consumptionRate = (double)deltaSamples / elapsedSec;
                double expectedSamples = 44100.0 * elapsedSec;
                double driftSamples = (double)deltaSamples - expectedSamples;
                driftMs = driftSamples * 1000.0 / 44100.0;
            }
        }
        s_lastSampleClock = s_lastSubmit;
        s_lastSamplesPlayed = vs.SamplesPlayed;
        s_haveBaseline = true;

        long q2 = s_queuedFrames;
        trend_push(q2);
        double avgQ = trend_avg();
        double trendDelta = (double)q2 - avgQ;

        long urCount = s_underrunCount;
        s_underrunCount = 0;

        log("XA2 submit #%lu: frames=%lu queue=%ld (%.0fms) "
            "XA2_BuffersQueued=%lu gap=%.1fms  trend_avg=%.0f trend_delta=%.0f "
            "consumption=%.0fHz drift=%.1fms underruns=%ld "
            "totP=%lld totC=%lld",
            (unsigned long)submitCounter, frames, q2, (double)q2 * 1000.0 / 44100.0,
            (unsigned long)vs.BuffersQueued, sinceLast,
            avgQ, trendDelta,
            consumptionRate, driftMs, urCount,
            (long long)s_totalProduced, (long long)s_totalConsumed);
    }
}

void XAudio2Output::Start()
{
    if (!m_initialized || !m_pSourceVoice || m_started)
        return;
    HRESULT hr = m_pSourceVoice->Start(0);
    if (SUCCEEDED(hr))
        m_started = true;
}

void XAudio2Output::Stop()
{
    if (!m_initialized || !m_pSourceVoice || !m_started)
        return;
    m_pSourceVoice->Stop();
    m_started = false;
    if (m_drainEvent)
        SetEvent(m_drainEvent);
}

void XAudio2Output::WaitForDrain()
{
    if (!m_started || !m_drainEvent)
        return;

    long q = s_queuedFrames;
    if (q <= HIGH_WATERMARK)
        return;

    ResetEvent(m_drainEvent);
    if (s_queuedFrames <= HIGH_WATERMARK)
        return;

    log("XA2 DRAIN: queue=%ld > HIGH=%ld, waiting...", (long)s_queuedFrames, (long)HIGH_WATERMARK);
    while (s_queuedFrames > LOW_WATERMARK)
    {
        DWORD waitResult = WaitForSingleObject(m_drainEvent, 50);
        if (waitResult == WAIT_TIMEOUT && s_queuedFrames <= LOW_WATERMARK)
            break;
    }
}

void XAudio2Output::Flush()
{
    if (!m_initialized || !m_pSourceVoice)
        return;

    m_pSourceVoice->Stop();
    m_pSourceVoice->FlushSourceBuffers();
    s_queuedFrames = 0;
    InterlockedIncrement(&s_flushGen);
    m_started = false;

    // Signal drain event so any blocked Submit() wakes up
    if (m_drainEvent)
        SetEvent(m_drainEvent);

    log("XA2 Flush: flushed, voice stopped (Submit will auto-start)");
}

uint32_t XAudio2Output::GetQueuedFrames() const
{
    return (uint32_t)s_queuedFrames;
}

volatile long* XAudio2Output::QueuedFramesPtr()
{
    return &s_queuedFrames;
}
volatile long long* XAudio2Output::TotalProducedPtr()
{
    return &s_totalProduced;
}
volatile long long* XAudio2Output::TotalConsumedPtr()
{
    return &s_totalConsumed;
}
