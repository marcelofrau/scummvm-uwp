#pragma once
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdint>
#include <windows.gaming.input.h>

namespace dosbox_uwp
{
    enum { BUTTON_A = 0, BUTTON_B = 1, BUTTON_X = 2, BUTTON_Y = 3, BUTTON_L = 17, BUTTON_R = 18, BUTTON_L2 = 19, BUTTON_R2 = 20,
           BUTTON_START = 7, BUTTON_SELECT = 6, BUTTON_R3 = 11, BUTTON_L3 = 12,
           BUTTON_DPAD_UP = 13, BUTTON_DPAD_DOWN = 14, BUTTON_DPAD_LEFT = 15, BUTTON_DPAD_RIGHT = 16 };

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
        bool HasControllerSDL() const { return m_controller != nullptr; }
        bool HasControllerUWP() const { return m_uwpGamepad != nullptr; }
        const char* GetControllerName() const { return m_controllerName; }
        const char* GetLastEventText() const { return m_lastEventStr; }
        void SetKeyboardButton(int btn, bool held);
        void GetLeftStick(float& x, float& y) const { x = m_leftStickX; y = m_leftStickY; }

    private:
        static const int MAX_BUTTONS = 32;
        void PollUwpGamepad();

        SDL_GameController* m_controller;
        Windows::Gaming::Input::Gamepad^ m_uwpGamepad;
        bool m_buttonHeld[MAX_BUTTONS];
        bool m_buttonJustPressed[MAX_BUTTONS];
        int m_controllerCount;
        bool m_initialized;
        bool m_hasController;
        char m_lastEventStr[64];
        char m_controllerName[128];
        float m_leftStickX = 0.0f;
        float m_leftStickY = 0.0f;
        float m_triggerL = 0.0f;
        float m_triggerR = 0.0f;
    };
}
