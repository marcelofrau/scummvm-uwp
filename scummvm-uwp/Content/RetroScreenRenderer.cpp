#include "pch.h"
#include "RetroScreenRenderer.h"
#include "Common\DirectXHelper.h"

using namespace dosbox_uwp;
using namespace Microsoft::WRL;

RetroScreenRenderer::RetroScreenRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
{
    OutputDebugStringA("[dosbox-uwp] RetroScreenRenderer ctor\n");
}

void RetroScreenRenderer::CreateDeviceDependentResources()
{
    OutputDebugStringA("[dosbox-uwp] RetroScreenRenderer::CreateDeviceDependentResources\n");
}

void RetroScreenRenderer::ReleaseDeviceDependentResources()
{
    OutputDebugStringA("[dosbox-uwp] RetroScreenRenderer::ReleaseDeviceDependentResources\n");
    m_videoBitmap.Reset();
    m_frameWidth = 0;
    m_frameHeight = 0;
}

void RetroScreenRenderer::UpdateVideoFrame(const uint8_t* data, unsigned width, unsigned height, unsigned pitch)
{
    if (!data || width == 0 || height == 0)
    {
        OutputDebugStringA("[dosbox-uwp] UpdateVideoFrame: invalid params\n");
        return;
    }

    char buf[256];
#ifdef FRAME_TRACE
    sprintf_s(buf, "[dosbox-uwp] UpdateVideoFrame: %ux%u pitch=%u d2dContext=%p\n",
        width, height, pitch, m_deviceResources->GetD2DDeviceContext());
    OutputDebugStringA(buf);
#endif

    auto d2dContext = m_deviceResources->GetD2DDeviceContext();

    if (!m_videoBitmap || m_frameWidth != width || m_frameHeight != height)
    {
        sprintf_s(buf, "[dosbox-uwp]   RecreateBitmap needed: old=%ux%u new=%ux%u\n",
            m_frameWidth, m_frameHeight, width, height);
        OutputDebugStringA(buf);
        RecreateBitmap(width, height);
    }

    if (m_videoBitmap)
    {
        D2D1_RECT_U rect = { 0, 0, width, height };
        HRESULT hr = m_videoBitmap->CopyFromMemory(&rect, data, pitch);
        if (FAILED(hr))
        {
            sprintf_s(buf, "[dosbox-uwp]   CopyFromMemory FAILED hr=0x%08X\n", hr);
            OutputDebugStringA(buf);
        }
    }
    else
    {
        OutputDebugStringA("[dosbox-uwp]   m_videoBitmap is NULL after RecreateBitmap!\n");
    }
}

void RetroScreenRenderer::Render()
{
    if (!m_videoBitmap)
    {
        OutputDebugStringA("[dosbox-uwp] Render: no bitmap, skip\n");
        return;
    }

    auto d2dContext = m_deviceResources->GetD2DDeviceContext();
    auto logicalSize = m_deviceResources->GetLogicalSize();

    char buf[256];
#ifdef FRAME_TRACE
    sprintf_s(buf, "[dosbox-uwp] Render: frame=%ux%u logical=%.0fx%.0f\n",
        m_frameWidth, m_frameHeight, logicalSize.Width, logicalSize.Height);
    OutputDebugStringA(buf);
#endif

    d2dContext->BeginDraw();

    float scaleX = logicalSize.Width / (float)m_frameWidth;
    float scaleY = logicalSize.Height / (float)m_frameHeight;
    float scale = min(scaleX, scaleY);

    float drawW = m_frameWidth * scale;
    float drawH = m_frameHeight * scale;
    float offsetX = (logicalSize.Width - drawW) * 0.5f;
    float offsetY = (logicalSize.Height - drawH) * 0.5f;

    D2D1_RECT_F destRect = D2D1::RectF(offsetX, offsetY, offsetX + drawW, offsetY + drawH);

#ifdef FRAME_TRACE
    sprintf_s(buf, "[dosbox-uwp]   DrawBitmap dest=(%.0f,%.0f)-(%.0f,%.0f) bitmap=%p\n",
        destRect.left, destRect.top, destRect.right, destRect.bottom, m_videoBitmap.Get());
    OutputDebugStringA(buf);
#endif

    d2dContext->DrawBitmap(
        m_videoBitmap.Get(),
        destRect,
        1.0f,
        m_interpolationMode,
        nullptr
    );

    HRESULT hr = d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        OutputDebugStringA("[dosbox-uwp]   EndDraw: D2DERR_RECREATE_TARGET\n");
        m_videoBitmap.Reset();
        m_frameWidth = 0;
        m_frameHeight = 0;
    }
    else if (FAILED(hr))
    {
        sprintf_s(buf, "[dosbox-uwp]   EndDraw FAILED hr=0x%08X\n", hr);
        OutputDebugStringA(buf);
    }
}

void RetroScreenRenderer::RecreateBitmap(unsigned width, unsigned height)
{
    auto d2dContext = m_deviceResources->GetD2DDeviceContext();

    m_videoBitmap.Reset();

    float dpiX, dpiY;
    d2dContext->GetDpi(&dpiX, &dpiY);

    char buf[256];
    sprintf_s(buf, "[dosbox-uwp] RecreateBitmap: %ux%u dpi=%.0fx%.0f context=%p\n",
        width, height, dpiX, dpiY, d2dContext);
    OutputDebugStringA(buf);

    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    props.dpiX = dpiX;
    props.dpiY = dpiY;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

    ComPtr<ID2D1Bitmap1> bitmap;
    HRESULT hr = d2dContext->CreateBitmap(
        D2D1::SizeU(width, height),
        nullptr,
        0,
        props,
        &bitmap
    );

    if (SUCCEEDED(hr))
    {
        m_videoBitmap = bitmap;
        m_frameWidth = width;
        m_frameHeight = height;
        sprintf_s(buf, "[dosbox-uwp]   CreateBitmap OK bitmap=%p\n", m_videoBitmap.Get());
        OutputDebugStringA(buf);
    }
    else
    {
        sprintf_s(buf, "[dosbox-uwp]   CreateBitmap FAILED hr=0x%08X\n", hr);
        OutputDebugStringA(buf);
    }
}
