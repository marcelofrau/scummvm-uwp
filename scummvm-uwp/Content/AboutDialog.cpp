#include "pch.h"
#include "AboutDialog.h"
#include "SettingsManager.h"
#include <cmath>

using namespace dosbox_uwp;
using namespace Microsoft::WRL;

AboutDialog::AboutDialog() {}

void AboutDialog::Open(const std::wstring& versionStr)
{
    m_visible = true;
    m_lines.clear();

    const uint32_t COL_RED    = 0xFFF8305b;
    const uint32_t COL_CYAN   = 0xFF59caf9;
    const uint32_t COL_GREEN  = 0xFF30f84c;
    const uint32_t COL_YELLOW = 0xFFF4f830;
    const uint32_t COL_WHITE  = 0xFFfefefe;
    const uint32_t COL_GRAY   = 0xFFaabbb9;
    const uint32_t COL_DIM    = 0xFF74898e;

    // Big title
    m_lines.push_back({ L"DOSBox Pure", COL_CYAN, FONT_BIG, true });
    m_lines.push_back({ L"UWP Port", COL_YELLOW, FONT_BODY, true });

    // Separator
    m_lines.push_back({ L"=", COL_DIM, FONT_SMALL, true });

    // Version + Author
    m_lines.push_back({ L"  Version:  " + versionStr, COL_GREEN, FONT_BODY, false });
    m_lines.push_back({ L"  Author:   marcelofrau", COL_WHITE, FONT_BODY, false });

    // Separator
    m_lines.push_back({ L"=", COL_DIM, FONT_SMALL, true });

    // Credits
    m_lines.push_back({ L"REFERENCES", COL_RED, FONT_BODY, true });
    m_lines.push_back({ L"  dosbox-pure: schellingb (libretro)", COL_GRAY, FONT_BODY, false });
    m_lines.push_back({ L"  DOSBox: the DOSBox Team", COL_GRAY, FONT_BODY, false });
    m_lines.push_back({ L"  ZillaLib: cross-platform framework", COL_GRAY, FONT_BODY, false });
    m_lines.push_back({ L"  RetroArch: libretro frontend", COL_GRAY, FONT_BODY, false });
    m_lines.push_back({ L"  xb-xray: Diagnostics + LUA + REPL", COL_GRAY, FONT_BODY, false });
    m_lines.push_back({ L"                   debugging tools", COL_DIM, FONT_SMALL, false });

    // Separator
    m_lines.push_back({ L"=", COL_DIM, FONT_SMALL, true });

    spdlog::info("[AboutDialog] Opened");
}

void AboutDialog::Close()
{
    m_visible = false;
    m_lines.clear();
}

void AboutDialog::ReleaseResources()
{
    if (!m_resourcesCreated) return;

    m_brushBg.Reset();
    m_brushFrame.Reset();
    m_brushTitleBg.Reset();
    m_brushTitleText.Reset();
    m_brushBodyText.Reset();
    m_brushCyan.Reset();
    m_brushGreen.Reset();
    m_brushYellow.Reset();
    m_brushRed.Reset();
    m_brushDim.Reset();
    m_brushBlack.Reset();
    m_brushSelection.Reset();
    m_textFormatBig.Reset();
    m_textFormatSmall.Reset();
    m_textFormatTitle.Reset();
    m_textFormatBody.Reset();
    m_textFormatFooter.Reset();
    m_dosboxLogo.Reset();
    m_fontCollection.Reset();

    m_resourcesCreated = false;
}

void AboutDialog::EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (m_resourcesCreated) return;

    const auto& theme = SettingsManager::GetTheme();

    // Load VCR OSD Mono font
    wchar_t fontPath[MAX_PATH];
    auto installPath = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;
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
                                col1.As(&m_fontCollection);
                        }
                    }
                }
            }
        }
    }

    // Brushes
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_panel), &m_brushBg);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.frame), &m_brushFrame);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.title_bg), &m_brushTitleBg);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.text_title), &m_brushTitleText);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.text_normal), &m_brushBodyText);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFF59caf9), &m_brushCyan);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFF30f84c), &m_brushGreen);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFFF4f830), &m_brushYellow);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFFF8305b), &m_brushRed);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFF74898e), &m_brushDim);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, theme.overlay_alpha), &m_brushBlack);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.selection_bg), &m_brushSelection);

    // Text formats
    float dpiscale;
    { FLOAT dx, dy; d2d->GetDpi(&dx, &dy); dpiscale = dx / 96.0f; }

    auto createFormat = [&](IDWriteTextFormat** fmt, float fontSize, bool isAscii = false) {
        dwrite->CreateTextFormat(
            L"VCR OSD Mono", m_fontCollection.Get(),
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize * dpiscale, L"en-us", fmt);
        if (*fmt) (*fmt)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    createFormat(&m_textFormatBig, 28.0f);
    createFormat(&m_textFormatSmall, 16.0f);
    createFormat(&m_textFormatTitle, 28.0f);
    createFormat(&m_textFormatBody, 18.0f);
    createFormat(&m_textFormatFooter, 14.0f);

    if (m_textFormatTitle) m_textFormatTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Load DOSBox logo
    if (!m_dosboxLogo)
    {
        wchar_t imgPath[MAX_PATH];
        wcscpy_s(imgPath, installPath->Data());
        size_t ilen = wcslen(imgPath);
        if (ilen > 0 && ilen < MAX_PATH - 40) {
            if (imgPath[ilen - 1] != L'\\') { imgPath[ilen] = L'\\'; ilen++; }
            wcscpy_s(imgPath + ilen, MAX_PATH - ilen, L"Assets\\dosbox.png");
        }
        ComPtr<IWICImagingFactory> wicFactory;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
        if (wicFactory)
        {
            ComPtr<IWICBitmapDecoder> decoder;
            if (SUCCEEDED(wicFactory->CreateDecoderFromFilename(imgPath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
            {
                ComPtr<IWICBitmapFrameDecode> frame;
                if (SUCCEEDED(decoder->GetFrame(0, &frame)))
                {
                    ComPtr<IWICFormatConverter> converter;
                    if (SUCCEEDED(wicFactory->CreateFormatConverter(&converter)) &&
                        SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
                    {
                        d2d->CreateBitmapFromWicBitmap(converter.Get(), &m_dosboxLogo);
                    }
                }
            }
        }
    }

    m_resourcesCreated = true;
    spdlog::info("[AboutDialog] Resources created (dpi={})", dpiscale);
}

void AboutDialog::Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (!m_visible) return;
    EnsureResources(d2d, dwrite, screenW, screenH);
    if (!m_resourcesCreated) return;

    const auto& theme = SettingsManager::GetTheme();

    // Semi-transparent fullscreen background
    d2d->FillRectangle(D2D1::RectF(0, 0, screenW, screenH), m_brushBlack.Get());

    // Panel size — centered, not too big
    float dpiscale;
    { FLOAT dx, dy; d2d->GetDpi(&dx, &dy); dpiscale = dx / 96.0f; }
    m_panelW = min(screenW * 0.60f, 520.0f);
    m_panelH = min(screenH * 0.82f, 580.0f);
    m_panelX = (screenW - m_panelW) * 0.5f;
    m_panelY = (screenH - m_panelH) * 0.5f;

    // Panel background
    d2d->FillRectangle(D2D1::RectF(m_panelX, m_panelY, m_panelX + m_panelW, m_panelY + m_panelH), m_brushBg.Get());

    // Panel frame (double border)
    d2d->DrawRectangle(D2D1::RectF(m_panelX, m_panelY, m_panelX + m_panelW, m_panelY + m_panelH), m_brushFrame.Get(), 2.0f);
    d2d->DrawRectangle(D2D1::RectF(m_panelX + 4, m_panelY + 4, m_panelX + m_panelW - 4, m_panelY + m_panelH - 4), m_brushFrame.Get(), 1.0f);

    // Title bar (animated pulse)
    float titleTop = m_panelY + 8;
    D2D1_RECT_F titleBg = { m_panelX + 8, titleTop, m_panelX + m_panelW - 8, titleTop + TITLE_HEIGHT };
    float t = (float)(GetTickCount64() % 3000) / 3000.0f;
    float alpha = 0.70f + 0.30f * (0.5f + 0.5f * sinf(t * 6.283185f));
    ComPtr<ID2D1SolidColorBrush> titlePulseBrush;
    D2D1_COLOR_F titleCol = D2D1::ColorF(theme.title_bg);
    titleCol.a = alpha;
    d2d->CreateSolidColorBrush(titleCol, &titlePulseBrush);
    d2d->FillRectangle(titleBg, titlePulseBrush.Get());

    // Title text "ABOUT" (centered in title bar)
    ComPtr<IDWriteTextLayout> titleLayout;
    float titleBarW = m_panelW - 32.0f;
    dwrite->CreateTextLayout(L"ABOUT", 5, m_textFormatTitle.Get(), titleBarW, TITLE_HEIGHT, &titleLayout);
    if (titleLayout && m_fontCollection)
    {
        DWRITE_TEXT_RANGE range = { 0, 5 };
        titleLayout->SetFontCollection(m_fontCollection.Get(), range);
    }
    if (titleLayout)
        d2d->DrawTextLayout(D2D1::Point2F(m_panelX + 16, titleTop), titleLayout.Get(), m_brushTitleText.Get());

    // DOSBox logo bitmap (if loaded) — draw centered above content area
    float contentY = m_panelY + 8 + TITLE_HEIGHT + 20;
    if (m_dosboxLogo)
    {
        D2D1_SIZE_F logoSize = m_dosboxLogo->GetSize();
        float logoMaxH = 110.0f * dpiscale;
        float scale = min(1.0f, logoMaxH / logoSize.height);
        float drawW = logoSize.width * scale;
        float drawH = logoSize.height * scale;
        float logoX = m_panelX + (m_panelW - drawW) * 0.5f;
        d2d->DrawBitmap(m_dosboxLogo.Get(), D2D1::RectF(logoX, contentY, logoX + drawW, contentY + drawH), 1.0f);
        contentY += drawH + 12.0f;
    }

    // Content area with clip
    float clipTop = contentY;
    float clipBottom = m_panelY + m_panelH - FOOTER_HEIGHT;
    d2d->PushAxisAlignedClip(D2D1::RectF(m_panelX, clipTop, m_panelX + m_panelW, clipBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Render lines
    float lineH = 24.0f * dpiscale;
    float bigH = 36.0f * dpiscale;
    float smallH = 20.0f * dpiscale;
    float yPos = contentY;

    for (auto& line : m_lines)
    {
        float h;
        IDWriteTextFormat* fmt;
        switch (line.font)
        {
        case FONT_BIG:  h = bigH;   fmt = m_textFormatBig.Get();   break;
        case FONT_SMALL: h = smallH; fmt = m_textFormatSmall.Get(); break;
        default:        h = lineH;  fmt = m_textFormatBody.Get();  break;
        }

        // Separator line — draw D2D horizontal line spanning panel
        if (line.text == L"=")
        {
            float lineY = yPos + h * 0.5f;
            d2d->DrawLine(
                D2D1::Point2F(m_panelX + PANEL_PADDING, lineY),
                D2D1::Point2F(m_panelX + m_panelW - PANEL_PADDING, lineY),
                m_brushDim.Get(), 1.0f);
            yPos += h;
            continue;
        }

        // Pick brush based on color
        ID2D1Brush* brush = m_brushBodyText.Get();
        if (line.color == 0xFF59caf9) brush = m_brushCyan.Get();
        else if (line.color == 0xFF30f84c) brush = m_brushGreen.Get();
        else if (line.color == 0xFFF4f830) brush = m_brushYellow.Get();
        else if (line.color == 0xFFF8305b) brush = m_brushRed.Get();
        else if (line.color == 0xFF74898e) brush = m_brushDim.Get();
        else if (line.color == 0xFFfefefe) brush = m_brushBodyText.Get();
        else if (line.color == theme.text_title) brush = m_brushTitleText.Get();

        float textW = m_panelW - PANEL_PADDING * 2;
        ComPtr<IDWriteTextLayout> layout;
        dwrite->CreateTextLayout(line.text.c_str(), (UINT32)line.text.size(), fmt, textW, h, &layout);
        if (layout && m_fontCollection)
        {
            DWRITE_TEXT_RANGE range = { 0, (UINT32)line.text.size() };
            layout->SetFontCollection(m_fontCollection.Get(), range);
        }
        if (layout)
        {
            float xPos = m_panelX + PANEL_PADDING;
            if (line.isCentered)
            {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                xPos = m_panelX + PANEL_PADDING;
            }
            d2d->DrawTextLayout(D2D1::Point2F(xPos, yPos), layout.Get(), brush);
        }

        yPos += h;
    }

    d2d->PopAxisAlignedClip();

    // Footer
    float footerY = m_panelY + m_panelH - FOOTER_HEIGHT;
    d2d->DrawLine(D2D1::Point2F(m_panelX + 12, footerY), D2D1::Point2F(m_panelX + m_panelW - 12, footerY), m_brushFrame.Get(), 1.0f);

    ComPtr<IDWriteTextLayout> footerLayout;
    dwrite->CreateTextLayout(L"Press any key to close", 22, m_textFormatBody.Get(), m_panelW, FOOTER_HEIGHT, &footerLayout);
    if (footerLayout && m_fontCollection)
    {
        DWRITE_TEXT_RANGE range = { 0, 22 };
        footerLayout->SetFontCollection(m_fontCollection.Get(), range);
    }
    if (footerLayout)
    {
        footerLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        d2d->DrawTextLayout(D2D1::Point2F(m_panelX + PANEL_PADDING, footerY + 4), footerLayout.Get(), m_brushDim.Get());
    }
}

void AboutDialog::OnConfirm()
{
    if (m_visible) Close();
}

void AboutDialog::OnBack()
{
    if (m_visible) Close();
}

int AboutDialog::HitTest(float sx, float sy)
{
    if (!m_visible) return -1;
    if (sx < m_panelX || sx > m_panelX + m_panelW || sy < m_panelY || sy > m_panelY + m_panelH)
        return -1;
    return 0; // any hit inside panel = close
}

void AboutDialog::HandlePointerDown(float sx, float sy)
{
    if (!m_visible) return;
    if (HitTest(sx, sy) >= 0) Close();
}
