#include "pch.h"
#include "ConfirmDialog.h"
#include "SettingsManager.h"
#include <cmath>
#include <Windows.h>

using namespace dosbox_uwp;
using namespace Microsoft::WRL;

ConfirmDialog::ConfirmDialog() {}

void ConfirmDialog::Open(const std::string& message, Mode mode, std::function<void(bool)> onResult)
{
    m_visible = true;
    m_message = message;
    m_mode = mode;
    m_onResult = onResult;
    m_selectedBtn = 0;
    ReleaseResources();
    spdlog::info("[ConfirmDialog] Open (mode={})", mode == CONFIRM ? "confirm" : "info");
}

void ConfirmDialog::Close()
{
    m_visible = false;
    ReleaseResources();
}

void ConfirmDialog::ReleaseResources()
{
    if (!m_resourcesCreated) return;
    m_brushBg.Reset();
    m_brushFrame.Reset();
    m_brushTitleBg.Reset();
    m_brushText.Reset();
    m_brushDim.Reset();
    m_brushBlack.Reset();
    m_brushBtnBg.Reset();
    m_brushBtnHover.Reset();
    m_brushBtnText.Reset();
    m_brushCyan.Reset();
    m_brushGreen.Reset();
    m_textFormatMsg.Reset();
    m_textFormatBtn.Reset();
    m_textFormatTitle.Reset();
    m_fontCollection.Reset();
    m_resourcesCreated = false;
}

void ConfirmDialog::EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
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

    // Brushes — using theme colors
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_panel), &m_brushBg);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.frame), &m_brushFrame);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.title_bg), &m_brushTitleBg);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.text_normal), &m_brushText);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.text_disabled), &m_brushDim);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, theme.overlay_alpha), &m_brushBlack);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_btn_on), &m_brushBtnBg);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_btn_hover), &m_brushBtnHover);
    d2d->CreateSolidColorBrush(D2D1::ColorF(theme.col_btn_text), &m_brushBtnText);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFF59caf9), &m_brushCyan);
    d2d->CreateSolidColorBrush(D2D1::ColorF(0xFF30f84c), &m_brushGreen);

    // Text formats
    float dpiscale;
    { FLOAT dx, dy; d2d->GetDpi(&dx, &dy); dpiscale = dx / 96.0f; }

    auto createFormat = [&](IDWriteTextFormat** fmt, float fontSize) {
        dwrite->CreateTextFormat(
            L"VCR OSD Mono", m_fontCollection.Get(),
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize * dpiscale, L"en-us", fmt);
        if (*fmt) (*fmt)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    createFormat(&m_textFormatTitle, 33.0f);
    createFormat(&m_textFormatMsg, 27.0f);
    createFormat(&m_textFormatBtn, 22.0f);

    if (m_textFormatTitle) m_textFormatTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    m_resourcesCreated = true;
    spdlog::info("[ConfirmDialog] Resources created");
}

void ConfirmDialog::Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (!m_visible) return;
    EnsureResources(d2d, dwrite, screenW, screenH);
    if (!m_resourcesCreated) return;

    const auto& theme = SettingsManager::GetTheme();
    float dpiscale;
    { FLOAT dx, dy; d2d->GetDpi(&dx, &dy); dpiscale = dx / 96.0f; }

    // Semi-transparent fullscreen background
    d2d->FillRectangle(D2D1::RectF(0, 0, screenW, screenH), m_brushBlack.Get());

    // Panel size — centered
    m_panelW = min(screenW * 0.60f, 520.0f);
    m_panelH = 260.0f * dpiscale;
    m_panelX = (screenW - m_panelW) * 0.5f;
    m_panelY = (screenH - m_panelH) * 0.5f;

    // Panel background
    d2d->FillRectangle(D2D1::RectF(m_panelX, m_panelY, m_panelX + m_panelW, m_panelY + m_panelH), m_brushBg.Get());

    // Panel frame
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

    // Title text (centered in title bar)
    const wchar_t* titleText = (m_mode == CONFIRM) ? L"CONFIRM" : L"INFO";
    float titleTextW = m_panelW - 32;
    ComPtr<IDWriteTextLayout> titleLayout;
    dwrite->CreateTextLayout(titleText, (UINT32)wcslen(titleText), m_textFormatTitle.Get(), titleTextW, TITLE_HEIGHT, &titleLayout);
    if (titleLayout && m_fontCollection)
    {
        DWRITE_TEXT_RANGE range = { 0, (UINT32)wcslen(titleText) };
        titleLayout->SetFontCollection(m_fontCollection.Get(), range);
    }
    if (titleLayout)
        d2d->DrawTextLayout(D2D1::Point2F(m_panelX + 16, titleTop + 6), titleLayout.Get(), m_brushBtnText.Get());

    // Message text (centered in panel)
    float msgY = m_panelY + 8 + TITLE_HEIGHT + 24;
    float msgH = m_panelH - TITLE_HEIGHT - BTN_HEIGHT - 60;
    ComPtr<IDWriteTextLayout> msgLayout;
    // Convert narrow to wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, m_message.c_str(), -1, nullptr, 0);
    std::wstring wmsg(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, m_message.c_str(), -1, &wmsg[0], wlen);
    dwrite->CreateTextLayout(wmsg.c_str(), (UINT32)wmsg.size(), m_textFormatMsg.Get(), m_panelW - PANEL_PADDING * 2, msgH, &msgLayout);
    if (msgLayout && m_fontCollection)
    {
        DWRITE_TEXT_RANGE range = { 0, (UINT32)wmsg.size() };
        msgLayout->SetFontCollection(m_fontCollection.Get(), range);
    }
    if (msgLayout)
    {
        msgLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        d2d->DrawTextLayout(D2D1::Point2F(m_panelX + PANEL_PADDING, msgY), msgLayout.Get(), m_brushText.Get());
    }

    // Buttons
    float btnY = m_panelY + m_panelH - BTN_HEIGHT - 16;
    float btnW = 120.0f;

    if (m_mode == CONFIRM)
    {
        // OK button (right)
        m_btnOkW = btnW;
        m_btnOkH = BTN_HEIGHT;
        m_btnOkX = m_panelX + m_panelW - PANEL_PADDING - btnW;
        m_btnOkY = btnY;
        bool okSelected = (m_selectedBtn == 0);
        d2d->FillRectangle(D2D1::RectF(m_btnOkX, m_btnOkY, m_btnOkX + m_btnOkW, m_btnOkY + m_btnOkH),
            okSelected ? m_brushBtnBg.Get() : m_brushBtnHover.Get());
        if (okSelected)
        {
            d2d->DrawRectangle(D2D1::RectF(m_btnOkX, m_btnOkY, m_btnOkX + m_btnOkW, m_btnOkY + m_btnOkH), m_brushCyan.Get(), 2.0f);
            d2d->DrawRectangle(D2D1::RectF(m_btnOkX + 3, m_btnOkY + 3, m_btnOkX + m_btnOkW - 3, m_btnOkY + m_btnOkH - 3), m_brushCyan.Get(), 1.0f);
        }
        else
        {
            d2d->DrawRectangle(D2D1::RectF(m_btnOkX, m_btnOkY, m_btnOkX + m_btnOkW, m_btnOkY + m_btnOkH), m_brushFrame.Get(), 1.0f);
        }

        ComPtr<IDWriteTextLayout> okLayout;
        dwrite->CreateTextLayout(L"OK", 2, m_textFormatBtn.Get(), btnW, BTN_HEIGHT, &okLayout);
        if (okLayout && m_fontCollection)
        {
            DWRITE_TEXT_RANGE range = { 0, 2 };
            okLayout->SetFontCollection(m_fontCollection.Get(), range);
        }
        if (okLayout)
        {
            okLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2d->DrawTextLayout(D2D1::Point2F(m_btnOkX, m_btnOkY + 9), okLayout.Get(), m_brushBtnText.Get());
        }

        // Cancel button (left of OK)
        m_btnCancelW = btnW;
        m_btnCancelH = BTN_HEIGHT;
        m_btnCancelX = m_btnOkX - BTN_SPACING - btnW;
        m_btnCancelY = btnY;
        bool cancelSelected = (m_selectedBtn == 1);
        d2d->FillRectangle(D2D1::RectF(m_btnCancelX, m_btnCancelY, m_btnCancelX + m_btnCancelW, m_btnCancelY + m_btnCancelH),
            cancelSelected ? m_brushBtnBg.Get() : m_brushBtnHover.Get());
        if (cancelSelected)
        {
            d2d->DrawRectangle(D2D1::RectF(m_btnCancelX, m_btnCancelY, m_btnCancelX + m_btnCancelW, m_btnCancelY + m_btnCancelH), m_brushCyan.Get(), 2.0f);
            d2d->DrawRectangle(D2D1::RectF(m_btnCancelX + 3, m_btnCancelY + 3, m_btnCancelX + m_btnCancelW - 3, m_btnCancelY + m_btnCancelH - 3), m_brushCyan.Get(), 1.0f);
        }
        else
        {
            d2d->DrawRectangle(D2D1::RectF(m_btnCancelX, m_btnCancelY, m_btnCancelX + m_btnCancelW, m_btnCancelY + m_btnCancelH), m_brushFrame.Get(), 1.0f);
        }

        ComPtr<IDWriteTextLayout> cancelLayout;
        dwrite->CreateTextLayout(L"Cancel", 6, m_textFormatBtn.Get(), btnW, BTN_HEIGHT, &cancelLayout);
        if (cancelLayout && m_fontCollection)
        {
            DWRITE_TEXT_RANGE range = { 0, 6 };
            cancelLayout->SetFontCollection(m_fontCollection.Get(), range);
        }
        if (cancelLayout)
        {
            cancelLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2d->DrawTextLayout(D2D1::Point2F(m_btnCancelX, m_btnCancelY + 9), cancelLayout.Get(), m_brushBtnText.Get());
        }
    }
    else // INFO mode — single OK button centered
    {
        m_btnOkW = btnW;
        m_btnOkH = BTN_HEIGHT;
        m_btnOkX = m_panelX + (m_panelW - btnW) * 0.5f;
        m_btnOkY = btnY;
        m_btnCancelW = m_btnCancelH = 0;

        d2d->FillRectangle(D2D1::RectF(m_btnOkX, m_btnOkY, m_btnOkX + m_btnOkW, m_btnOkY + m_btnOkH), m_brushBtnBg.Get());
        d2d->DrawRectangle(D2D1::RectF(m_btnOkX, m_btnOkY, m_btnOkX + m_btnOkW, m_btnOkY + m_btnOkH), m_brushCyan.Get(), 2.0f);
        d2d->DrawRectangle(D2D1::RectF(m_btnOkX + 3, m_btnOkY + 3, m_btnOkX + m_btnOkW - 3, m_btnOkY + m_btnOkH - 3), m_brushCyan.Get(), 1.0f);

        ComPtr<IDWriteTextLayout> okLayout;
        dwrite->CreateTextLayout(L"OK", 2, m_textFormatBtn.Get(), btnW, BTN_HEIGHT, &okLayout);
        if (okLayout && m_fontCollection)
        {
            DWRITE_TEXT_RANGE range = { 0, 2 };
            okLayout->SetFontCollection(m_fontCollection.Get(), range);
        }
        if (okLayout)
        {
            okLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2d->DrawTextLayout(D2D1::Point2F(m_btnOkX, m_btnOkY + 9), okLayout.Get(), m_brushBtnText.Get());
        }
    }
}

void ConfirmDialog::OnConfirm()
{
    if (!m_visible) return;
    bool wasConfirm = (m_mode == CONFIRM);
    Close();
    if (m_onResult) m_onResult(true);
}

void ConfirmDialog::OnBack()
{
    if (!m_visible) return;
    Close();
    if (m_onResult) m_onResult(false);
}

void ConfirmDialog::HandlePointerDown(float sx, float sy)
{
    if (!m_visible) return;

    // Check OK button
    if (sx >= m_btnOkX && sx <= m_btnOkX + m_btnOkW && sy >= m_btnOkY && sy <= m_btnOkY + m_btnOkH)
    {
        OnConfirm();
        return;
    }

    // Check Cancel button (confirm mode only)
    if (m_mode == CONFIRM && m_btnCancelW > 0)
    {
        if (sx >= m_btnCancelX && sx <= m_btnCancelX + m_btnCancelW && sy >= m_btnCancelY && sy <= m_btnCancelY + m_btnCancelH)
        {
            OnBack();
            return;
        }
    }
}

void ConfirmDialog::HandleKeyDown(unsigned int vkey)
{
    if (!m_visible) return;

    if (vkey == VK_LEFT || vkey == VK_RIGHT)
    {
        if (m_mode == CONFIRM && m_btnCancelW > 0)
            m_selectedBtn = (vkey == VK_LEFT) ? 1 : 0;
    }
    else if (vkey == VK_RETURN || vkey == VK_SPACE)
    {
        if (m_selectedBtn == 0)
            OnConfirm();
        else
            OnBack();
    }
    else if (vkey == VK_ESCAPE)
    {
        OnBack();
    }
}
