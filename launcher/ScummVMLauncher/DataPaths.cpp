#include "pch.h"
#include "DataPaths.h"

namespace scummvm_uwp
{
    namespace DataPaths
    {
        std::string g_systemDirUtf8;
        std::string g_saveDirUtf8;

        void SetPaths(const std::wstring& systemDir, const std::wstring& saveDir)
        {
            auto toUtf8 = [](const std::wstring& w) -> std::string
            {
                if (w.empty())
                    return {};
                int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                if (n <= 0)
                    return {};
                std::string s(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
                return s;
            };
            g_systemDirUtf8 = toUtf8(systemDir);
            g_saveDirUtf8 = toUtf8(saveDir);
        }
    }
}
