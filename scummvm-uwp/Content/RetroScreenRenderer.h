#pragma once

#include "..\Common\DeviceResources.h"
#include <cstdint>
#include <wrl.h>

namespace dosbox_uwp
{
    class RetroScreenRenderer
    {
    public:
        RetroScreenRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
        void CreateDeviceDependentResources();
        void ReleaseDeviceDependentResources();
        void UpdateVideoFrame(const uint8_t* data, unsigned width, unsigned height, unsigned pitch);
        void Render();
        void SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE mode) { m_interpolationMode = mode; }
        D2D1_BITMAP_INTERPOLATION_MODE GetInterpolationMode() const { return m_interpolationMode; }

    private:
        void RecreateBitmap(unsigned width, unsigned height);

        std::shared_ptr<DX::DeviceResources> m_deviceResources;

        Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_videoBitmap;
        unsigned m_frameWidth = 0;
        unsigned m_frameHeight = 0;
        D2D1_BITMAP_INTERPOLATION_MODE m_interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    };
}
