#include "pch.h"
#include "FileBrowser.h"
#include "SettingsManager.h"
#include <algorithm>
#include <cmath>

using namespace dosbox_uwp;
using namespace Microsoft::WRL;
using namespace Windows::ApplicationModel;
using namespace Windows::Storage;

// Supported file extensions for DOS games
static const wchar_t* s_supportedExts[] = {
    L".zip", L".dosz", L".exe", L".com", L".bat",
    L".iso", L".chd",  L".cue", L".img", L".ima",
    L".vhd", L".conf"
};
static const int s_numExts = sizeof(s_supportedExts) / sizeof(s_supportedExts[0]);

static bool WideEqualsIgnoreCase(const std::wstring& a, const wchar_t* b)
{
    if (a.size() != wcslen(b)) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (towlower(a[i]) != towlower(b[i])) return false;
    return true;
}

static bool HasSupportedExtension(const std::wstring& name)
{
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = name.substr(dot);
    for (int i = 0; i < s_numExts; i++)
        if (WideEqualsIgnoreCase(ext, s_supportedExts[i])) return true;
    return false;
}

// Path utilities
static std::wstring EnsureTrailingSlash(const std::wstring& path)
{
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        return path + L"\\";
    return path;
}

FileBrowser::FileBrowser()
{
    spdlog::info("[FileBrowser] Created");
}

void FileBrowser::Open()
{
    spdlog::info("[FileBrowser] Open — showing root drive list");
    m_visible = true;
    m_selected = 0;
    m_scrollOffset = 0;
    m_marqueeItemIdx = -1;
    m_marqueeStartTime = 0;
    ScanDirectory(L"");
}

void FileBrowser::Close()
{
    spdlog::info("[FileBrowser] Close");
    m_visible = false;
}

bool FileBrowser::PassesExtensionFilter(const std::wstring& name)
{
    return HasSupportedExtension(name);
}

std::wstring FileBrowser::GetParentPath(const std::wstring& path)
{
    if (path.empty()) return path;

    std::wstring p = path;
    // Remove trailing slash
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();

    size_t lastSlash = p.rfind(L'\\');
    if (lastSlash == std::wstring::npos)
        lastSlash = p.rfind(L'/');

    if (lastSlash == std::wstring::npos)
    {
        // Root of a drive like "C:" — return empty to go back to drive list
        if (p.size() == 2 && p[1] == L':')
        {
            spdlog::info("[FileBrowser] Parent of drive root '{}' -> root list", std::string(p.begin(), p.end()));
            return L"";
        }
        return L"";
    }

    std::wstring parent = p.substr(0, lastSlash + 1);
    spdlog::info("[FileBrowser] Parent of '{}' -> '{}'",
        std::string(path.begin(), path.end()),
        std::string(parent.begin(), parent.end()));
    return parent;
}

void FileBrowser::ScanDirectory(const std::wstring& path)
{
    m_entries.clear();
    m_selected = 0;
    m_scrollOffset = 0;
    m_marqueeItemIdx = -1;
    m_currentPath = path;

    spdlog::info("[FileBrowser] ScanDirectory: '{}'",
        path.empty() ? "(root)" : std::string(path.begin(), path.end()));

    if (path.empty())
    {
        // Root: enumerate drives + LocalFolder
        DWORD drives = GetLogicalDrives();
        spdlog::info("[FileBrowser] GetLogicalDrives() = 0x{:08X}", (unsigned)drives);

        for (int i = 0; i < 26; i++)
        {
            if (drives & (1 << i))
            {
                wchar_t driveLetter[] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0' };
                FileEntry entry;
                entry.name = driveLetter;
                entry.isDir = true;
                m_entries.push_back(entry);
                spdlog::info("[FileBrowser]   Drive: {}", std::string(driveLetter, driveLetter + 3));
            }
        }

        // Always add LocalFolder as home
        try
        {
            auto localFolder = ApplicationData::Current->LocalFolder;
            FileEntry homeEntry;
            homeEntry.name = L"[HOME] " + std::wstring(localFolder->Path->Data());
            homeEntry.isDir = true;
            m_entries.push_back(homeEntry);
            spdlog::info("[FileBrowser]   Home: {}", std::string(homeEntry.name.begin(), homeEntry.name.end()));
        }
        catch (Platform::Exception^ ex)
        {
            spdlog::warn("[FileBrowser]   Failed to get LocalFolder: {}", (int)ex->HResult);
        }

        spdlog::info("[FileBrowser] Root: {} entries", (int)m_entries.size());
        return;
    }

    // Directory listing via FindFirstFileExFromAppW
    std::wstring searchPath = EnsureTrailingSlash(path) + L"*";

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileExFromAppW(
        searchPath.c_str(), FindExInfoStandard, &findData,
        FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        spdlog::warn("[FileBrowser] FindFirstFileExFromAppW failed for '{}' (GLE={})",
            std::string(path.begin(), path.end()), GetLastError());
        // Add ".." so user can navigate back even from empty/unreadable drives
        if (!path.empty())
        {
            FileEntry dd;
            dd.name = L"..";
            dd.isDir = true;
            m_entries.push_back(dd);
        }
        return;
    }

    // Collect directories first, then files
    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    do
    {
        std::wstring name = findData.cFileName;

        // Skip "." directory
        if (name == L"." ) continue;

        // ".." goes first always
        if (name == L"..")
        {
            FileEntry entry;
            entry.name = L"..";
            entry.isDir = true;
            dirs.insert(dirs.begin(), entry);
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            FileEntry entry;
            entry.name = name;
            entry.isDir = true;
            dirs.push_back(entry);
        }
        else
        {
            // Only add files that pass extension filter
            if (PassesExtensionFilter(name))
            {
                FileEntry entry;
                entry.name = name;
                entry.isDir = false;
                files.push_back(entry);
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    // Sort directories (skip ".." at index 0) and files alphabetically
    if (dirs.size() > 1)
    {
        std::sort(dirs.begin() + 1, dirs.end(),
            [](const FileEntry& a, const FileEntry& b) {
                return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
            });
    }
    std::sort(files.begin(), files.end(),
        [](const FileEntry& a, const FileEntry& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });

    // Ensure ".." is always first for non-root paths (Win32 may omit it for drive roots)
    if (!path.empty())
    {
        bool hasDotDot = false;
        for (auto& d : dirs)
            if (d.name == L"..") { hasDotDot = true; break; }
        if (!hasDotDot)
        {
            FileEntry dd;
            dd.name = L"..";
            dd.isDir = true;
            dirs.insert(dirs.begin(), dd);
        }
    }

    // Merge: dirs first, then files
    m_entries = dirs;
    m_entries.insert(m_entries.end(), files.begin(), files.end());

    spdlog::info("[FileBrowser] '{}' -> {} dirs, {} files ({} total)",
        std::string(path.begin(), path.end()),
        (int)dirs.size(), (int)files.size(), (int)m_entries.size());
}

// ============================================================
// Rendering
// ============================================================

void FileBrowser::EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (m_resourcesCreated) return;

    // Load VCR OSD Mono
    wchar_t fontPath[MAX_PATH];
    auto installPath = Package::Current->InstalledLocation->Path;
    wcscpy_s(fontPath, installPath->Data());
    size_t flen = wcslen(fontPath);
    if (flen > 0 && flen < MAX_PATH - 60)
    {
        if (fontPath[flen - 1] != L'\\') { fontPath[flen] = L'\\'; flen++; }
        wcscpy_s(fontPath + flen, MAX_PATH - flen, L"Assets\\Fonts\\VCR_OSD_MONO_1.001.ttf");
        ComPtr<IDWriteFontFile> fontFile;
        if (SUCCEEDED(dwrite->CreateFontFileReference(fontPath, nullptr, &fontFile)))
        {
            ComPtr<IDWriteFactory5> dwrite5;
            if (SUCCEEDED(dwrite->QueryInterface(IID_PPV_ARGS(&dwrite5))))
            {
                ComPtr<IDWriteFontSetBuilder1> builder;
                if (SUCCEEDED(dwrite5->CreateFontSetBuilder(&builder)))
                {
                    if (SUCCEEDED(builder->AddFontFile(fontFile.Get())))
                    {
                        ComPtr<IDWriteFontSet> fontSet;
                        if (SUCCEEDED(builder->CreateFontSet(&fontSet)))
                        {
                            ComPtr<IDWriteFontCollection1> col1;
                            if (SUCCEEDED(dwrite5->CreateFontCollectionFromFontSet(fontSet.Get(), &col1)))
                            {
                                col1.As(&m_fontCollection);
                            }
                        }
                    }
                }
            }
        }
    }

    float fontSizeTitle = 33.0f;
    float fontSizeItem = 27.0f;
    float fontSizeFooter = 22.0f;
    if (screenW < 800.0f)
    {
        fontSizeTitle = 27.0f;
        fontSizeItem = 22.0f;
        fontSizeFooter = 18.0f;
    }

    {
        const auto& c = SettingsManager::GetTheme();
        d2d->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, c.overlay_alpha), &m_brushBlack);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.bg_panel), &m_brushBg);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.frame), &m_brushFrame);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.title_bg), &m_brushTitleBg);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.selection_bg), &m_brushSelectedBg);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.selection_text), &m_brushSelectedText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_normal), &m_brushItemText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_bios), &m_brushDirText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.file_text), &m_brushFileText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_value), &m_brushPathText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_disabled), &m_brushFooter);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_disabled), &m_brushDimPrefix);
    }

    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeTitle, L"en-US", &m_textFormatTitle);
    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeItem, L"en-US", &m_textFormatItem);
    m_textFormatItem->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeFooter, L"en-US", &m_textFormatPath);
    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeFooter, L"en-US", &m_textFormatFooter);

    m_resourcesCreated = true;
    spdlog::info("[FileBrowser] Resources created (fonts + brushes)");
}

void FileBrowser::ReleaseResources()
{
    if (!m_resourcesCreated) return;

    m_brushBg.Reset();
    m_brushFrame.Reset();
    m_brushTitleBg.Reset();
    m_brushSelectedBg.Reset();
    m_brushSelectedText.Reset();
    m_brushItemText.Reset();
    m_brushDirText.Reset();
    m_brushFileText.Reset();
    m_brushPathText.Reset();
    m_brushFooter.Reset();
    m_brushBlack.Reset();
    m_brushDimPrefix.Reset();
    m_textFormatTitle.Reset();
    m_textFormatItem.Reset();
    m_textFormatPath.Reset();
    m_textFormatFooter.Reset();
    m_fontCollection.Reset();

    m_resourcesCreated = false;
    spdlog::info("[FileBrowser] Resources released");
}

static ComPtr<ID2D1Brush> MakeAnimatedTitleBrush(ID2D1DeviceContext* d2d,
    float rectX, float rectY, float rectW, float rectH, uint32_t baseColor)
{
    float t = (float)(GetTickCount64() % 3000) / 3000.0f;
    float alpha = 0.70f + 0.30f * (0.5f + 0.5f * sinf(t * 6.283185f));
    D2D1_COLOR_F col = D2D1::ColorF(baseColor);
    col.a = alpha;
    ComPtr<ID2D1SolidColorBrush> brush;
    d2d->CreateSolidColorBrush(col, &brush);
    return brush;
}

static void DrawTextLineFB(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, IDWriteFontCollection* fc,
    IDWriteTextFormat* fmt, const wchar_t* text, UINT32 len, float x, float y, float w, float h,
    ID2D1Brush* brush)
{
    ComPtr<IDWriteTextLayout> layout;
    dwrite->CreateTextLayout(text, len, fmt, w, h, &layout);
    if (layout && fc)
    {
        DWRITE_TEXT_RANGE r = { 0, len };
        layout->SetFontCollection(fc, r);
    }
    if (layout)
    {
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        layout->SetTrimming(&trimming, nullptr);
        d2d->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush);
    }
}

void FileBrowser::DrawMarqueeText(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite,
    const wchar_t* text, UINT32 len,
    float x, float y, float maxW, float h,
    ID2D1Brush* brush, IDWriteFontCollection* fc)
{
    // Measure text width
    ComPtr<IDWriteTextLayout> measureLayout;
    dwrite->CreateTextLayout(text, len, m_textFormatItem.Get(), 9999.0f, h, &measureLayout);
    if (!measureLayout) return;
    if (fc)
    {
        DWRITE_TEXT_RANGE r = { 0, len };
        measureLayout->SetFontCollection(fc, r);
    }
    DWRITE_TEXT_METRICS tm;
    measureLayout->GetMetrics(&tm);

    float textWidth = tm.width;

    // If text fits, draw normally
    if (textWidth <= maxW)
    {
        DrawTextLineFB(d2d, dwrite, fc, m_textFormatItem.Get(), text, len, x, y, maxW, h, brush);
        return;
    }

    // Marquee: clip and animate
    D2D1_RECT_F clipRect = { x, y, x + maxW, y + h };
    d2d->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    double elapsedSec = (GetTickCount64() - m_marqueeStartTime) / 1000.0;
    float scrollDist = textWidth - maxW + MARQUEE_PAD;
    float scrollTime = scrollDist / MARQUEE_SPEED_PX;
    float cycleTime = MARQUEE_PAUSE_SEC + scrollTime + MARQUEE_PAUSE_SEC;
    float phase = (float)fmod(elapsedSec, (double)cycleTime);

    float offset = 0.0f;
    if (phase < MARQUEE_PAUSE_SEC)
    {
        offset = 0.0f; // pause at start
    }
    else if (phase < MARQUEE_PAUSE_SEC + scrollTime)
    {
        offset = (phase - MARQUEE_PAUSE_SEC) * MARQUEE_SPEED_PX;
    }
    else
    {
        offset = scrollDist; // pause at end
    }

    ComPtr<IDWriteTextLayout> layout;
    dwrite->CreateTextLayout(text, len, m_textFormatItem.Get(), textWidth + 1.0f, h, &layout);
    if (layout && fc)
    {
        DWRITE_TEXT_RANGE r = { 0, len };
        layout->SetFontCollection(fc, r);
    }
    if (layout)
        d2d->DrawTextLayout(D2D1::Point2F(x - offset, y), layout.Get(), brush);

    d2d->PopAxisAlignedClip();
}

void FileBrowser::Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (!m_visible) return;

    EnsureResources(d2d, dwrite, screenW, screenH);
    const auto& theme = SettingsManager::GetTheme();

    m_lastScreenW = screenW;
    m_lastScreenH = screenH;

    // Semi-transparent background overlay
    D2D1_RECT_F fullBg = { 0, 0, screenW, screenH };
    d2d->FillRectangle(fullBg, m_brushBlack.Get());

    // Panel dimensions
    m_panelW = screenW * PANEL_WIDTH_RATIO;
    if (m_panelW > PANEL_MAX_WIDTH) m_panelW = PANEL_MAX_WIDTH;
    if (m_panelW < PANEL_MIN_WIDTH) m_panelW = PANEL_MIN_WIDTH;
    m_panelH = screenH * PANEL_HEIGHT_RATIO;
    if (m_panelH > PANEL_MAX_HEIGHT) m_panelH = PANEL_MAX_HEIGHT;
    if (m_panelH < PANEL_MIN_HEIGHT) m_panelH = PANEL_MIN_HEIGHT;
    m_panelX = 30.0f;
    m_panelY = screenH - m_panelH - 40.0f;

    // Panel background
    D2D1_RECT_F panelBg = { m_panelX, m_panelY, m_panelX + m_panelW, m_panelY + m_panelH };
    d2d->FillRectangle(panelBg, m_brushBg.Get());

    // Outer frame
    d2d->DrawRectangle(panelBg, m_brushFrame.Get(), 2.0f);
    D2D1_RECT_F innerFrame = { m_panelX + 4.0f, m_panelY + 4.0f,
        m_panelX + m_panelW - 4.0f, m_panelY + m_panelH - 4.0f };
    d2d->DrawRectangle(innerFrame, m_brushFrame.Get(), 1.0f);

    // Title bar (animated gradient)
    float titleY = m_panelY + PANEL_PADDING;
    D2D1_RECT_F titleBg = { m_panelX + PANEL_PADDING, titleY,
        m_panelX + m_panelW - PANEL_PADDING, titleY + TITLE_HEIGHT };
    auto fbTitleBrush = MakeAnimatedTitleBrush(d2d, titleBg.left, titleBg.top,
        titleBg.right - titleBg.left, titleBg.bottom - titleBg.top, theme.title_bg);
    d2d->FillRectangle(titleBg, fbTitleBrush.Get());

    // Title text: "FILE BROWSER"
    {
        const wchar_t* titleText = L"FILE BROWSER";
        ComPtr<IDWriteTextLayout> titleLayout;
        dwrite->CreateTextLayout(titleText, (UINT32)wcslen(titleText), m_textFormatTitle.Get(),
            m_panelW * 0.5f, TITLE_HEIGHT, &titleLayout);
        if (titleLayout)
        {
            if (m_fontCollection)
            {
                DWRITE_TEXT_RANGE fr = { 0, (UINT32)wcslen(titleText) };
                titleLayout->SetFontCollection(m_fontCollection.Get(), fr);
            }
            d2d->DrawTextLayout(D2D1::Point2F(m_panelX + ITEM_INDENT, titleY + 4.0f),
                titleLayout.Get(), m_brushSelectedText.Get());
        }
    }

    // Path text (right side of title bar)
    if (!m_currentPath.empty())
    {
        ComPtr<IDWriteTextLayout> pathLayout;
        std::wstring pathText = m_currentPath;
        dwrite->CreateTextLayout(pathText.c_str(), (UINT32)pathText.size(), m_textFormatPath.Get(),
            m_panelW * 0.55f, TITLE_HEIGHT, &pathLayout);
        if (pathLayout)
        {
            if (m_fontCollection)
            {
                DWRITE_TEXT_RANGE fr = { 0, (UINT32)pathText.size() };
                pathLayout->SetFontCollection(m_fontCollection.Get(), fr);
            }
            DWRITE_TEXT_METRICS ptm;
            pathLayout->GetMetrics(&ptm);
            float pathX = m_panelX + m_panelW - ptm.width - ITEM_INDENT - PANEL_PADDING;
            d2d->DrawTextLayout(D2D1::Point2F(pathX, titleY + 4.0f),
                pathLayout.Get(), m_brushPathText.Get());
        }
    }

    // Item list area
    float itemAreaTop = titleY + TITLE_HEIGHT + 8.0f;
    float itemAreaBottom = m_panelY + m_panelH - PANEL_PADDING - FOOTER_HEIGHT - 8.0f;
    float listAvailable = itemAreaBottom - itemAreaTop;
    int maxFit = (int)(listAvailable / ITEM_HEIGHT);
    if (maxFit < 1) maxFit = 1;
    if (maxFit > (int)MAX_VISIBLE) maxFit = (int)MAX_VISIBLE;

    int itemCount = (int)m_entries.size();
    int visibleCount = min(itemCount, maxFit);

    // Clamp scroll
    if (m_scrollOffset > itemCount - visibleCount)
        m_scrollOffset = max(0, itemCount - visibleCount);
    if (m_scrollOffset < 0) m_scrollOffset = 0;

    float listY = itemAreaTop;

    for (int i = 0; i < visibleCount; i++)
    {
        int idx = m_scrollOffset + i;
        auto& entry = m_entries[idx];
        float iy = listY + i * ITEM_HEIGHT;

        bool isSelected = (idx == m_selected);

        if (isSelected)
        {
            D2D1_RECT_F selRect = { m_panelX + PANEL_PADDING, iy,
                m_panelX + m_panelW - PANEL_PADDING, iy + ITEM_HEIGHT };
            d2d->FillRectangle(selRect, m_brushSelectedBg.Get());
        }

        // Prefix: [DIR] or [   ]
        const wchar_t* prefix = entry.isDir ? L"[DIR] " : L"[   ] ";
        UINT32 prefixLen = (UINT32)wcslen(prefix);

        ComPtr<IDWriteTextLayout> prefixLayout;
        dwrite->CreateTextLayout(prefix, prefixLen, m_textFormatItem.Get(),
            100.0f, ITEM_HEIGHT, &prefixLayout);
        if (prefixLayout && m_fontCollection)
        {
            DWRITE_TEXT_RANGE fr = { 0, prefixLen };
            prefixLayout->SetFontCollection(m_fontCollection.Get(), fr);
        }

        float prefixWidth = 0.0f;
        if (prefixLayout)
        {
            DWRITE_TEXT_METRICS ptm;
            prefixLayout->GetMetrics(&ptm);
            prefixWidth = ptm.width;
        }

        // Draw prefix
        auto prefixBrush = isSelected ? m_brushDimPrefix.Get() :
            (entry.isDir ? m_brushDirText.Get() : m_brushDimPrefix.Get());
        if (prefixLayout)
            d2d->DrawTextLayout(D2D1::Point2F(m_panelX + ITEM_INDENT, iy),
                prefixLayout.Get(), prefixBrush);

        // Draw name (with marquee for long names)
        float nameX = m_panelX + ITEM_INDENT + prefixWidth + 4.0f;
        float nameMaxW = m_panelW - ITEM_INDENT * 3 - prefixWidth;

        auto nameBrush = isSelected ? m_brushSelectedText.Get() :
            (entry.isDir ? m_brushDirText.Get() : m_brushFileText.Get());

        // Track marquee state
        if (isSelected)
        {
            if (m_marqueeItemIdx != idx)
            {
                m_marqueeItemIdx = idx;
                m_marqueeStartTime = GetTickCount64();
            }
            DrawMarqueeText(d2d, dwrite, entry.name.c_str(), (UINT32)entry.name.size(),
                nameX, iy, nameMaxW, ITEM_HEIGHT, nameBrush, m_fontCollection.Get());
        }
        else
        {
            DrawTextLineFB(d2d, dwrite, m_fontCollection.Get(), m_textFormatItem.Get(),
                entry.name.c_str(), (UINT32)entry.name.size(),
                nameX, iy, nameMaxW, ITEM_HEIGHT, nameBrush);
        }
    }

    // Scroll indicator (right edge)
    if (itemCount > visibleCount)
    {
        float scrollBarH = listAvailable;
        float thumbH = scrollBarH * ((float)visibleCount / (float)itemCount);
        float thumbY = listY + scrollBarH * ((float)m_scrollOffset / (float)itemCount);
        if (thumbH < 10.0f) thumbH = 10.0f;

        D2D1_RECT_F scrollBg = { m_panelX + m_panelW - PANEL_PADDING - 4.0f, listY,
            m_panelX + m_panelW - PANEL_PADDING, listY + scrollBarH };
        d2d->FillRectangle(scrollBg, m_brushFrame.Get());

        D2D1_RECT_F scrollThumb = { m_panelX + m_panelW - PANEL_PADDING - 3.0f, thumbY,
            m_panelX + m_panelW - PANEL_PADDING - 1.0f, thumbY + thumbH };
        d2d->FillRectangle(scrollThumb, m_brushSelectedText.Get());
    }

    // Footer area
    float footerY = m_panelY + m_panelH - PANEL_PADDING - FOOTER_HEIGHT;

    // Footer area background
    D2D1_RECT_F footerBg = { m_panelX + PANEL_PADDING, footerY,
        m_panelX + m_panelW - PANEL_PADDING, footerY + FOOTER_HEIGHT };
    d2d->FillRectangle(footerBg, m_brushTitleBg.Get());

    // Item count indicator (left side of footer)
    if (itemCount > 0)
    {
        wchar_t countBuf[32];
        swprintf_s(countBuf, L"%d/%d", m_selected + 1, itemCount);
        ComPtr<IDWriteTextLayout> countLayout;
        dwrite->CreateTextLayout(countBuf, (UINT32)wcslen(countBuf), m_textFormatFooter.Get(),
            100.0f, FOOTER_HEIGHT, &countLayout);
        if (countLayout)
        {
            if (m_fontCollection)
            {
                DWRITE_TEXT_RANGE fr = { 0, (UINT32)wcslen(countBuf) };
                countLayout->SetFontCollection(m_fontCollection.Get(), fr);
            }
            d2d->DrawTextLayout(
                D2D1::Point2F(m_panelX + ITEM_INDENT, footerY),
                countLayout.Get(), m_brushFooter.Get());
        }
    }

    // Footer
    const wchar_t* footerText = L"A:Sel  B:Back  Y:Home  LB/RB:Page";
    ComPtr<IDWriteTextLayout> footerLayout;
    dwrite->CreateTextLayout(footerText, (UINT32)wcslen(footerText), m_textFormatFooter.Get(),
        m_panelW, FOOTER_HEIGHT, &footerLayout);
    if (footerLayout)
    {
        if (m_fontCollection)
        {
            DWRITE_TEXT_RANGE fr = { 0, (UINT32)wcslen(footerText) };
            footerLayout->SetFontCollection(m_fontCollection.Get(), fr);
        }
        DWRITE_TEXT_METRICS ftm;
        footerLayout->GetMetrics(&ftm);
        float footerX = m_panelX + (m_panelW - ftm.width) * 0.5f;
        d2d->DrawTextLayout(D2D1::Point2F(footerX, footerY),
            footerLayout.Get(), m_brushFooter.Get());
    }
}

// ============================================================
// Input
// ============================================================

void FileBrowser::OnDPad(bool up)
{
    if (!m_visible || m_entries.empty()) return;

    spdlog::info("[FileBrowser] OnDPad up={} selected={} total={}", up, m_selected, (int)m_entries.size());

    if (up)
    {
        do {
            m_selected--;
            if (m_selected < 0) m_selected = (int)m_entries.size() - 1;
        } while (m_selected > 0 && m_entries[m_selected].name.empty());
    }
    else
    {
        do {
            m_selected++;
            if (m_selected >= (int)m_entries.size()) m_selected = 0;
        } while (m_selected < (int)m_entries.size() - 1 && m_entries[m_selected].name.empty());
    }

    // Scroll management
    int maxVisible = (int)MAX_VISIBLE;
    if (m_selected < m_scrollOffset)
        m_scrollOffset = m_selected;
    if (m_selected >= m_scrollOffset + maxVisible)
        m_scrollOffset = m_selected - maxVisible + 1;
}

void FileBrowser::OnConfirm()
{
    if (!m_visible || m_entries.empty()) return;
    if (m_selected < 0 || m_selected >= (int)m_entries.size()) return;

    auto& entry = m_entries[m_selected];
    spdlog::info("[FileBrowser] OnConfirm: '{}' isDir={}", std::string(entry.name.begin(), entry.name.end()), entry.isDir);

    if (entry.isDir)
    {
        if (entry.name == L"..")
        {
            OnBack();
            return;
        }

        std::wstring newPath;
        if (m_currentPath.empty())
        {
            // Root level: entry name IS the path (e.g. "C:\")
            // Handle [HOME] prefix
            if (entry.name.substr(0, 7) == L"[HOME] ")
                newPath = entry.name.substr(7);
            else
                newPath = entry.name;
        }
        else
        {
            newPath = EnsureTrailingSlash(m_currentPath) + entry.name;
        }

        spdlog::info("[FileBrowser] Navigate into: '{}'", std::string(newPath.begin(), newPath.end()));
        ScanDirectory(newPath);
    }
    else
    {
        // File selected — build full path and callback
        std::wstring fullPath = EnsureTrailingSlash(m_currentPath) + entry.name;
        spdlog::info("[FileBrowser] File selected: '{}'", std::string(fullPath.begin(), fullPath.end()));

        if (onFileSelected)
        {
            spdlog::info("[FileBrowser] Invoking onFileSelected callback");
            onFileSelected(fullPath);
        }
    }
}

void FileBrowser::OnBack()
{
    if (!m_visible) return;

    if (m_currentPath.empty())
    {
        // Already at root — close browser
        spdlog::info("[FileBrowser] Back at root — closing");
        Close();
        return;
    }

    std::wstring parent = GetParentPath(m_currentPath);
    spdlog::info("[FileBrowser] Back: '{}' -> '{}'",
        std::string(m_currentPath.begin(), m_currentPath.end()),
        parent.empty() ? "(root)" : std::string(parent.begin(), parent.end()));
    ScanDirectory(parent);
}

void FileBrowser::OnPageUp()
{
    if (!m_visible || m_entries.empty()) return;

    int maxVisible = (int)MAX_VISIBLE;
    m_selected -= maxVisible;
    if (m_selected < 0) m_selected = 0;
    m_scrollOffset -= maxVisible;
    if (m_scrollOffset < 0) m_scrollOffset = 0;

    spdlog::info("[FileBrowser] PageUp -> selected={}", m_selected);
}

void FileBrowser::OnPageDown()
{
    if (!m_visible || m_entries.empty()) return;

    int maxVisible = (int)MAX_VISIBLE;
    m_selected += maxVisible;
    if (m_selected >= (int)m_entries.size()) m_selected = (int)m_entries.size() - 1;
    m_scrollOffset += maxVisible;
    if (m_scrollOffset > (int)m_entries.size() - maxVisible)
        m_scrollOffset = max(0, (int)m_entries.size() - maxVisible);

    spdlog::info("[FileBrowser] PageDown -> selected={}", m_selected);
}

void FileBrowser::OnHome()
{
    if (!m_visible) return;
    spdlog::info("[FileBrowser] Home -> LocalFolder");
    try
    {
        auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
        ScanDirectory(std::wstring(localFolder->Path->Data()));
    }
    catch (Platform::Exception^ ex)
    {
        spdlog::warn("[FileBrowser] Home failed: {}", (int)ex->HResult);
    }
}

int FileBrowser::HitTest(float sx, float sy)
{
    if (!m_visible) return -1;
    if (sx < m_panelX || sx > m_panelX + m_panelW) return -1;
    if (sy < m_panelY || sy > m_panelY + m_panelH) return -1;

    float titleY = m_panelY + PANEL_PADDING;
    float itemAreaTop = titleY + TITLE_HEIGHT + 8.0f;
    float itemAreaBottom = m_panelY + m_panelH - PANEL_PADDING - FOOTER_HEIGHT - 8.0f;
    float listAvailable = itemAreaBottom - itemAreaTop;
    int maxFit = (int)(listAvailable / ITEM_HEIGHT);
    if (maxFit < 1) maxFit = 1;
    if (maxFit > (int)MAX_VISIBLE) maxFit = (int)MAX_VISIBLE;
    int visibleCount = min((int)m_entries.size(), maxFit);

    float listY = itemAreaTop;
    if (sy < listY || sy > listY + visibleCount * ITEM_HEIGHT) return -1;

    int idx = (int)((sy - listY) / ITEM_HEIGHT) + m_scrollOffset;
    if (idx < 0 || idx >= (int)m_entries.size()) return -1;
    return idx;
}

void FileBrowser::HandlePointerMove(float sx, float sy)
{
    if (GetTickCount64() < m_wheelIgnoreUntil) return;
    int idx = HitTest(sx, sy);
    if (idx >= 0)
    {
        if (m_selected != idx)
        {
            m_selected = idx;
            m_marqueeItemIdx = -1; // reset marquee for new selection
            m_marqueeStartTime = GetTickCount64();
        }
    }
}

void FileBrowser::HandlePointerDown(float sx, float sy)
{
    if (!m_visible || m_entries.empty()) return;

    int idx = HitTest(sx, sy);
    if (idx < 0) return;

    // Click on same item -> confirm (open dir or select file)
    if (idx == m_selected)
    {
        OnConfirm();
        return;
    }

    // Click on different item -> select it
    m_selected = idx;
    m_marqueeItemIdx = -1;
    m_marqueeStartTime = GetTickCount64();
}

void FileBrowser::HandlePointerWheel(int delta)
{
    if (!m_visible || m_entries.empty()) return;
    m_wheelIgnoreUntil = GetTickCount64() + 300; // ignore mouse hover for 300ms

    // Scroll by 3 lines per notch
    int scroll = (delta > 0) ? -3 : 3;
    m_selected += scroll;
    if (m_selected < 0) m_selected = 0;
    if (m_selected >= (int)m_entries.size()) m_selected = (int)m_entries.size() - 1;

    int maxVisible = (int)MAX_VISIBLE;
    if (m_selected < m_scrollOffset)
        m_scrollOffset = m_selected;
    if (m_selected >= m_scrollOffset + maxVisible)
        m_scrollOffset = m_selected - maxVisible + 1;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
}
