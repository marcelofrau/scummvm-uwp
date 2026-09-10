#pragma once

#include <cstdint>
#include <windows.gaming.input.h>

namespace scummvm_uwp
{
    enum { BUTTON_A = 0, BUTTON_B = 1, BUTTON_X = 2, BUTTON_Y = 3, BUTTON_L = 17, BUTTON_R = 18, BUTTON_L2 = 19, BUTTON_R2 = 20,
           BUTTON_START = 7, BUTTON_SELECT = 6, BUTTON_R3 = 11, BUTTON_L3 = 12,
           BUTTON_DPAD_UP = 13, BUTTON_DPAD_DOWN = 14, BUTTON_DPAD_LEFT = 15, BUTTON_DPAD_RIGHT = 16 };

    // Gamepad input via the UWP Gamepad API only (works on Xbox; the
    // SDL joystick API is unavailable there). Maps physical buttons to the
    // RetroPad-style BUTTON_* indices consumed by RetroCore::SetJoypadButton.
    class SdlInput
    {
    public:
        SdlInput();
        ~SdlInput();

        bool Initialize();
        void PollEvents();

        bool IsButtonHeld(int btn) const;
        bool WasButtonJustPressed(int btn);
        bool IsAnyButtonHeld() const { return m_buttonHeld[0]; }
        bool HasController() const { return m_hasController; }
        bool IsInitialized() const { return m_initialized; }
        bool HasControllerUWP() const { return m_uwpGamepad != nullptr; }
        const char* GetControllerName() const { return m_controllerName; }
        void SetKeyboardButton(int btn, bool held);
        void GetLeftStick(float& x, float& y) const { x = m_leftStickX; y = m_leftStickY; }
        void GetRightStick(float& x, float& y) const { x = m_rightStickX; y = m_rightStickY; }

    private:
        static const int MAX_BUTTONS = 32;
        void PollUwpGamepad();

        Windows::Gaming::Input::Gamepad^ m_uwpGamepad;
        bool m_buttonHeld[MAX_BUTTONS];
        bool m_buttonJustPressed[MAX_BUTTONS];
        bool m_initialized;
        bool m_hasController;
        char m_controllerName[128];
        float m_leftStickX = 0.0f;
        float m_leftStickY = 0.0f;
        float m_rightStickX = 0.0f;
        float m_rightStickY = 0.0f;
        float m_triggerL = 0.0f;
        float m_triggerR = 0.0f;
    };
}
