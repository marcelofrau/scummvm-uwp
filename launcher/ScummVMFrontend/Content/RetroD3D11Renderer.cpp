#include "pch.h"
#include "RetroD3D11Renderer.h"
#include "Common\DirectXHelper.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>

using namespace scummvm_uwp;
using namespace Microsoft::WRL;
using namespace DirectX;

// Minimal vertex shader: position (float2) + texcoord (float2)
static const char* s_vsHLSL = R"(
struct VSInput {
    float2 pos : POSITION;
    float2 tex : TEXCOORD0;
};
struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};
PSInput main(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.tex = input.tex;
    return output;
}
)";

// Minimal pixel shader: sample texture
static const char* s_psHLSL = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};
float4 main(PSInput input) : SV_TARGET {
    return tex.Sample(samp, input.tex);
}
)";

// Vertex format: position (float2) + texcoord (float2)
struct VertexPositionTexcoord
{
    XMFLOAT2 pos;
    XMFLOAT2 tex;
};

static D3D11_INPUT_ELEMENT_DESC s_inputLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

RetroD3D11Renderer::RetroD3D11Renderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
{
}

RetroD3D11Renderer::~RetroD3D11Renderer()
{
    ReleaseDeviceDependentResources();
}

void RetroD3D11Renderer::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Compile vertex shader
    ComPtr<ID3DBlob> vsBlob, vsError;
    HRESULT hr = D3DCompile(s_vsHLSL, strlen(s_vsHLSL), "vs.hlsl", nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vsBlob, &vsError);
    if (FAILED(hr))
    {
        OutputDebugStringA("[dosbox-uwp] D3D11 VS compile FAILED\n");
        if (vsError) OutputDebugStringA((char*)vsError->GetBufferPointer());
        return;
    }
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, &m_vertexShader);

    // Input layout
    device->CreateInputLayout(s_inputLayout, 2,
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);

    // Compile pixel shader
    ComPtr<ID3DBlob> psBlob, psError;
    hr = D3DCompile(s_psHLSL, strlen(s_psHLSL), "ps.hlsl", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &psBlob, &psError);
    if (FAILED(hr))
    {
        OutputDebugStringA("[dosbox-uwp] D3D11 PS compile FAILED\n");
        if (psError) OutputDebugStringA((char*)psError->GetBufferPointer());
        return;
    }
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
        nullptr, &m_pixelShader);

    // Sampler: linear filtering, clamp to edge
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, &m_samplerState);

    // Index buffer: 2 triangles for a quad
    static const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = { indices };
    device->CreateBuffer(&ibDesc, &ibData, &m_indexBuffer);

    // Pre-allocate staging texture at max common DOSBox resolution (1024x768)
    // Avoids expensive recreation on resolution changes (640x400 ↔ 640x480)
    {
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = 1024;
        stagingDesc.Height = 768;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
        m_stagingWidth = 1024;
        m_stagingHeight = 768;
        OutputDebugStringA("[dosbox-uwp] D3D11: pre-allocated staging 1024x768\n");
    }

    OutputDebugStringA("[dosbox-uwp] D3D11 renderer: shaders compiled OK\n");
}

void RetroD3D11Renderer::ReleaseDeviceDependentResources()
{
    m_gpuTexture.Reset();
    m_textureSRV.Reset();
    m_stagingTexture.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_inputLayout.Reset();
    m_samplerState.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_frameWidth = 0;
    m_frameHeight = 0;
    m_stagingWidth = 0;
    m_stagingHeight = 0;
}

void RetroD3D11Renderer::UpdateVideoFrame(const uint8_t* data, unsigned width, unsigned height, unsigned pitch)
{
    if (!data || width == 0 || height == 0)
        return;

    auto device = m_deviceResources->GetD3DDevice();
    auto context = m_deviceResources->GetD3DDeviceContext();

    // Recreate textures on resolution change
    if (!m_gpuTexture || m_frameWidth != width || m_frameHeight != height)
    {
        // Only recreate GPU texture + SRV (staging is pre-allocated at 1024x768)
        m_gpuTexture.Reset();
        m_textureSRV.Reset();

        // GPU texture (default usage, shader resource)
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &m_gpuTexture);
        if (FAILED(hr)) return;

        // Shader resource view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_gpuTexture.Get(), &srvDesc, &m_textureSRV);

        // Recreate staging only if frame is larger than pre-allocated (1024x768)
        if (!m_stagingTexture || width > m_stagingWidth || height > m_stagingHeight)
        {
            m_stagingTexture.Reset();
            D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
            m_stagingWidth = width;
            m_stagingHeight = height;
            spdlog::info("[D3D11] staging texture recreated {}x{}", width, height);
        }

        m_frameWidth = width;
        m_frameHeight = height;

        // Recreate vertex buffer with letterbox geometry
        float screenW = (float)m_deviceResources->GetLogicalSize().Width;
        float screenH = (float)m_deviceResources->GetLogicalSize().Height;
        float scaleX = screenW / (float)width;
        float scaleY = screenH / (float)height;
        float scale = min(scaleX, scaleY);
        float drawW = width * scale / screenW;   // NDC width
        float drawH = height * scale / screenH;  // NDC height

        // Map screen to NDC [-1, 1]
        float left   = -drawW;
        float right  =  drawW;
        float top    =  drawH;
        float bottom = -drawH;

        VertexPositionTexcoord vertices[] =
        {
            { { left,  bottom }, { 0.0f, 1.0f } },  // BL
            { { left,  top    }, { 0.0f, 0.0f } },  // TL
            { { right, top    }, { 1.0f, 0.0f } },  // TR
            { { right, bottom }, { 1.0f, 1.0f } },  // BR
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(vertices);
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData = { vertices };
        m_vertexBuffer.Reset();
        device->CreateBuffer(&vbDesc, &vbData, &m_vertexBuffer);

        char buf[256];
        sprintf_s(buf, "[dosbox-uwp] D3D11: textures recreated %ux%u\n", width, height);
        OutputDebugStringA(buf);
    }

    // Upload CPU framebuffer → staging → GPU
    if (m_stagingTexture)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_WRITE, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            // Copy row by row (source pitch may differ from dest pitch)
            unsigned dstRowBytes = width * 4;
            const uint8_t* src = data;
            uint8_t* dst = (uint8_t*)mapped.pData;
            for (unsigned y = 0; y < height; y++)
            {
                memcpy(dst + y * mapped.RowPitch, src + y * pitch, dstRowBytes);
            }
            context->Unmap(m_stagingTexture.Get(), 0);

            // GPU copy staging → GPU texture (clip to frame dimensions since staging may be larger)
            D3D11_BOX srcBox = { 0, 0, 0, width, height, 1 };
            context->CopySubresourceRegion(m_gpuTexture.Get(), 0, 0, 0, 0,
                m_stagingTexture.Get(), 0, &srcBox);
        }
    }
}

void RetroD3D11Renderer::Render()
{
    if (!m_gpuTexture || !m_vertexShader || !m_pixelShader)
        return;

    auto context = m_deviceResources->GetD3DDeviceContext();

    // Set pipeline state
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT stride = sizeof(VertexPositionTexcoord);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, m_textureSRV.GetAddressOf());
    context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    // Draw 6 indices (2 triangles = 1 quad)
    context->DrawIndexed(6, 0, 0);
}
