#pragma once

#include <string>

namespace scummvm_uwp
{
    // Stages ScummVM system data and sets DataPaths. Runs once at boot.
    // - Xbox: prefers E:\scummvm (system + saves), falls back to LocalState.
    // - Desktop: LocalState only.
    // - Extracts InstalledLocation\system\scummvm.zip into <root>\system when
    //   the .scummvm-ready flag is missing or the app version changed.
    // - Writes <root>\system\scummvm.ini only when absent (preserves user edits).
    namespace Bootstrap
    {
        bool Run();
        std::wstring LocalStateDir();
        std::wstring InstalledLocationDir();
        bool FileExistsInLocalState(const wchar_t* fileName);
    }
}
