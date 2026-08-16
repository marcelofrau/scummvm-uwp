#pragma once

#include "..\Common\DeviceResources.h"
#include <cstdint>
#include <wrl.h>

namespace scummvm_uwp
{
    class RetroD3D11Renderer
    {
    public:
        RetroD3D11Renderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
        ~RetroD3D11Renderer();

        void CreateDeviceDependentResources();
        void ReleaseDeviceDependentResources();
        void UpdateVideoFrame(const uint8_t* data, unsigned width, unsigned height, unsigned pitch);
        void Render();

    private:
        std::shared_ptr<DX::DeviceResources> m_deviceResources;

        // GPU texture (default usage, bound to shader)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_gpuTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureSRV;

        // Staging texture (CPU-accessible, for upload)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTexture;

        // Shader pipeline
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;

        unsigned m_frameWidth = 0;
        unsigned m_frameHeight = 0;
        unsigned m_stagingWidth = 0;
        unsigned m_stagingHeight = 0;
    };
}
