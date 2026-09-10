#pragma once

#include <string>

// Paths the libretro core resolves via GET_SYSTEM_DIRECTORY / GET_SAVE_DIRECTORY.
// Set by Bootstrap::Run() before the core boots. UTF-8 (libretro convention).
namespace scummvm_uwp
{
    namespace DataPaths
    {
        extern std::string g_systemDirUtf8;
        extern std::string g_saveDirUtf8;

        void SetPaths(const std::wstring& systemDir, const std::wstring& saveDir);
    }
}
