#include "pch.h"
#include "SdlInput.h"
#include <cstring>

using namespace scummvm_uwp;

using namespace Windows::Gaming::Input;

SdlInput::SdlInput()
    : m_uwpGamepad(nullptr)
    , m_initialized(false)
    , m_hasController(false)
{
    memset(m_buttonHeld, 0, sizeof(m_buttonHeld));
    memset(m_buttonJustPressed, 0, sizeof(m_buttonJustPressed));
    m_controllerName[0] = '\0';
}

SdlInput::~SdlInput()
{
    m_uwpGamepad = nullptr;
}

bool SdlInput::Initialize()
{
    m_initialized = true;
    OutputDebugStringA("[scummvm-uwp] SdlInput: UWP Gamepad API mode\n");
    return true;
}

void SdlInput::PollEvents()
{
    memset(m_buttonJustPressed, 0, sizeof(m_buttonJustPressed));

    m_leftStickX = 0.0f;
    m_leftStickY = 0.0f;
    m_triggerL = 0.0f;
    m_triggerR = 0.0f;

    PollUwpGamepad();

    // Triggers -> L2/R2 digital buttons
    bool triggerL = m_triggerL > 0.5f;
    bool triggerR = m_triggerR > 0.5f;
    if (triggerL && !m_buttonHeld[BUTTON_L2]) { m_buttonHeld[BUTTON_L2] = true; m_buttonJustPressed[BUTTON_L2] = true; }
    else if (!triggerL && m_buttonHeld[BUTTON_L2]) m_buttonHeld[BUTTON_L2] = false;
    if (triggerR && !m_buttonHeld[BUTTON_R2]) { m_buttonHeld[BUTTON_R2] = true; m_buttonJustPressed[BUTTON_R2] = true; }
    else if (!triggerR && m_buttonHeld[BUTTON_R2]) m_buttonHeld[BUTTON_R2] = false;
}

void SdlInput::SetKeyboardButton(int btn, bool held)
{
    if (btn >= 0 && btn < MAX_BUTTONS)
    {
        m_buttonHeld[btn] = held;
        if (held)
        {
            m_buttonJustPressed[btn] = true;
        }
    }
}

void SdlInput::PollUwpGamepad()
{
    if (!m_uwpGamepad)
    {
        auto gamepads = Gamepad::Gamepads;
        if (gamepads->Size > 0)
        {
            m_uwpGamepad = gamepads->GetAt(0);
            m_hasController = true;
            strcpy_s(m_controllerName, sizeof(m_controllerName), "UWP Gamepad");
            OutputDebugStringA("[scummvm-uwp] UWP Gamepad connected\n");
        }
    }

    if (m_uwpGamepad)
    {
        auto reading = m_uwpGamepad->GetCurrentReading();

        m_leftStickX = reading.LeftThumbstickX;
        m_leftStickY = reading.LeftThumbstickY;
        m_rightStickX = reading.RightThumbstickX;
        m_rightStickY = reading.RightThumbstickY;
        m_triggerL = reading.LeftTrigger;
        m_triggerR = reading.RightTrigger;

        struct { GamepadButtons flag; int btn; } map[] = {
            { GamepadButtons::A, BUTTON_A },
            { GamepadButtons::B, BUTTON_B },
            { GamepadButtons::X, BUTTON_X },
            { GamepadButtons::Y, BUTTON_Y },
            { GamepadButtons::LeftThumbstick, BUTTON_L3 },
            { GamepadButtons::RightThumbstick, BUTTON_R3 },
            { GamepadButtons::View, BUTTON_SELECT },
            { GamepadButtons::Menu, BUTTON_START },
            { GamepadButtons::LeftShoulder, BUTTON_L },
            { GamepadButtons::RightShoulder, BUTTON_R },
            { GamepadButtons::DPadUp, BUTTON_DPAD_UP },
            { GamepadButtons::DPadDown, BUTTON_DPAD_DOWN },
            { GamepadButtons::DPadLeft, BUTTON_DPAD_LEFT },
            { GamepadButtons::DPadRight, BUTTON_DPAD_RIGHT },
        };

        for (auto& m : map)
        {
            bool held = (reading.Buttons & m.flag) != GamepadButtons::None;
            if (held && !m_buttonHeld[m.btn])
            {
                m_buttonHeld[m.btn] = true;
                m_buttonJustPressed[m.btn] = true;
            }
            else if (!held && m_buttonHeld[m.btn])
            {
                m_buttonHeld[m.btn] = false;
            }
        }
    }
    else
    {
        m_leftStickX = 0.0f;
        m_leftStickY = 0.0f;
        m_rightStickX = 0.0f;
        m_rightStickY = 0.0f;
        m_triggerL = 0.0f;
        m_triggerR = 0.0f;
    }
}

bool SdlInput::IsButtonHeld(int btn) const
{
    if (btn < 0 || btn >= MAX_BUTTONS) return false;
    return m_buttonHeld[btn];
}

bool SdlInput::WasButtonJustPressed(int btn)
{
    if (btn < 0 || btn >= MAX_BUTTONS) return false;
    bool result = m_buttonJustPressed[btn];
    m_buttonJustPressed[btn] = false;
    return result;
}
