#include "pch.h"
#include "Bootstrap.h"
#include "DataPaths.h"
#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <Windows.Storage.h>
#include <Windows.ApplicationModel.h>
#include <windows.h>

using namespace Windows::ApplicationModel;
using namespace Windows::Storage;

namespace fs = std::filesystem;

namespace scummvm_uwp
{
    std::wstring Bootstrap::LocalStateDir();
    std::wstring Bootstrap::InstalledLocationDir();
    bool Bootstrap::FileExistsInLocalState(const wchar_t* fileName);

    namespace
    {
        std::wstring GetAppVersionString()
        {
            PackageVersion v = Package::Current->Id->Version;
            wchar_t buf[48];
            swprintf_s(buf, L"%u.%u.%u.%u", v.Major, v.Minor, v.Build, v.Revision);
            return std::wstring(buf);
        }

        std::wstring Utf8ToWide(const std::string& s)
        {
            if (s.empty())
                return {};
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            if (n <= 0)
                return {};
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }

        bool CreateParentsForFile(const std::wstring& filePath)
        {
            fs::path p(filePath);
            fs::path parent = p.parent_path();
            std::error_code ec;
            fs::create_directories(parent, ec);
            return !ec;
        }

        void WriteUtf8File(const std::wstring& path, const std::string& content)
        {
            std::ofstream out(path, std::ios::binary);
            out.write(content.data(), (std::streamsize)content.size());
        }

        bool ReadUtf8File(const std::wstring& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        // Extract a zip into destDir using miniz. Returns false on failure.
        bool ExtractZip(const std::string& zipPathUtf8, const std::wstring& destDir)
        {
            mz_zip_archive zip;
            memset(&zip, 0, sizeof(zip));
            if (!mz_zip_reader_init_file(&zip, zipPathUtf8.c_str(), 0))
            {
                spdlog::error("[bootstrap] mz_zip_reader_init_file FAILED: {}", zipPathUtf8);
                return false;
            }

            mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
            bool ok = true;
            for (mz_uint i = 0; i < numFiles; i++)
            {
                mz_zip_archive_file_stat st;
                memset(&st, 0, sizeof(st));
                if (!mz_zip_reader_file_stat(&zip, i, &st))
                {
                    ok = false;
                    break;
                }

                char name[512];
                if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                {
                    ok = false;
                    break;
                }

                std::string rel(name);
                if (rel.empty() || rel.find("..") != std::string::npos)
                    continue; // skip traversal paths

                std::wstring outPath = destDir + L"\\" + Utf8ToWide(rel);

                if (mz_zip_reader_is_file_a_directory(&zip, i))
                {
                    fs::create_directories(outPath);
                    continue;
                }

                if (!CreateParentsForFile(outPath))
                {
                    spdlog::error("[bootstrap] mkdir FAILED for {}", rel);
                    ok = false;
                    break;
                }

                // miniz 3.x has no NULL-buffer size probe — the size comes
                // from the file stat.
                size_t size = (size_t)st.m_uncomp_size;
                std::vector<uint8_t> data(size);
                if (!mz_zip_reader_extract_to_mem(&zip, i, data.data(), size, 0))
                {
                    spdlog::error("[bootstrap] extract FAILED for {}", rel);
                    ok = false;
                    break;
                }

                FILE* f = _wfopen(outPath.c_str(), L"wb");
                if (!f)
                {
                    spdlog::error("[bootstrap] open FAILED for {}", rel);
                    ok = false;
                    break;
                }
                size_t written = fwrite(data.data(), 1, size, f);
                fclose(f);
                if (written != size)
                {
                    spdlog::error("[bootstrap] write FAILED for {}", rel);
                    ok = false;
                    break;
                }

                if ((i % 200) == 0)
                    spdlog::info("[bootstrap] extracted {}/{} files", (int)i, (int)numFiles);
            }

            mz_zip_reader_end(&zip);
            return ok;
        }

        bool DirectoryHasData(const std::wstring& systemDir)
        {
            // scummvm.ini or any game data present → treat as ready.
            if (fs::exists(systemDir + L"\\scummvm.ini"))
                return true;
            std::error_code ec;
            fs::directory_iterator it(systemDir, ec);
            if (ec)
                return false;
            int count = 0;
            for (auto& e : it)
            {
                (void)e;
                if (++count >= 3)
                    return true;
            }
            return count > 0;
        }

        // Stage the zip into systemDir. Version-gated via .scummvm-ready.
        void StageIfNeeded(const std::wstring& systemDir, const std::wstring& saveDir,
                           const std::wstring& flagValue, bool gateByVersion)
        {
            std::wstring flagPath = systemDir + L"\\.scummvm-ready";

            bool needExtract = !fs::exists(flagPath);
            if (gateByVersion && fs::exists(flagPath))
            {
                std::string flagContent;
                if (ReadUtf8File(flagPath, flagContent))
                {
                    std::wstring flagW = Utf8ToWide(flagContent);
                    if (flagW != flagValue)
                    {
                        spdlog::info("[bootstrap] app version changed ({} -> {}), re-staging", flagW, flagValue);
                        needExtract = true;
                    }
                }
            }

            if (!needExtract && !DirectoryHasData(systemDir))
            {
                spdlog::info("[bootstrap] system dir exists but is empty — re-staging");
                needExtract = true;
            }

            std::wstring zipPath = Bootstrap::InstalledLocationDir() + L"\\system\\scummvm.zip";
            if (!fs::exists(zipPath))
            {
                spdlog::error("[bootstrap] scummvm.zip MISSING at {}", zipPath);
                return;
            }

            if (needExtract)
            {
                spdlog::info("[bootstrap] extracting scummvm.zip -> {}", systemDir);
                std::wstring tmpDir = systemDir + L".tmp";
                std::error_code ec;
                fs::remove_all(tmpDir, ec);

                std::string zipPathUtf8;
                {
                    // miniz needs a UTF-8 path; convert.
                    int n = WideCharToMultiByte(CP_UTF8, 0, zipPath.c_str(), (int)zipPath.size(), nullptr, 0, nullptr, nullptr);
                    std::string zp(n, '\0');
                    if (n > 0)
                        WideCharToMultiByte(CP_UTF8, 0, zipPath.c_str(), (int)zipPath.size(), &zp[0], n, nullptr, nullptr);
                    zipPathUtf8 = zp;
                }

                bool ok = ExtractZip(zipPathUtf8, tmpDir);
                if (ok)
                {
                    fs::remove_all(systemDir, ec);
                    fs::rename(tmpDir, systemDir, ec);
                    if (ec)
                    {
                        spdlog::error("[bootstrap] rename {} -> {} failed", tmpDir, systemDir);
                        ok = false;
                    }
                }
                if (!ok)
                {
                    fs::remove_all(tmpDir, ec);
                    spdlog::error("[bootstrap] staging FAILED — falling back to existing dir if present");
                }
            }

            // scummvm.ini — only when absent (preserve user config).
            std::wstring iniPath = systemDir + L"\\scummvm.ini";
            if (!fs::exists(iniPath))
            {
                WriteUtf8File(iniPath, "[scummvm]\ngui_theme=scummremastered\ngui_scale=150\n");
                spdlog::info("[bootstrap] scummvm.ini written (first run)");
            }
            else
            {
                spdlog::info("[bootstrap] scummvm.ini present — keeping user config");
            }

            WriteUtf8File(flagPath, std::string(flagValue.begin(), flagValue.end()));
            spdlog::info("[bootstrap] ready flag written");
        }
    }

    std::wstring Bootstrap::LocalStateDir()
    {
        return std::wstring(ApplicationData::Current->LocalFolder->Path->Data());
    }

    std::wstring Bootstrap::InstalledLocationDir()
    {
        return std::wstring(Package::Current->InstalledLocation->Path->Data());
    }

    bool Bootstrap::FileExistsInLocalState(const wchar_t* fileName)
    {
        std::wstring path = Bootstrap::LocalStateDir() + L"\\" + fileName;
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool Bootstrap::Run()
    {
        std::wstring localState = Bootstrap::LocalStateDir();
        std::wstring systemDir = localState + L"\\system";
        std::wstring saveDir = localState + L"\\saves";
        std::error_code ec;
        fs::create_directories(systemDir, ec);
        fs::create_directories(saveDir, ec);

        spdlog::info("[bootstrap] LocalState root (system={}, saves={})", systemDir, saveDir);
        // Always LocalState (desktop and Xbox): the internal SSD is fast and
        // version-gating via .scummvm-ready re-stages cleanly on updates.
        StageIfNeeded(systemDir, saveDir, GetAppVersionString(), /*gateByVersion=*/true);

        DataPaths::SetPaths(systemDir, saveDir);
        spdlog::info("[bootstrap] SYSTEM_DIR={}", DataPaths::g_systemDirUtf8);
        spdlog::info("[bootstrap] SAVE_DIR={}", DataPaths::g_saveDirUtf8);
        return true;
    }
}
