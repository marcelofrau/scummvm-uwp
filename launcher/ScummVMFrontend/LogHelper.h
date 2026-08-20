#pragma once

#define FRONTEND_VERSION "2026.8.20"

// Minimal spdlog-compatible shim: everything logs to OutputDebugString.
// No fmt/spdlog dependency. Supports `{}` and `{:0NdX}` / `{:0NdX}` hex specs.
#include <windows.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <type_traits>
#include <vector>
#include "miniz/miniz.h"

#pragma push_macro("OutputDebugStringA")
#undef OutputDebugStringA

namespace spdlog
{
    inline std::wstring g_logPath; // shared across TUs (C++17 inline variable)
    namespace level
    {
        enum level_enum : int
        {
            trace = 0,
            debug = 1,
            info = 2,
            warn = 3,
            err = 4,
            error = err,
            critical = 5,
            off = 6,
        };
    }

    namespace detail
    {
        inline std::string wallclock_ms()
        {
            // Returns "[HH:MM:SS.mmm]" — wall clock with millisecond precision.
            // Used as prefix on every log line for cross-reference with device
            // clocks and log collection timestamps.
            SYSTEMTIME st;
            GetLocalTime(&st);
            char buf[32];
            sprintf_s(buf, "[%02d:%02d:%02d.%03d]",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            return std::string(buf);
        }

        inline std::string utf8_from_wide(const wchar_t* src)
        {
            if (!src)
                return "(null)";
            int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 0)
                return "(null)";
            std::string out(static_cast<size_t>(n) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, src, -1, &out[0], n, nullptr, nullptr);
            return out;
        }

        inline void append(std::string& out, const char* v) { out += (v ? v : "(null)"); }
        inline void append(std::string& out, const std::string& v) { out += v; }
        inline void append(std::string& out, const wchar_t* v) { out += utf8_from_wide(v ? v : L"(null)"); }
        inline void append(std::string& out, const std::wstring& v) { out += utf8_from_wide(v.c_str()); }
        inline void append(std::string& out, char v) { out += v; }
        inline void append(std::string& out, bool v) { out += v ? "true" : "false"; }
        inline void append(std::string& out, double v) { out += std::to_string(v); }
        inline void append(std::string& out, float v) { out += std::to_string(v); }
        inline void append(std::string& out, long long v) { out += std::to_string(v); }
        inline void append(std::string& out, unsigned long long v) { out += std::to_string(v); }
        template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
        inline void append(std::string& out, T v)
        {
            out += std::to_string(static_cast<long long>(v));
        }

        template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
        inline void append_hex(std::string& out, T v, int width)
        {
            char buf[40];
            if (width > 0)
                sprintf_s(buf, "%.*llX", width, static_cast<unsigned long long>(v));
            else
                sprintf_s(buf, "%llX", static_cast<unsigned long long>(v));
            out += buf;
        }

        template <typename T, typename std::enable_if<!std::is_integral<T>::value, int>::type = 0>
        inline void append_hex(std::string& out, T v, int)
        {
            append(out, v);
        }

        inline void parse(std::string& out, const char* fmt)
        {
            out += (fmt ? fmt : "");
        }

        template <typename T, typename... Args>
        void parse(std::string& out, const char* fmt, T&& first, Args&&... rest)
        {
            if (!fmt)
                return;
            const char* p = strstr(fmt, "{");
            if (!p)
            {
                out += fmt;
                return;
            }
            const char* close = strchr(p, '}');
            if (!close)
            {
                out += fmt;
                return;
            }
            out.append(fmt, p - fmt);

            // `{:<hex spec>}` -> hex (only uppercase X handled; lowercase is rare)
            bool hexSpec = false;
            int width = 0;
            if (close - p > 2 && p[1] == ':')
            {
                for (const char* s = p + 2; s < close; ++s)
                {
                    if (*s == 'X')
                        hexSpec = true;
                    else if (*s >= '0' && *s <= '9')
                        width = width * 10 + (*s - '0');
                }
            }

            if (hexSpec)
            {
                append_hex(out, std::forward<T>(first), width);
                parse(out, close + 1, rest...);
                return;
            }

            append(out, std::forward<T>(first));
            parse(out, close + 1, rest...);
        }
    }

    inline void ods(const char* msg)
    {
        wchar_t wide[2048];
        int n = MultiByteToWideChar(CP_UTF8, 0, msg, -1, wide, 2047);
        if (n <= 0)
            return;
        wide[2047] = 0;
        ::OutputDebugStringW(wide);

        // Mirror to a file so the same trace is readable on console devices
        // (Xbox) where a debugger/ODS is not available. Path set via
        // LogInit(ApplicationData LocalFolder); env-var fallback only.
        static std::wstring s_fallback = [] {
            wchar_t env[512] = { 0 };
            DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", env, 511);
            return len ? std::wstring(env) + L"\\scummvm-debug.log" : std::wstring(L"scummvm-debug.log");
        }();
        const std::wstring& path = g_logPath.empty() ? s_fallback : g_logPath;

        // std::ofstream append — same mechanism as the staging writes that
        // are known to work in the UWP process (vs _wfopen_s).
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (out)
            out.write(msg, (std::streamsize)strlen(msg));
    }

    template <typename... Args>
    inline void log(level::level_enum lvl, const char* fmt, Args&&... args)
    {
        std::string s;
        detail::parse(s, fmt, std::forward<Args>(args)...);
        const char* prefix = "";
        switch (lvl)
        {
        case level::critical: prefix = "[CRIT] "; break;
        case level::error: prefix = "[ERR] "; break;
        case level::warn: prefix = "[WARN] "; break;
        default: break;
        }
        ods((detail::wallclock_ms() + std::string(prefix) + s + "\n").c_str());
    }

    template <typename... Args> inline void trace(const char* fmt, Args&&... args) { log(level::trace, fmt, args...); }
    template <typename... Args> inline void debug(const char* fmt, Args&&... args) { log(level::debug, fmt, args...); }
    template <typename... Args> inline void info(const char* fmt, Args&&... args) { log(level::info, fmt, args...); }
    template <typename... Args> inline void warn(const char* fmt, Args&&... args) { log(level::warn, fmt, args...); }
    template <typename... Args> inline void error(const char* fmt, Args&&... args) { log(level::error, fmt, args...); }
    template <typename... Args> inline void critical(const char* fmt, Args&&... args) { log(level::critical, fmt, args...); }

    inline void shutdown() {}
}

#pragma pop_macro("OutputDebugStringA")

inline void LogPrint(const char* msg)
{
    std::string s(msg ? msg : "");
    if (!s.empty() && s.back() == '\n')
        s.pop_back();
    spdlog::info("{}", s);
}

#define OutputDebugStringA(msg) LogPrint(msg)

inline bool GzipCompressFile(const std::wstring& srcPath, const std::wstring& dstPath)
{
    // Read source file
    std::ifstream in(srcPath, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize sz = in.tellg();
    if (sz <= 0) { in.close(); return false; }
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> src(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char*>(src.data()), sz)) { in.close(); return false; }
    in.close();

    // Compress with miniz into a gzip-compatible buffer
    unsigned long compSize = (unsigned long)(sz * 110 / 100 + 64);
    std::vector<unsigned char> comp(compSize);

    // miniz mz_compress gives raw deflate — write as .gz manually
    mz_stream stream = {};
    stream.next_in = src.data();
    stream.avail_in = (mz_uint32)src.size();
    stream.next_out = comp.data();
    stream.avail_out = compSize;
    stream.zalloc = NULL;
    stream.zfree = NULL;
    stream.opaque = NULL;

    int level = MZ_DEFAULT_COMPRESSION;
    int status = mz_deflateInit2(&stream, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
    if (status != MZ_OK) return false;
    status = mz_deflate(&stream, MZ_FINISH);
    mz_deflateEnd(&stream);
    if (status != MZ_STREAM_END) return false;

    compSize = stream.total_out;

    // Write gzip file: magic + method + flags + mtime + xfl + OS + compressed data + CRC32 + size
    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, src.data(), (unsigned long)src.size());
    mz_ulong isize = (mz_ulong)src.size() & 0xFFFFFFFF;
    mz_ulong csize = (mz_ulong)compSize;

    std::ofstream out(dstPath, std::ios::binary);
    if (!out) return false;
    unsigned char hdr[10] = {
        0x1f, 0x8b,  // gzip magic
        0x08,        // deflate
        0x00,        // flags
        0,0,0,0,     // mtime (0)
        0x00,        // xfl
        0xff          // OS (unknown)
    };
    out.write(reinterpret_cast<const char*>(hdr), 10);
    out.write(reinterpret_cast<const char*>(comp.data()), compSize);
    unsigned char footer[8];
    footer[0] = (crc >>  0) & 0xff; footer[1] = (crc >>  8) & 0xff;
    footer[2] = (crc >> 16) & 0xff; footer[3] = (crc >> 24) & 0xff;
    footer[4] = (isize >>  0) & 0xff; footer[5] = (isize >>  8) & 0xff;
    footer[6] = (isize >> 16) & 0xff; footer[7] = (isize >> 24) & 0xff;
    out.write(reinterpret_cast<const char*>(footer), 8);
    out.close();
    return true;
}

inline void LogRotate(const std::wstring& dir)
{
    // Rotate scummvm-debug.log → .1.log.gz → .2.log.gz → .3.log.gz → .4.log.gz
    // Delete anything beyond .4.log.gz (keeps last 5 sessions total).
    const int kMaxOld = 4;

    // Delete oldest
    for (int i = kMaxOld + 1; i <= kMaxOld + 10; ++i)
    {
        std::wstring p = dir + L"\\scummvm-debug." + std::to_wstring(i) + L".log.gz";
        DeleteFileW(p.c_str());
        // Also delete uncompressed leftover
        std::wstring plog = dir + L"\\scummvm-debug." + std::to_wstring(i) + L".log";
        DeleteFileW(plog.c_str());
    }

    // Compress .N-1.log → .N.log.gz (for N = kMaxOld down to 2)
    // Then delete the .N-1.log source.
    for (int i = kMaxOld; i >= 2; --i)
    {
        std::wstring srcLog = dir + L"\\scummvm-debug." + std::to_wstring(i - 1) + L".log";
        std::wstring dstGz = dir + L"\\scummvm-debug." + std::to_wstring(i) + L".log.gz";
        DeleteFileW(dstGz.c_str());
        if (GzipCompressFile(srcLog, dstGz))
        {
            DeleteFileW(srcLog.c_str());
        }
        else
        {
            // Compression failed — just shift as uncompressed
            std::wstring dstLog = dir + L"\\scummvm-debug." + std::to_wstring(i) + L".log";
            MoveFileExW(srcLog.c_str(), dstLog.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }

    // .1.log ← current log (uncompressed, active)
    {
        std::wstring current = dir + L"\\scummvm-debug.log";
        std::wstring first = dir + L"\\scummvm-debug.1.log";
        MoveFileExW(current.c_str(), first.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

inline void LogInit(const wchar_t* logPath)
{
    if (logPath && *logPath)
        spdlog::g_logPath = logPath;
    else
        spdlog::g_logPath.clear();

    // Rotate previous logs — keep last 5 sessions
    if (!spdlog::g_logPath.empty())
    {
        // Extract directory from logPath
        size_t pos = spdlog::g_logPath.find_last_of(L'\\');
        if (pos != std::wstring::npos)
        {
            std::wstring dir = spdlog::g_logPath.substr(0, pos);
            LogRotate(dir);
        }
    }

    std::ofstream probe(spdlog::g_logPath, std::ios::binary | std::ios::app);
    if (probe)
    {
        probe.write("\r\n=== LogInit sink active ===\r\n", 31);
        ::OutputDebugStringW(L"LogInit: sink active\n");
    }
    else
    {
        ::OutputDebugStringW(L"LogInit: FAILED to open sink\n");
    }
}

inline void LogShutdown()
{
    spdlog::shutdown();
}

// BootTrace: ultra-defensive boot-stage marker. Independent of the spdlog
// formatting machinery; appends one line with tid + ms-since-first-call to the
// active log file (g_logPath) and flushes. Never throws. Used to find where a
// silent launch-kill happens (watchdog/activation) when spdlog may never run.
inline void BootTrace(const wchar_t* stage)
{
    if (spdlog::g_logPath.empty())
        return;
    try
    {
        static const LARGE_INTEGER s_t0 = [] {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            return t;
        }();
        static const LARGE_INTEGER s_freq = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f;
        }();

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double ms = (double)(now.QuadPart - s_t0.QuadPart) * 1000.0 / s_freq.QuadPart;

        std::ofstream out(spdlog::g_logPath, std::ios::binary | std::ios::app);
        if (!out)
            return;
        char line[1400];
        // Wall clock + QPC boot time + thread id for every BootTrace line
        SYSTEMTIME st;
        GetLocalTime(&st);
        int n = sprintf_s(line, "[%02d:%02d:%02d.%03d] [boot %07.1fms tid=0x%04X] %ls\r\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            ms, (unsigned)GetCurrentThreadId(), stage ? stage : L"?");
        out.write(line, n);
        out.flush();
    }
    catch (...) {}
}

// UTF-8 variant for narrow strings (core-side messages).
inline void BootTrace(const char* stage)
{
    std::wstring ws;
    int n = MultiByteToWideChar(CP_UTF8, 0, stage ? stage : "?", -1, nullptr, 0);
    if (n > 0)
    {
        ws.resize(static_cast<size_t>(n) - 1);
        MultiByteToWideChar(CP_UTF8, 0, stage, -1, &ws[0], n);
    }
    BootTrace(ws.c_str());
}
