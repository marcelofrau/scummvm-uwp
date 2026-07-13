#include "pch.h"
#include "SdlInput.h"
#include <cstring>
#include <vector>
#include <cmath>

using namespace dosbox_uwp;

using namespace Windows::Gaming::Input;
using namespace Windows::Foundation;

SdlInput::SdlInput()
    : m_controller(nullptr)
    , m_uwpGamepad(nullptr)
    , m_controllerCount(0)
    , m_initialized(false)
    , m_hasController(false)
{
    memset(m_buttonHeld, 0, sizeof(m_buttonHeld));
    memset(m_buttonJustPressed, 0, sizeof(m_buttonJustPressed));
    m_lastEventStr[0] = '\0';
    m_controllerName[0] = '\0';
}

SdlInput::~SdlInput()
{
    m_uwpGamepad = nullptr;
    if (m_controller)
    {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
    if (m_initialized)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
    }
}

bool SdlInput::Initialize()
{
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) < 0)
    {
        OutputDebugStringA("SDL_Init FAILED\n");
        return false;
    }
    m_initialized = true;

    m_controllerCount = SDL_NumJoysticks();
    if (m_controllerCount > 0)
    {
        m_controller = SDL_GameControllerOpen(0);
        m_hasController = (m_controller != nullptr);
        if (m_hasController)
        {
            const char* name = SDL_GameControllerName(m_controller);
            strcpy_s(m_controllerName, sizeof(m_controllerName), name ? name : "Unknown");
            char buf[128];
            sprintf_s(buf, "Controller: %s\n", m_controllerName);
            OutputDebugStringA(buf);
        }
    }
    else
    {
        OutputDebugStringA("No SDL controller. Will try UWP Gamepad API...\n");
        m_hasController = false;
    }
    return true;
}

void SdlInput::PollEvents()
{
    // clear edge-triggered flags
    memset(m_buttonJustPressed, 0, sizeof(m_buttonJustPressed));

    // reset analog sticks + triggers — set by whichever path reads them below
    m_leftStickX = 0.0f;
    m_leftStickY = 0.0f;
    m_triggerL = 0.0f;
    m_triggerR = 0.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_CONTROLLERDEVICEADDED:
        {
            int joystickIndex = event.cdevice.which;
            if (!m_controller)
            {
                m_controller = SDL_GameControllerOpen(joystickIndex);
                if (m_controller)
                {
                    m_hasController = true;
                    const char* name = SDL_GameControllerName(m_controller);
                    strcpy_s(m_controllerName, sizeof(m_controllerName), name ? name : "Unknown");
                    // close UWP fallback since SDL now owns the controller
                    m_uwpGamepad = nullptr;
                    sprintf_s(m_lastEventStr, "CTL:CONNECTED %s", m_controllerName);
                    char buf[256];
                    sprintf_s(buf, "SDL controller connected: %s\n", m_controllerName);
                    OutputDebugStringA(buf);
                }
            }
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
        {
            SDL_GameController* removed = SDL_GameControllerFromInstanceID(event.cdevice.which);
            if (removed == m_controller)
            {
                SDL_GameControllerClose(m_controller);
                m_controller = nullptr;
                m_hasController = false;
                m_controllerName[0] = '\0';
                memset(m_buttonHeld, 0, sizeof(m_buttonHeld));
                sprintf_s(m_lastEventStr, "CTL:DISCONNECTED");
                OutputDebugStringA("SDL controller disconnected\n");
                // UWP fallback will re-open in PollUwpGamepad next frame
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        {
            sprintf_s(m_lastEventStr, "CTL:btn%d DOWN", event.cbutton.button);
            break;
        }
        case SDL_CONTROLLERBUTTONUP:
        {
            sprintf_s(m_lastEventStr, "CTL:btn%d UP", event.cbutton.button);
            break;
        }
        case SDL_KEYDOWN:
            if (!event.key.repeat && event.key.keysym.sym == SDLK_SPACE)
            {
                m_buttonHeld[BUTTON_A] = true;
                m_buttonJustPressed[BUTTON_A] = true;
                sprintf_s(m_lastEventStr, "KB:SPACE=A DOWN");
            }
            break;
        case SDL_KEYUP:
            if (event.key.keysym.sym == SDLK_SPACE)
            {
                m_buttonHeld[BUTTON_A] = false;
                sprintf_s(m_lastEventStr, "KB:SPACE=A UP");
            }
            break;
        }
    }

    // Poll current button state as safety net for missed SDL events
    if (m_controller)
    {
        struct { SDL_GameControllerButton sdl; int local; } map[] = {
            { SDL_CONTROLLER_BUTTON_A, BUTTON_A },
            { SDL_CONTROLLER_BUTTON_B, BUTTON_B },
            { SDL_CONTROLLER_BUTTON_X, BUTTON_X },
            { SDL_CONTROLLER_BUTTON_Y, BUTTON_Y },
            { SDL_CONTROLLER_BUTTON_LEFTSTICK, BUTTON_L3 },
            { SDL_CONTROLLER_BUTTON_RIGHTSTICK, BUTTON_R3 },
            { SDL_CONTROLLER_BUTTON_BACK, BUTTON_SELECT },
            { SDL_CONTROLLER_BUTTON_START, BUTTON_START },
            { SDL_CONTROLLER_BUTTON_DPAD_UP, BUTTON_DPAD_UP },
            { SDL_CONTROLLER_BUTTON_DPAD_DOWN, BUTTON_DPAD_DOWN },
            { SDL_CONTROLLER_BUTTON_DPAD_LEFT, BUTTON_DPAD_LEFT },
            { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BUTTON_DPAD_RIGHT },
            { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BUTTON_L },
            { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, BUTTON_R },
        };
        for (auto& m : map)
        {
            bool held = SDL_GameControllerGetButton(m_controller, m.sdl) != 0;
            if (held && !m_buttonHeld[m.local])
                m_buttonJustPressed[m.local] = true;
            m_buttonHeld[m.local] = held;
        }

        // Read SDL analog axes + triggers
        m_leftStickX = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        m_leftStickY = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        m_triggerL = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
        m_triggerR = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    }

    // Update L2/R2 trigger buttons (axes → digital)
    bool triggerL = m_triggerL > 0.5f;
    bool triggerR = m_triggerR > 0.5f;
    if (triggerL && !m_buttonHeld[BUTTON_L2]) { m_buttonHeld[BUTTON_L2] = true; m_buttonJustPressed[BUTTON_L2] = true; }
    else if (!triggerL && m_buttonHeld[BUTTON_L2]) m_buttonHeld[BUTTON_L2] = false;
    if (triggerR && !m_buttonHeld[BUTTON_R2]) { m_buttonHeld[BUTTON_R2] = true; m_buttonJustPressed[BUTTON_R2] = true; }
    else if (!triggerR && m_buttonHeld[BUTTON_R2]) m_buttonHeld[BUTTON_R2] = false;

    // UWP Gamepad API: works on Xbox where SDL joystick API is unavailable
    // only used when SDL controller is not connected
    if (!m_controller)
        PollUwpGamepad();
}

void SdlInput::SetKeyboardButton(int btn, bool held)
{
    if (btn >= 0 && btn < MAX_BUTTONS)
    {
        m_buttonHeld[btn] = held;
        if (held)
        {
            m_buttonJustPressed[btn] = true;
            sprintf_s(m_lastEventStr, "KB:btn%d %s", btn, "DOWN");
        }
        else
        {
            sprintf_s(m_lastEventStr, "KB:btn%d %s", btn, "UP");
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
            OutputDebugStringA("UWP Gamepad connected\n");
        }
    }

    if (m_uwpGamepad)
    {
        auto reading = m_uwpGamepad->GetCurrentReading();

        m_leftStickX = reading.LeftThumbstickX;
        m_leftStickY = reading.LeftThumbstickY;
        m_triggerL = reading.LeftTrigger;
        m_triggerR = reading.RightTrigger;

        struct { GamepadButtons flag; int btn; const char* name; } map[] = {
            { GamepadButtons::A, BUTTON_A, "A" },
            { GamepadButtons::B, BUTTON_B, "B" },
            { GamepadButtons::X, BUTTON_X, "X" },
            { GamepadButtons::Y, BUTTON_Y, "Y" },
            { GamepadButtons::LeftThumbstick, BUTTON_L3, "L3" },
            { GamepadButtons::RightThumbstick, BUTTON_R3, "R3" },
            { GamepadButtons::View, BUTTON_SELECT, "Select" },
            { GamepadButtons::Menu, BUTTON_START, "Start" },
            { GamepadButtons::LeftShoulder, BUTTON_L, "L" },
            { GamepadButtons::RightShoulder, BUTTON_R, "R" },
            { GamepadButtons::DPadUp, BUTTON_DPAD_UP, "DPadUp" },
            { GamepadButtons::DPadDown, BUTTON_DPAD_DOWN, "DPadDown" },
            { GamepadButtons::DPadLeft, BUTTON_DPAD_LEFT, "DPadLeft" },
            { GamepadButtons::DPadRight, BUTTON_DPAD_RIGHT, "DPadRight" },
        };

        for (auto& m : map)
        {
            bool held = (reading.Buttons & m.flag) != GamepadButtons::None;
            if (held && !m_buttonHeld[m.btn])
            {
                m_buttonHeld[m.btn] = true;
                m_buttonJustPressed[m.btn] = true;
                sprintf_s(m_lastEventStr, "UWP:%s DOWN", m.name);
            }
            else if (!held && m_buttonHeld[m.btn])
            {
                m_buttonHeld[m.btn] = false;
                sprintf_s(m_lastEventStr, "UWP:%s UP", m.name);
            }
        }
    }
    else
    {
        m_leftStickX = 0.0f;
        m_leftStickY = 0.0f;
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


