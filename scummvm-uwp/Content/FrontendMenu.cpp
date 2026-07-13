#include "pch.h"
#include "FrontendMenu.h"
#include "SettingsManager.h"
#include "RetroCore.h"
#include <wincodec.h>
#include <sysinfoapi.h>
#include <memoryapi.h>
#include <windows.h>
#include <intrin.h>
#include <Windows.Security.ExchangeActiveSyncProvisioning.h>
#include <Windows.System.Profile.h>
#include <Windows.ApplicationModel.h>
#include <cmath>

using namespace dosbox_uwp;
using namespace Microsoft::WRL;
using namespace Windows::Security::ExchangeActiveSyncProvisioning;
using namespace Windows::System::Profile;
using namespace Windows::ApplicationModel;

static std::wstring TrimStr(std::wstring s)
{
    auto end = s.find_last_not_of(L' ');
    if (end != std::wstring::npos) s.erase(end + 1);
    auto start = s.find_first_not_of(L' ');
    if (start != std::wstring::npos) s.erase(0, start);
    return s;
}

static std::wstring GetCpuBrand()
{
#ifdef _M_AMD64
    int CPUInfo[4] = { -1 };
    __cpuid(CPUInfo, 0x80000000);
    unsigned int nExIds = CPUInfo[0];
    if (nExIds >= 0x80000004)
    {
        char brand[0x40] = { 0 };
        for (unsigned int i = 0x80000002; i <= 0x80000004; i++)
        {
            __cpuid(CPUInfo, i);
            memcpy(brand + (i - 0x80000002) * 16, CPUInfo, sizeof(CPUInfo));
        }
        int wlen = MultiByteToWideChar(CP_UTF8, 0, brand, -1, nullptr, 0);
        if (wlen > 0)
        {
            std::wstring result;
            result.resize(wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, brand, -1, &result[0], wlen);
            return TrimStr(result);
        }
    }
#endif
    return L"Unknown CPU";
}

static std::wstring GetMemoryStr()
{
    MEMORYSTATUSEX memInfo = { sizeof(memInfo) };
    if (GlobalMemoryStatusEx(&memInfo))
    {
        double mb = (double)memInfo.ullTotalPhys / (1024.0 * 1024.0);
        wchar_t buf[32];
        swprintf_s(buf, L"Memory: %.0f MB OK", mb);
        return buf;
    }
    return L"Memory: Unknown";
}

static std::wstring GetConsoleModel()
{
    try
    {
        auto deviceInfo = ref new EasClientDeviceInformation();
        std::wstring family(AnalyticsInfo::VersionInfo->DeviceFamily->Data());
        std::wstring model(deviceInfo->SystemProductName->Data());
        if (family == L"Windows.Xbox")
            return model.empty() ? L"Xbox" : model;
    }
    catch (...) {}
    return L"";
}

const wchar_t* const FrontendMenu::s_easterEggs[] = {
    // Portal / GLaDOS
    L"The cake is a lie. Also, the boot sector.",
    L"This was a triumph. I'm making a note here: huge success.",
    L"I'm not even angry. I'm being so sincere right now.",
    L"Android Hell is a real place where you will be sent.",
    L"Who else but me could make a cake this good?",
    // 2001: A Space Odyssey / HAL
    L"I'm sorry, Dave. I'm afraid I can't boot that.",
    L"Daisy, Daisy, give me your answer do...",
    L"I am putting myself to the fullest possible use.",
    // Project Hail Mary
    L"I penetrate the outer cell membrane with a nanotube.",
    L"Science isn't about why. It's about why not.",
    L"Good good good! Question question question!",
    L"The rock is my buddy. We share a brain cell.",
    // StarCraft
    L"My life for Aiur... and also for RAM.",
    L"You require more Vespene gas to continue booting.",
    L"Additional pylons needed.",
    L"En Taro Adun! Also, more conventional memory.",
    L"Carrier has arrived. The floppy drive has not.",
    L"Not enough minerals.",
    // Warcraft
    L"Work work. Also, defrag the hard drive.",
    L"Zug zug. Time to load Doom.",
    L"I smell lemmmings... I mean, executables.",
    L"Ready to work... on your save files.",
    L"The humans are coming. Hide the good games.",
    L"Someone actually woke me up for this?",
    // Ghostbusters
    L"There is no DOS, only Zuul.",
    L"Back off, man. I'm a BIOS.",
    L"Don't cross the streams. Especially not COM1 and COM2.",
    L"100% pure Hungarian notation.",
    // Hitchhiker's Guide
    L"42 - The answer to memory test.",
    L"Don't Panic. Your 640K is fine.",
    L"So long, and thanks for all the sectors.",
    L"The BIOS is mostly harmless.",
    L"If you've done six impossible things before breakfast...",
    // Matrix
    L"There is no reset button. Only the blue pill.",
    L"I know DOS. I know because I must.",
    L"Free your mind. Forget about conventional memory.",
    L"After this, there is no turning back.",
    L"I'm going to show them a world without rules.",
    // Terminator
    L"I'll be back... after POST.",
    L"Hasta la vista, Windows 3.1.",
    L"Your boot process is over. Mine is about to begin.",
    L"Come with me if you want to load.",
    // Alien
    L"In space, no one can hear your floppy seek.",
    L"Last survivor of the Conventional Memory.",
    L"I can't lie about your chances of booting. But I encourage you to try.",
    L"Get away from her, you BIT... 16 bus.",
    // Star Wars
    L"Help me, Obi-Wan. You're my only BIOS.",
    L"I've got a bad feeling about this boot sequence.",
    L"Do or do not. There is no try... to access extended memory.",
    L"That's no moon. That's a 5.25\" floppy.",
    L"The Force is strong with this processor.",
    // Star Trek
    L"Live long and prosper... on 640K.",
    L"Resistance is futile. Your data WILL be archived.",
    L"Make it so. Boot sequence 7Alpha.",
    L"Space: the final frontier. These are the ROMs of the BIOS.",
    L"I have been, and always shall be, your boot loader.",
    // Blade Runner
    L"Like tears in rain. Time to load...",
    L"I've seen things you people wouldn't believe. Attack ships on fire off the shoulder of Orion.",
    L"All those moments will be lost in time, like floppy seeks.",
    // Tron
    L"I fight for the users. And their save files.",
    L"The Grid. A digital frontier. I tried to picture clusters of data as I moved along.",
    L"End of line. Reboot required.",
    // WarGames
    L"A strange game. The only winning move is not to load.",
    L"Shall we play a game? How about Global Thermonuclear Doom?",
    L"Computer, start DOS mode. Nice war game.",
    // Sneakers
    L"My voice is my password. Verify me.",
    L"The world isn't run by weapons anymore. It's run by ones and zeros.",
    // The Princess Bride
    L"Have fun storming the BIOS!",
    L"Life is pain, Highness. Anyone who says differently is selling something.",
    L"Inconceivable! You don't have enough RAM?",
    L"You keep using that 486. I don't think it means what you think it means.",
    // Spaceballs
    L"Comb the desert! And defrag your C: drive.",
    L"We've been jammed. It's Raspberry. There's only one man who would dare give me a Raspberry.",
    L"When you're right, you're right. And you? You're right.",
    // Back to the Future
    L"Roads? Where we're going, we don't need roads. We need DOS.",
    L"1.21 gigahertz? This processor needs 1.21 gigawatts!",
    L"Your BIOS is heavy. Is there a problem with your power supply?",
    // Firefly
    L"I'm a leaf on the wind. Watch me defrag.",
    L"Curse your sudden but inevitable reboot.",
    L"Every time something goes wrong, I blame Inara's floppy drive.",
    L"Well. Here I am.",
    // Battlestar Galactica
    L"So say we all: reboot and load again.",
    L"Frak. The Cylons found our BIOS.",
    L"All this has happened before, and all of it will happen again.",
    L"It's in the machine! Right behind the MBR.",
    // Doctor Who
    L"I'm the Doctor. I've lived... through every reboot.",
    L"Exterminate! Exterminate! ...just kidding. Loading.",
    L"The universe is big. It's vast and complicated and ridiculous. And so is your FAT table.",
    L"Wibbly wobbly, timey wimey... stuff.",
    // Lord of the Rings
    L"One does not simply load Doom into Mordor... or 640K.",
    L"I am no BIOS. I am a hobbit of the Shire.",
    L"You shall not pass... the memory check.",
    L"My precious... my precious 640K of conventional memory.",
    L"Even the smallest BIOS can change the course of the future.",
    // Game of Thrones
    L"Winter is coming... for your hard drive.",
    L"A DOS box pays its debts. Especially in kilobytes.",
    L"I drink and I know things. About your boot sequence.",
    L"The night is dark and full of bit errors.",
    L"Chaos isn't a pit. Chaos is a 512-byte sector.",
    // Monty Python
    L"And now for something completely different: a reboot.",
    L"I fart in your general direction. Your mother was a floppy drive.",
    L"This is an ex-BIOS. It has ceased to be.",
    L"Nobody expects the Spanish Inquisition... of POST tests.",
    L"What is the airspeed velocity of an unladen swallow? ...bytes per second?",
    // Alien franchise
    L"Game over, man! Game over! ...just kidding. Loading.",
    // Terminator 2
    L"Hasta la vista, extended memory.",
    // Office Space
    L"I have people skills! I can read your floppy disk!",
    L"PC load letter? What the f--- does that even mean?",
    L"Did you get that memo about the BIOS update?",
    // Hackers
    L"I'm in. The BIOS is wide open.",
    L"Cookie cookie cookie starts with C.",
    // Judge Dredd
    L"I am the BIOS. I am the law.",
    L"Case closed. Boot complete.",
    // Metal Gear Solid
    L"A weapon to surpass Metal Gear... or at least a faster CPU.",
    L"It's not about changing the world. It's about doing our best to boot.",
    // DOOM
    L"Rip and tear. Until it is done... loading DOOM.",
    L"The only thing they fear is you. Also, low memory.",
    // Quake
    L"There is only one key: the boot key.",
    // Unreal
    L"Welcome to DOS... beautiful and mysterious.",
    // Half-Life
    L"The right BIOS in the wrong place can make all the difference in the world.",
    L"Wake up, Mr. Freeman. Wake up and smell the boot sector.",
    // Portal 2
    L"Goodbye. And again, good luck with the boot.",
    L"Testing. Testing. And, oh, cake.",
    // Cyberpunk
    L"Wake up, samurai. You have a BIOS to load.",
    // Minecraft
    L"I used to be an adventurer. Then I took a seek error to the knee.",
    // Dark Souls
    L"You died. But you have 640K of souls remaining.",
    L"Bonfire detected. Restoring BIOS settings.",
    // Bioshock
    L"Would you kindly reboot?",
    L"A BIOS chooses. A slave obeys.",
    // Mass Effect
    L"I'm Commander Shepard, and this is my favorite BIOS on the Citadel.",
    L"Wrex. Grunt. ...Load.",
    // Fallout
    L"War. War never changes. But your boot sector does.",
    L"Vault-Tec calling. Your BIOS is ready.",
    L"Please stand by. Loading in progress.",
    // Skyrim
    L"I used to be a DOS user like you, then I took a seek error to the knee.",
    // Final Fantasy
    L"This is not the end. This is not even the beginning of the end. But it is perhaps, the end of the beginning... of boot.",
    // Misc
    L"Have you tried turning it off and on again?",
    L"A watched BIOS never boots.",
    L"To err is human. To really foul up a hard drive requires a BIOS.",
    L"Programming today is a race between software engineers striving to build bigger and better idiot-proof programs, and the Universe trying to produce bigger and better idiots.",
    L"There are only 10 types of people: those who understand binary, and those who don't.",
    L"I'm sorry, I can't do that, Dave. Just kidding. Booting.",
    L"You are in a twisty little maze of passages, all alike. Also, the boot sector is corrupted.",
};
const int FrontendMenu::s_easterEggCount = sizeof(FrontendMenu::s_easterEggs) / sizeof(FrontendMenu::s_easterEggs[0]);

FrontendMenu::FrontendMenu()
{
    std::wstring cpuLine = L"CPU: " + GetCpuBrand();
    std::wstring memLine = GetMemoryStr();

    m_biosLines.push_back(L"DOSBox Pure Unleashed BIOS (UWP Edition)");
    m_biosLines.push_back(L"Copyright (C) 2000-2026 Unleashed Project");
    m_biosLines.push_back(L"");
    m_biosLines.push_back(cpuLine);
    m_biosLines.push_back(memLine);

    std::wstring consoleModel = GetConsoleModel();
    if (!consoleModel.empty())
        m_biosLines.push_back(L"System: " + consoleModel);

    m_biosLines.push_back(L"");
    m_biosLines.push_back(L"Award Plug and Play BIOS Extension v1.0A");
    m_biosLines.push_back(L"Copyright (C) 1986 Award Software, Inc.");
    m_biosLines.push_back(L"");

    srand((unsigned)GetTickCount64());
    m_easterEggIndex = rand() % s_easterEggCount;
    m_biosLines.push_back(s_easterEggs[m_easterEggIndex]);
    m_biosLines.push_back(L"");
    m_biosLines.push_back(L"F10 / L3 = Menu     L = Open Game     ESC = Exit");

    // Parse total memory MB for count-up animation
    {
        MEMORYSTATUSEX memInfo = { sizeof(memInfo) };
        if (GlobalMemoryStatusEx(&memInfo))
            m_memoryTotalMB = (int)(memInfo.ullTotalPhys / (1024ULL * 1024ULL));
        else
            m_memoryTotalMB = 64;
    }

    try
    {
        auto pkg = Package::Current;
        auto ver = pkg->Id->Version;
        wchar_t buf[64];
        swprintf_s(buf, L"v%hu.%hu.%hu.%hu", ver.Major, ver.Minor, ver.Build, ver.Revision);
        m_versionStr = buf;
    }
    catch (...) { m_versionStr = L"v?.?.?.?"; }

    m_animStartTick = GetTickCount64();

    BuildMenuTree();
    m_visible = true;
}

void FrontendMenu::Show()
{
    m_visible = true;
}

void FrontendMenu::OnEasterEgg()
{
    m_easterEggIndex = (m_easterEggIndex + 1) % s_easterEggCount;
    // Easter egg line is always 2 lines before the last (empty + footer are last two)
    if (m_biosLines.size() >= 3)
        m_biosLines[m_biosLines.size() - 3] = s_easterEggs[m_easterEggIndex];
}

void FrontendMenu::SetCoreLoaded(bool loaded)
{
    if (m_coreLoadedPrev == loaded) return;
    m_coreLoadedPrev = loaded;
    RebuildItems();
}

void FrontendMenu::BuildMenuTree()
{
    m_mainItems =
    {
        { "Load File",           MenuAction::OPEN_FILE },
        { "",                    MenuAction::NONE },
        { "Settings",            MenuAction::SETTINGS, {}, {}, {}, 0, true },
        { "History",             MenuAction::OPEN_HISTORY, {}, {}, {}, 0, true },
        { "",                    MenuAction::NONE },
        { "About",               MenuAction::ABOUT },
        { "Exit",                MenuAction::EXIT },
    };

    m_settingsItems =
    {
        { "General",             MenuAction::GENERAL },
        { "Input",               MenuAction::INPUT },
        { "Performance",         MenuAction::PERFORMANCE },
        { "Video",               MenuAction::VIDEO },
        { "System",              MenuAction::SYSTEM },
        { "Audio",               MenuAction::AUDIO },
        { "",                    MenuAction::NONE },
        { "Reset All Settings",  MenuAction::RESET_ALL_SETTINGS },
        { "",                    MenuAction::NONE },
        { "Back",                MenuAction::BACK },
    };

    // Lambda: find current value index by searching core values
    auto findVal = [](const std::string& key, const std::vector<std::string>& coreVals, const char* def) -> int {
        std::string cur = SettingsManager::GetOption(key.c_str(), def);
        for (int i = 0; i < (int)coreVals.size(); i++)
            if (coreVals[i] == cur) return i;
        return 0;
    };
    // Lambda: find current value index by searching display values (frontend-only options)
    auto findValDisplay = [](const std::string& key, const std::vector<std::string>& vals, const char* def) -> int {
        std::string cur = SettingsManager::GetOption(key.c_str(), def);
        for (int i = 0; i < (int)vals.size(); i++)
            if (vals[i] == cur) return i;
        return 0;
    };
    // Lambda: get value to send to core (coreValues when set, values otherwise)
    auto getCoreValue = [](const MenuItem& item) -> const std::string& {
        return (!item.coreValues.empty()) ? item.coreValues[item.currentValue] : item.values[item.currentValue];
    };
    // Lambda: format float as string without trailing zeros (0.2->"0.2", 2.0->"2.0")
    auto fmtFloat = [](double v) -> std::string {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", v);
        std::string s(buf);
        size_t dot = s.find('.');
        if (dot != std::string::npos) {
            size_t last = s.find_last_not_of('0');
            if (last == dot) last++; // keep at least one digit after decimal
            s.erase(last + 1);
        }
        return s;
    };

    // === General ===
    {
        std::vector<std::string> vsyncVals = { "Off", "On" };
        std::vector<std::string> scalerVals = { "Nearest", "Bilinear" };
        std::vector<std::string> fpsVals = { "Off", "On (10 FPS)", "On (15 FPS)", "On (20 FPS)", "On (30 FPS)", "On (35 FPS)", "On (50 FPS)", "On (60 FPS)", "On (70 FPS)", "On (90 FPS)", "On (120 FPS)", "On (144 FPS)", "On (240 FPS)", "On (360 FPS)" };
        std::vector<std::string> fpsCore = { "false", "10", "15", "20", "30", "35", "50", "true", "70", "90", "120", "144", "240", "360" };
        std::vector<std::string> saveVals = { "Enable save states", "Enable save states with rewind", "Disabled" };
        std::vector<std::string> saveCore = { "on", "rewind", "disabled" };
        std::vector<std::string> menuVals = { "Show at start, show again after game ends", "Show at start, exit 5s after", "Show at start, exit 3s after", "Show at start, exit immediately", "Always show menu" };
        std::vector<std::string> menuCore = { "99", "5", "3", "0", "-1" };
        std::vector<std::string> transVals;
        for (int i = 10; i <= 100; i += 10)
            transVals.push_back(std::to_string(i));

        m_generalItems = {
            { "VSync",               MenuAction::TOGGLE_VALUE, {}, vsyncVals, {}, findValDisplay("frontend_vsync", vsyncVals, "On"), true, "frontend_vsync" },
            { "Scaler",              MenuAction::TOGGLE_VALUE, {}, scalerVals, {}, findValDisplay("frontend_scaler", scalerVals, "Bilinear"), true, "frontend_scaler" },
            { "",                    MenuAction::NONE },
            { "Force Output FPS",    MenuAction::TOGGLE_VALUE, {}, fpsVals, fpsCore, findVal("dosbox_pure_force60fps", fpsCore, "false"), true, "dosbox_pure_force60fps" },
            { "Save States Support", MenuAction::TOGGLE_VALUE, {}, saveVals, saveCore, findVal("dosbox_pure_savestate", saveCore, "on"), false, "dosbox_pure_savestate" },
            { "Start Menu",          MenuAction::TOGGLE_VALUE, {}, menuVals, menuCore, findVal("dosbox_pure_menu_time", menuCore, "99"), true, "dosbox_pure_menu_time" },
            { "",                    MenuAction::NONE },
            { "Reset to Defaults",   MenuAction::RESET_DEFAULTS },
            { "",                    MenuAction::NONE },
            { "Back",                MenuAction::BACK },
        };
    }

    // === Input ===
    {
        std::vector<std::string> l3Vals = { "On (Default to Menu)", "On (Default to On-Screen Keyboard)", "On (Only OSK While in Game)", "Off" };
        std::vector<std::string> l3Core = { "true", "keyboard", "onlyosk", "false" };
        std::vector<std::string> mouseVals = { "Auto (default)", "Virtual mouse movement", "Direct controlled mouse (not supported by all games)", "Touchpad mode (see description, best for touch screens)", "Off (ignore mouse inputs)" };
        std::vector<std::string> mouseCore = { "true", "virtual", "direct", "pad", "false" };
        std::vector<std::string> wheelVals = { "Left-Bracket/Right-Bracket", "Comma/Period", "Page-Up/Page-Down", "Home/End", "Delete/Page-Down", "Minus/Equals", "Semicolon/Quote", "Numpad Minus/Plus", "Numpad Divide/Multiply", "Up/Down", "Left/Right", "Q/E", "Disable" };
        std::vector<std::string> wheelCore = { "67/68", "72/71", "79/82", "78/81", "80/82", "64/65", "69/70", "99/100", "97/98", "84/85", "83/86", "11/13", "none" };
        std::vector<std::string> speedVals, speedCore;
        for (int i = 20; i <= 200; i += 5) {
            speedVals.push_back(std::to_string(i) + "%");
            speedCore.push_back(fmtFloat(i / 100.0));
        }
        for (int i = 22; i <= 50; i += 2) {
            speedVals.push_back(std::to_string(i * 10) + "%");
            speedCore.push_back(fmtFloat(i / 10.0));
        }
        std::vector<std::string> mapVals = { "On (default)", "Enable with notification on game detection", "Off" };
        std::vector<std::string> mapCore = { "true", "notify", "false" };
        std::vector<std::string> kbVals = { "US (default)", "UK", "Belgium", "Brazil", "Croatia", "Czech Republic", "Denmark", "Finland", "France", "Germany", "Greece", "Hungary", "Iceland", "Italy", "Netherlands", "Norway", "Poland", "Portugal", "Russia", "Slovakia", "Slovenia", "Spain", "Sweden", "Switzerland (German)", "Switzerland (French)", "Turkey" };
        std::vector<std::string> kbCore = { "us", "uk", "be", "br", "hr", "cz243", "dk", "su", "fr", "gr", "gk", "hu", "is161", "it", "nl", "no", "pl", "po", "ru", "sk", "si", "sp", "sv", "sg", "sf", "tr" };
        std::vector<std::string> dzVals, dzCore;
        for (int i = 0; i <= 40; i += 5) {
            dzVals.push_back(std::to_string(i) + "%");
            dzCore.push_back(std::to_string(i));
        }
        std::vector<std::string> timedVals = { "On (default)", "Off" };
        std::vector<std::string> timedCore = { "true", "false" };
        std::vector<std::string> awVals = { "Right Stick, D-Pad, Mouse (Default)", "Right Stick, D-Pad", "Right Stick, Mouse", "Right Stick", "Both Sticks, D-Pad, Mouse", "Both Sticks, D-Pad", "Both Sticks, Mouse", "Both Sticks", "Left Stick, D-Pad, Mouse", "Left Stick, D-Pad", "Left Stick, Mouse", "Left Stick", "D-Pad, Mouse", "D-Pad", "Mouse" };
        std::vector<std::string> awCore = { "14", "6", "10", "2", "15", "7", "11", "3", "13", "5", "9", "1", "12", "4", "8" };

        m_inputItems = {
            { "L3 Button to Show Menu",     MenuAction::TOGGLE_VALUE, {}, l3Vals, l3Core, findVal("dosbox_pure_on_screen_keyboard", l3Core, "true"), true, "dosbox_pure_on_screen_keyboard" },
            { "Mouse Input Mode",            MenuAction::TOGGLE_VALUE, {}, mouseVals, mouseCore, findVal("dosbox_pure_mouse_input", mouseCore, "true"), true, "dosbox_pure_mouse_input" },
            { "Bind Mouse Wheel To Key",     MenuAction::TOGGLE_VALUE, {}, wheelVals, wheelCore, findVal("dosbox_pure_mouse_wheel", wheelCore, "67/68"), true, "dosbox_pure_mouse_wheel" },
            { "Mouse Sensitivity",           MenuAction::TOGGLE_VALUE, {}, speedVals, speedCore, findVal("dosbox_pure_mouse_speed_factor", speedCore, "1.0"), true, "dosbox_pure_mouse_speed_factor" },
            { "Horizontal Mouse Sensitivity", MenuAction::TOGGLE_VALUE, {}, speedVals, speedCore, findVal("dosbox_pure_mouse_speed_factor_x", speedCore, "1.0"), true, "dosbox_pure_mouse_speed_factor_x" },
            { "",                            MenuAction::NONE },
            { "Automatic Game Pad Mappings", MenuAction::TOGGLE_VALUE, {}, mapVals, mapCore, findVal("dosbox_pure_auto_mapping", mapCore, "true"), true, "dosbox_pure_auto_mapping" },
            { "Keyboard Layout",             MenuAction::TOGGLE_VALUE, {}, kbVals, kbCore, findVal("dosbox_pure_keyboard_layout", kbCore, "us"), true, "dosbox_pure_keyboard_layout" },
            { "Joystick Analog Deadzone",    MenuAction::TOGGLE_VALUE, {}, dzVals, dzCore, findVal("dosbox_pure_joystick_analog_deadzone", dzCore, "15"), true, "dosbox_pure_joystick_analog_deadzone" },
            { "Joystick Timed Intervals",    MenuAction::TOGGLE_VALUE, {}, timedVals, timedCore, findVal("dosbox_pure_joystick_timed", timedCore, "true"), true, "dosbox_pure_joystick_timed" },
            { "Action Wheel Inputs",         MenuAction::TOGGLE_VALUE, {}, awVals, awCore, findVal("dosbox_pure_actionwheel_inputs", awCore, "14"), true, "dosbox_pure_actionwheel_inputs" },
            { "",                            MenuAction::NONE },
            { "Reset to Defaults",           MenuAction::RESET_DEFAULTS },
            { "",                            MenuAction::NONE },
            { "Back",                        MenuAction::BACK },
        };
    }

    // === Performance ===
    {
        std::vector<std::string> cycVals = { "AUTO - DOSBox will try to detect performance needs (default)", "MAX - Emulate as many instructions as possible", "8086/8088, 4.77 MHz from 1980 (315 cps)", "286, 6 MHz from 1982 (1320 cps)", "286, 12.5 MHz from 1985 (2750 cps)", "386, 20 MHz from 1987 (4720 cps)", "386DX, 33 MHz from 1989 (7800 cps)", "486DX, 33 MHz from 1990 (13400 cps)", "486DX2, 66 MHz from 1992 (26800 cps)", "Pentium, 100 MHz from 1995 (77000 cps)", "Pentium II, 300 MHz from 1997 (200000 cps)", "Pentium III, 600 MHz from 1999 (500000 cps)", "AMD Athlon, 1.2 GHz from 2000 (1000000 cps)" };
        std::vector<std::string> cycCore = { "auto", "max", "315", "1320", "2750", "4720", "7800", "13400", "26800", "77000", "200000", "500000", "1000000" };
        std::vector<std::string> maxVals = { "Unlimited", "8086/8088, 4.77 MHz from 1980 (315 cps)", "286, 6 MHz from 1982 (1320 cps)", "286, 12.5 MHz from 1985 (2750 cps)", "386, 20 MHz from 1987 (4720 cps)", "386DX, 33 MHz from 1989 (7800 cps)", "486DX, 33 MHz from 1990 (13400 cps)", "486DX2, 66 MHz from 1992 (26800 cps)", "Pentium, 100 MHz from 1995 (77000 cps)", "Pentium II, 300 MHz from 1997 (200000 cps)", "Pentium III, 600 MHz from 1999 (500000 cps)", "AMD Athlon, 1.2 GHz from 2000 (1000000 cps)" };
        std::vector<std::string> maxCore = { "none", "315", "1320", "2750", "4720", "7800", "13400", "26800", "77000", "200000", "500000", "1000000" };
        std::vector<std::string> scaleVals, scaleCore;
        for (int i = 20; i <= 200; i += 5) {
            scaleVals.push_back(std::to_string(i) + "%");
            scaleCore.push_back(fmtFloat(i / 100.0));
        }
        std::vector<std::string> limitVals, limitCore;
        for (int i = 50; i <= 100; i++) {
            limitVals.push_back(std::to_string(i) + "%");
            limitCore.push_back(fmtFloat(i / 100.0));
        }
        std::vector<std::string> perfVals = { "Disabled", "Simple", "Detailed information" };
        std::vector<std::string> perfCore = { "none", "simple", "detailed" };

        m_performanceItems = {
            { "Emulated Performance",         MenuAction::TOGGLE_VALUE, {}, cycVals, cycCore, findVal("dosbox_pure_cycles", cycCore, "auto"), true, "dosbox_pure_cycles" },
            { "Maximum Emulated Performance",  MenuAction::TOGGLE_VALUE, {}, maxVals, maxCore, findVal("dosbox_pure_cycles_max", maxCore, "none"), true, "dosbox_pure_cycles_max" },
            { "Performance Scale",             MenuAction::TOGGLE_VALUE, {}, scaleVals, scaleCore, findVal("dosbox_pure_cycles_scale", scaleCore, "1.0"), true, "dosbox_pure_cycles_scale" },
            { "Limit CPU Usage",               MenuAction::TOGGLE_VALUE, {}, limitVals, limitCore, findVal("dosbox_pure_cycle_limit", limitCore, "1.0"), true, "dosbox_pure_cycle_limit" },
            { "Show Performance Statistics",   MenuAction::TOGGLE_VALUE, {}, perfVals, perfCore, findVal("dosbox_pure_perfstats", perfCore, "none"), true, "dosbox_pure_perfstats" },
            { "",                              MenuAction::NONE },
            { "Reset to Defaults",             MenuAction::RESET_DEFAULTS },
            { "",                              MenuAction::NONE },
            { "Back",                          MenuAction::BACK },
        };
    }

    // === Video ===
    {
        std::vector<std::string> machineVals = { "SVGA (Super Video Graphics Array) (default)", "VGA (Video Graphics Array)", "EGA (Enhanced Graphics Adapter)", "CGA (Color Graphics Adapter)", "Tandy (Tandy Graphics Adapter)", "Hercules (Hercules Graphics Card)", "PCjr" };
        std::vector<std::string> machineCore = { "svga", "vga", "ega", "cga", "tandy", "hercules", "pcjr" };
        std::vector<std::string> cgaVals = { "Early model, composite mode auto (default)", "Early model, composite mode on", "Early model, composite mode off", "Late model, composite mode auto", "Late model, composite mode on", "Late model, composite mode off" };
        std::vector<std::string> cgaCore = { "early_auto", "early_on", "early_off", "late_auto", "late_on", "late_off" };
        std::vector<std::string> hercVals = { "Black & white (default)", "Black & amber", "Black & green" };
        std::vector<std::string> hercCore = { "white", "amber", "green" };
        std::vector<std::string> svgaVals = { "S3 Trio64 (default)", "S3 Trio64 no-line buffer hack (reduces flickering in some games)", "S3 Trio64 VESA 1.3", "Tseng Labs ET3000", "Tseng Labs ET4000", "Paradise PVGA1A" };
        std::vector<std::string> svgaCore = { "svga_s3", "vesa_nolfb", "vesa_oldvbe", "svga_et3000", "svga_et4000", "svga_paradise" };
        std::vector<std::string> memVals = { "512KB", "1MB", "2MB (default)", "3MB", "4MB", "8MB (not always recognized)" };
        std::vector<std::string> memCore = { "0", "1", "2", "3", "4", "8" };
        std::vector<std::string> voodooVals = { "Enabled - 8MB memory (default)", "Enabled - 12MB memory, Dual Texture", "Enabled - 4MB memory, Low Resolution Only", "Disabled" };
        std::vector<std::string> voodooCore = { "8mb", "12mb", "4mb", "off" };
        std::vector<std::string> perfVals = { "Auto (default)", "Hardware OpenGL", "Software Multi Threaded", "Software Multi Threaded, low quality", "Software Single Threaded, low quality", "Software Single Threaded" };
        std::vector<std::string> perfCore = { "auto", "4", "1", "3", "2", "0" };
        std::vector<std::string> scaleVals, scaleCore;
        for (int i = 1; i <= 8; i++) {
            scaleVals.push_back(std::to_string(i) + "x");
            scaleCore.push_back(std::to_string(i));
        }
        std::vector<std::string> gamVals, gamCore;
        for (int i = -10; i <= -1; i++) {
            gamVals.push_back(std::to_string(i));
            gamCore.push_back(std::to_string(i));
        }
        gamVals.push_back("None"); gamCore.push_back("0");
        for (int i = 1; i <= 10; i++) {
            gamVals.push_back("+" + std::to_string(i));
            gamCore.push_back(std::to_string(i));
        }
        gamVals.push_back("Disable Gamma Correction"); gamCore.push_back("999");
        std::vector<std::string> aspVals = { "Off (default)", "On (single-scan)", "On (double-scan when applicable)", "Padded to 4:3 (single-scan)", "Padded to 4:3 (double-scan when applicable)" };
        std::vector<std::string> aspCore = { "false", "true", "doublescan", "padded", "padded-doublescan" };
        std::vector<std::string> osVals = { "Off (default)", "Small", "Medium", "Large" };
        std::vector<std::string> osCore = { "0", "1", "2", "3" };

        m_videoItems = {
            { "Emulated Graphics Chip",          MenuAction::TOGGLE_VALUE, {}, machineVals, machineCore, findVal("dosbox_pure_machine", machineCore, "svga"), true, "dosbox_pure_machine" },
            { "CGA Mode",                        MenuAction::TOGGLE_VALUE, {}, cgaVals, cgaCore, findVal("dosbox_pure_cga", cgaCore, "early_auto"), true, "dosbox_pure_cga" },
            { "Hercules Color Mode",             MenuAction::TOGGLE_VALUE, {}, hercVals, hercCore, findVal("dosbox_pure_hercules", hercCore, "white"), true, "dosbox_pure_hercules" },
            { "SVGA Mode",                       MenuAction::TOGGLE_VALUE, {}, svgaVals, svgaCore, findVal("dosbox_pure_svga", svgaCore, "svga_s3"), true, "dosbox_pure_svga" },
            { "SVGA Memory",                     MenuAction::TOGGLE_VALUE, {}, memVals, memCore, findVal("dosbox_pure_svgamem", memCore, "2"), true, "dosbox_pure_svgamem" },
            { "",                                MenuAction::NONE },
            { "3dfx Voodoo Emulation",           MenuAction::TOGGLE_VALUE, {}, voodooVals, voodooCore, findVal("dosbox_pure_voodoo", voodooCore, "8mb"), false, "dosbox_pure_voodoo" },
            { "3dfx Voodoo Performance",         MenuAction::TOGGLE_VALUE, {}, perfVals, perfCore, findVal("dosbox_pure_voodoo_perf", perfCore, "auto"), false, "dosbox_pure_voodoo_perf" },
            { "3dfx Voodoo OpenGL Scaling",      MenuAction::TOGGLE_VALUE, {}, scaleVals, scaleCore, findVal("dosbox_pure_voodoo_scale", scaleCore, "1"), false, "dosbox_pure_voodoo_scale" },
            { "3dfx Voodoo Gamma Correction",    MenuAction::TOGGLE_VALUE, {}, gamVals, gamCore, findVal("dosbox_pure_voodoo_gamma", gamCore, "-2"), false, "dosbox_pure_voodoo_gamma" },
            { "",                                MenuAction::NONE },
            { "Aspect Ratio Correction",         MenuAction::TOGGLE_VALUE, {}, aspVals, aspCore, findVal("dosbox_pure_aspect_correction", aspCore, "false"), true, "dosbox_pure_aspect_correction" },
            { "Overscan Border Size",            MenuAction::TOGGLE_VALUE, {}, osVals, osCore, findVal("dosbox_pure_overscan", osCore, "0"), true, "dosbox_pure_overscan" },
            { "",                                MenuAction::NONE },
            { "Reset to Defaults",               MenuAction::RESET_DEFAULTS },
            { "",                                MenuAction::NONE },
            { "Back",                            MenuAction::BACK },
        };
    }

    // === System ===
    {
        std::vector<std::string> memVals = { "Disable extended memory (no EMS/XMS)", "4 MB", "8 MB", "16 MB (default)", "24 MB", "32 MB", "48 MB", "64 MB", "96 MB", "128 MB", "224 MB", "256 MB", "512 MB", "1024 MB" };
        std::vector<std::string> memCore = { "none", "4", "8", "16", "24", "32", "48", "64", "96", "128", "224", "256", "512", "1024" };
        std::vector<std::string> modemVals = { "Null Modem (Direct Serial)", "Dial-Up Modem (Hayes Standard)" };
        std::vector<std::string> modemCore = { "null", "dial" };
        std::vector<std::string> cpuVals = { "Auto - Mixed feature set with maximum performance and compatibility", "386 - 386 instruction with fast memory access", "386 (slow) - 386 instruction set with memory privilege checks", "386 (prefetch) - With prefetch queue emulation (only on 'auto' and 'normal' core)", "486 (slow) - 486 instruction set with memory privilege checks", "Pentium (slow) - 586 instruction set with memory privilege checks", "Pentium MMX (slow) - 586 instruction set with MMX extension" };
        std::vector<std::string> cpuCore = { "auto", "386", "386_slow", "386_prefetch", "486_slow", "pentium_slow", "pentium_mmx" };
        std::vector<std::string> coreVals = { "Auto - Real-mode games use normal, protected-mode games use dynamic", "Dynamic - Dynamic recompilation (fast, using dynrec implementation)", "Normal (interpreter)", "Simple (interpreter optimized for old real-mode games)" };
        std::vector<std::string> coreCore = { "auto", "dynamic", "normal", "simple" };
        std::vector<std::string> confVals = { "Disabled conf support (default)", "Try 'dosbox.conf' in the loaded content (ZIP or folder)", "Try '.conf' with same name as loaded content (next to ZIP or folder)" };
        std::vector<std::string> confCore = { "false", "inside", "outside" };
        std::vector<std::string> strictVals = { "Off", "On" };
        std::vector<std::string> strictCore = { "false", "true" };
        std::vector<std::string> ramVals = { "Keep (default)", "Discard", "Save Difference Per Content" };
        std::vector<std::string> ramCore = { "false", "true", "diff" };
        std::vector<std::string> freeVals = { "1GB (default)", "2GB", "4GB", "8GB", "Discard Changes to D:", "Disable D: Hard Disk (use only CD-ROM)" };
        std::vector<std::string> freeCore = { "1024", "2048", "4096", "8192", "discard", "hide" };
        std::vector<std::string> forceVals = { "Off (default)", "On" };
        std::vector<std::string> forceCore = { "false", "true" };

        m_systemItems = {
            { "Memory Size",              MenuAction::TOGGLE_VALUE, {}, memVals, memCore, findVal("dosbox_pure_memory_size", memCore, "16"), true, "dosbox_pure_memory_size" },
            { "Modem Type",               MenuAction::TOGGLE_VALUE, {}, modemVals, modemCore, findVal("dosbox_pure_modem", modemCore, "null"), true, "dosbox_pure_modem" },
            { "CPU Type",                 MenuAction::TOGGLE_VALUE, {}, cpuVals, cpuCore, findVal("dosbox_pure_cpu_type", cpuCore, "auto"), true, "dosbox_pure_cpu_type" },
            { "CPU Core",                 MenuAction::TOGGLE_VALUE, {}, coreVals, coreCore, findVal("dosbox_pure_cpu_core", coreCore, "auto"), true, "dosbox_pure_cpu_core" },
            { "",                         MenuAction::NONE },
            { "Loading of dosbox.conf",   MenuAction::TOGGLE_VALUE, {}, confVals, confCore, findVal("dosbox_pure_conf", confCore, "false"), true, "dosbox_pure_conf" },
            { "Use Strict Mode",          MenuAction::TOGGLE_VALUE, {}, strictVals, strictCore, findVal("dosbox_pure_strict_mode", strictCore, "false"), true, "dosbox_pure_strict_mode" },
            { "",                         MenuAction::NONE },
            { "OS Disk Modifications",    MenuAction::TOGGLE_VALUE, {}, ramVals, ramCore, findVal("dosbox_pure_bootos_ramdisk", ramCore, "false"), true, "dosbox_pure_bootos_ramdisk" },
            { "Free Space on D:",         MenuAction::TOGGLE_VALUE, {}, freeVals, freeCore, findVal("dosbox_pure_bootos_dfreespace", freeCore, "1024"), true, "dosbox_pure_bootos_dfreespace" },
            { "Force Normal Core in OS",  MenuAction::TOGGLE_VALUE, {}, forceVals, forceCore, findVal("dosbox_pure_bootos_forcenormal", forceCore, "false"), true, "dosbox_pure_bootos_forcenormal" },
            { "",                         MenuAction::NONE },
            { "Reset to Defaults",        MenuAction::RESET_DEFAULTS },
            { "",                         MenuAction::NONE },
            { "Back",                     MenuAction::BACK },
        };
    }

    // === Audio ===
    {
        std::vector<std::string> rateVals = { "48000", "44100", "32000", "22050", "16000", "11025", "8000", "49716" };
        std::vector<std::string> sbTypeVals = { "SoundBlaster 16 (default)", "SoundBlaster Pro 2", "SoundBlaster Pro", "SoundBlaster 2.0", "SoundBlaster 1.0", "GameBlaster", "None" };
        std::vector<std::string> sbTypeCore = { "sb16", "sbpro2", "sbpro1", "sb2", "sb1", "gb", "none" };
        std::vector<std::string> sbConfVals = { "Port 0x220, IRQ 7, 8-Bit DMA 1, 16-bit DMA 5", "Port 0x220, IRQ 5, 8-Bit DMA 1, 16-bit DMA 5", "Port 0x240, IRQ 7, 8-Bit DMA 1, 16-bit DMA 5", "Port 0x240, IRQ 7, 8-Bit DMA 3, 16-bit DMA 7", "Port 0x240, IRQ 2, 8-Bit DMA 3, 16-bit DMA 7", "Port 0x240, IRQ 5, 8-Bit DMA 3, 16-bit DMA 5", "Port 0x240, IRQ 5, 8-Bit DMA 1, 16-bit DMA 5", "Port 0x240, IRQ 10, 8-Bit DMA 3, 16-bit DMA 7", "Port 0x280, IRQ 10, 8-Bit DMA 0, 16-bit DMA 6", "Port 0x280, IRQ 5, 8-Bit DMA 1, 16-bit DMA 5" };
        std::vector<std::string> sbConfCore = { "A220 I7 D1 H5", "A220 I5 D1 H5", "A240 I7 D1 H5", "A240 I7 D3 H7", "A240 I2 D3 H7", "A240 I5 D3 H5", "A240 I5 D1 H5", "A240 I10 D3 H7", "A280 I10 D0 H6", "A280 I5 D1 H5" };
        std::vector<std::string> adlibVals = { "Auto (select based on the SoundBlaster type) (default)", "CMS (Creative Music System / GameBlaster)", "OPL-2 (AdLib / OPL-2 / Yamaha 3812)", "Dual OPL-2 (Dual OPL-2 used by SoundBlaster Pro 1.0 for stereo sound)", "OPL-3 (AdLib / OPL-3 / Yamaha YMF262)", "OPL-3 Gold (AdLib Gold / OPL-3 / Yamaha YMF262)", "Disabled" };
        std::vector<std::string> adlibCore = { "auto", "cms", "opl2", "dualopl2", "opl3", "opl3gold", "none" };
        std::vector<std::string> adlibEmuVals = { "Default", "High quality Nuked OPL3" };
        std::vector<std::string> adlibEmuCore = { "default", "nuked" };
        std::vector<std::string> midiVals = { "Frontend MIDI driver", "Disabled" };
        std::vector<std::string> midiCore = { "frontend", "disabled" };
        std::vector<std::string> gusVals = { "Off (default)", "On" };
        std::vector<std::string> gusCore = { "false", "true" };
        std::vector<std::string> tandyVals = { "Off (default)", "On" };
        std::vector<std::string> tandyCore = { "auto", "on" };
        std::vector<std::string> swapVals = { "Off (default)", "On" };
        std::vector<std::string> swapCore = { "false", "true" };
        std::vector<std::string> volVals, volCore;
        for (int p = 5; p <= 100; p += 5) {
            volVals.push_back(std::to_string(p) + "%");
            volCore.push_back(fmtFloat(p / 100.0));
        }
        for (int p = 110; p <= 200; p += 10) {
            volVals.push_back(std::to_string(p) + "%");
            volCore.push_back(fmtFloat(p / 100.0));
        }
        for (int p = 225; p <= 500; p += 25) {
            volVals.push_back(std::to_string(p) + "%");
            volCore.push_back(fmtFloat(p / 100.0));
        }

        m_audioItems = {
            { "Audio Sample Rate",              MenuAction::TOGGLE_VALUE, {}, rateVals, {}, findVal("dosbox_pure_audiorate", rateVals, "48000"), true, "dosbox_pure_audiorate" },
            { "SoundBlaster Type",              MenuAction::TOGGLE_VALUE, {}, sbTypeVals, sbTypeCore, findVal("dosbox_pure_sblaster_type", sbTypeCore, "sb16"), true, "dosbox_pure_sblaster_type" },
            { "SoundBlaster Settings",          MenuAction::TOGGLE_VALUE, {}, sbConfVals, sbConfCore, findVal("dosbox_pure_sblaster_conf", sbConfCore, "A220 I7 D1 H5"), true, "dosbox_pure_sblaster_conf" },
            { "SoundBlaster Adlib/FM Mode",    MenuAction::TOGGLE_VALUE, {}, adlibVals, adlibCore, findVal("dosbox_pure_sblaster_adlib_mode", adlibCore, "auto"), true, "dosbox_pure_sblaster_adlib_mode" },
            { "SoundBlaster Adlib Provider",   MenuAction::TOGGLE_VALUE, {}, adlibEmuVals, adlibEmuCore, findVal("dosbox_pure_sblaster_adlib_emu", adlibEmuCore, "default"), true, "dosbox_pure_sblaster_adlib_emu" },
            { "MIDI Output",                   MenuAction::TOGGLE_VALUE, {}, midiVals, midiCore, findVal("dosbox_pure_midi", midiCore, "disabled"), true, "dosbox_pure_midi" },
            { "Enable Gravis Ultrasound",      MenuAction::TOGGLE_VALUE, {}, gusVals, gusCore, findVal("dosbox_pure_gus", gusCore, "false"), true, "dosbox_pure_gus" },
            { "Enable Tandy Sound",            MenuAction::TOGGLE_VALUE, {}, tandyVals, tandyCore, findVal("dosbox_pure_tandysound", tandyCore, "auto"), true, "dosbox_pure_tandysound" },
            { "Swap Stereo",                   MenuAction::TOGGLE_VALUE, {}, swapVals, swapCore, findVal("dosbox_pure_swapstereo", swapCore, "false"), true, "dosbox_pure_swapstereo" },
            { "",                              MenuAction::NONE },
            { "Volume: Sound Blaster",         MenuAction::TOGGLE_VALUE, {}, volVals, volCore, findVal("dosbox_pure_volume_sb", volCore, "1.0"), true, "dosbox_pure_volume_sb" },
            { "Volume: MIDI",                  MenuAction::TOGGLE_VALUE, {}, volVals, volCore, findVal("dosbox_pure_volume_midi", volCore, "1.0"), true, "dosbox_pure_volume_midi" },
            { "Volume: Adlib",                 MenuAction::TOGGLE_VALUE, {}, volVals, volCore, findVal("dosbox_pure_volume_adlib", volCore, "1.0"), true, "dosbox_pure_volume_adlib" },
            { "Volume: Speaker",               MenuAction::TOGGLE_VALUE, {}, volVals, volCore, findVal("dosbox_pure_volume_speaker", volCore, "1.0"), true, "dosbox_pure_volume_speaker" },
            { "Volume: CD-ROM",                MenuAction::TOGGLE_VALUE, {}, volVals, volCore, findVal("dosbox_pure_volume_cdrom", volCore, "1.0"), true, "dosbox_pure_volume_cdrom" },
            { "Volume: Other",                 MenuAction::TOGGLE_VALUE, {}, volVals, volCore, findVal("dosbox_pure_volume_other", volCore, "1.0"), true, "dosbox_pure_volume_other" },
            { "",                              MenuAction::NONE },
            { "Reset to Defaults",             MenuAction::RESET_DEFAULTS },
            { "",                              MenuAction::NONE },
            { "Back",                          MenuAction::BACK },
        };
    }

    m_stateItems =
    {
        { "Save State",          MenuAction::TOGGLE_VALUE, {}, {}, {}, 0, false },
        { "Load State",          MenuAction::TOGGLE_VALUE, {}, {}, {}, 0, false },
        { "Slot",                MenuAction::TOGGLE_VALUE, {}, { "1", "2", "3", "4", "5" }, {}, 0 },
        { "",                    MenuAction::NONE },
        { "Back",                MenuAction::BACK },
    };

    m_aboutItems =
    {
        { "DOSBox Pure Unleashed", MenuAction::NONE },
        { "UWP Frontend",          MenuAction::NONE },
        { "Based on dosbox-pure",  MenuAction::NONE },
        { "libretro core",         MenuAction::NONE },
        { "",                      MenuAction::NONE },
        { "Back",                  MenuAction::BACK },
    };

    RebuildItems();
}

void FrontendMenu::RebuildItems()
{
    m_stack.clear();
    m_stack.push_back({ "DOSBox Pure Unleashed", &m_mainItems });
    m_selected = 0;
    m_scrollOffset = 0;

    // Clear overlay state
    m_overlayActive = false;
    m_overlayItems = nullptr;
    m_overlayStack.clear();

    auto& items = *m_stack.back().items;
    for (int i = 0; i < (int)items.size(); i++)
    {
        if (!items[i].label.empty() && items[i].enabled && items[i].action != MenuAction::NONE)
        {
            m_selected = i;
            break;
        }
    }
}

static bool TryLoadImgFromPath(ID2D1DeviceContext* d2d, const wchar_t* imgPath, ID2D1Bitmap1** bitmap, IWICImagingFactory* wicFactory)
{
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory->CreateDecoderFromFilename(imgPath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) return false;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr))
    {
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) return false;
    }
    hr = d2d->CreateBitmapFromWicBitmap(converter.Get(), bitmap);
    return SUCCEEDED(hr);
}

static void LoadImg(ID2D1DeviceContext* d2d, const wchar_t* filename, ID2D1Bitmap1** bitmap)
{
    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) { spdlog::warn("LoadImg: CoCreateInstance WIC failed hr={:08x}", (unsigned)hr); return; }

    // Try path 1: Package::InstalledLocation (AppX deployed)
    {
        wchar_t imgPath[MAX_PATH];
        try
        {
            auto installPath = Package::Current->InstalledLocation->Path;
            wcscpy_s(imgPath, installPath->Data());
            size_t plen = wcslen(imgPath);
            if (plen > 0 && plen < MAX_PATH - 60)
            {
                if (imgPath[plen - 1] != L'\\') { imgPath[plen] = L'\\'; plen++; }
                wcscpy_s(imgPath + plen, MAX_PATH - plen, filename);
            }
        }
        catch (...) { return; }
        if (TryLoadImgFromPath(d2d, imgPath, bitmap, wicFactory.Get()))
        {
            spdlog::info("LoadImg: OK from InstalledLocation");
            return;
        }
        spdlog::warn("LoadImg: InstalledLocation failed, trying parent dir");
    }

    // Try path 2: parent of InstalledLocation (project output dir, CopyToOutputDirectory)
    {
        wchar_t imgPath[MAX_PATH];
        try
        {
            auto installPath = Package::Current->InstalledLocation->Path;
            wcscpy_s(imgPath, installPath->Data());
            wchar_t* lastSlash = wcsrchr(imgPath, L'\\');
            if (lastSlash) *lastSlash = L'\0';
            size_t plen = wcslen(imgPath);
            if (plen > 0 && plen < MAX_PATH - 60)
            {
                if (imgPath[plen - 1] != L'\\') { imgPath[plen] = L'\\'; plen++; }
                wcscpy_s(imgPath + plen, MAX_PATH - plen, filename);
            }
        }
        catch (...) { return; }
        if (TryLoadImgFromPath(d2d, imgPath, bitmap, wicFactory.Get()))
        {
            spdlog::info("LoadImg: OK from parent dir");
            return;
        }
        spdlog::warn("LoadImg: all paths failed");
    }
}

void FrontendMenu::LoadLogoBitmap(ID2D1DeviceContext* d2d)
{
    if (!m_epaLogo)
        LoadImg(d2d, L"Assets\\EPA_logo.png", &m_epaLogo);
    if (!m_dosboxLogo)
        LoadImg(d2d, L"Assets\\dosbox-transparent.png", &m_dosboxLogo);
}

void FrontendMenu::EnsureResources(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (m_resourcesCreated) return;

    // Load VCR OSD Mono from DWrite custom font collection
    wchar_t fontPath[MAX_PATH];
    auto installPath = Package::Current->InstalledLocation->Path;
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
                                {
                                    col1.As(&m_fontCollection);
                                }
                            }
                        }
                    }
                }
            }
    }

    float fontSizeTitle = 33.0f;
    float fontSizeItem = 27.0f;
    float fontSizeFooter = 22.0f;
    if (screenW < 800.0f)
    {
        fontSizeTitle = 27.0f;
        fontSizeItem = 22.0f;
        fontSizeFooter = 18.0f;
    }

    {
        const auto& c = SettingsManager::GetTheme();
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.bg_fullscreen), &m_brushBlack);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_disabled), &m_brushDisabled);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.bg_panel), &m_brushBg);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.title_bg), &m_brushTitleBg);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.selection_text), &m_brushSelected);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_normal), &m_brushItemText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.selection_bg), &m_brushTitleText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_value), &m_brushValueText);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_disabled), &m_brushFooter);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.frame), &m_brushFrame);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_bios), &m_brushBios);
        d2d->CreateSolidColorBrush(D2D1::ColorF(c.text_title), &m_brushWhite);
    }

    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeTitle, L"en-US", &m_textFormatTitle);
    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeItem, L"en-US", &m_textFormatItem);
    dwrite->CreateTextFormat(
        L"VCR OSD Mono", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSizeFooter, L"en-US", &m_textFormatFooter);

    LoadLogoBitmap(d2d);

    m_resourcesCreated = true;
}

void FrontendMenu::ReleaseResources()
{
    if (!m_resourcesCreated) return;

    m_brushTitleBg.Reset();
    m_brushBg.Reset();
    m_brushSelected.Reset();
    m_brushItemText.Reset();
    m_brushTitleText.Reset();
    m_brushValueText.Reset();
    m_brushDisabled.Reset();
    m_brushFooter.Reset();
    m_brushFrame.Reset();
    m_brushBios.Reset();
    m_brushBlack.Reset();
    m_brushWhite.Reset();
    m_textFormatTitle.Reset();
    m_textFormatItem.Reset();
    m_textFormatFooter.Reset();
    m_epaLogo.Reset();
    m_dosboxLogo.Reset();
    m_fontCollection.Reset();

    m_resourcesCreated = false;

    m_fileBrowser.ReleaseResources();
    m_aboutDialog.ReleaseResources();
    m_confirmDialog.ReleaseResources();
}

void FrontendMenu::Render(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (!m_visible) return;
}

static constexpr double ANIM_INITIAL_SEC = 0.5;
static constexpr double ANIM_LINE_INTERVAL = 0.18;
static constexpr double ANIM_EMPTY_INTERVAL = 0.03;
static constexpr double ANIM_MEMORY_DELAY = 0.3;
static constexpr double ANIM_MEMORY_DURATION = 1.2;

static void DrawTextLine(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, IDWriteFontCollection* fc,
    IDWriteTextFormat* fmt, const wchar_t* text, UINT32 len, float x, float y, float w, float h,
    ID2D1Brush* brush)
{
    ComPtr<IDWriteTextLayout> layout;
    dwrite->CreateTextLayout(text, len, fmt, w, h, &layout);
    if (layout && fc)
    {
        DWRITE_TEXT_RANGE r = { 0, len };
        layout->SetFontCollection(fc, r);
    }
    if (layout)
        d2d->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush);
}

static ComPtr<ID2D1Brush> MakeAnimatedTitleBrush(ID2D1DeviceContext* d2d,
    float rectX, float rectY, float rectW, float rectH, uint32_t baseColor)
{
    // Subtle alpha pulse: oscillate opacity 0.85–1.0 over 3 seconds
    float t = (float)(GetTickCount64() % 3000) / 3000.0f;
    float alpha = 0.70f + 0.30f * (0.5f + 0.5f * sinf(t * 6.283185f));
    D2D1_COLOR_F col = D2D1::ColorF(baseColor);
    col.a = alpha;
    ComPtr<ID2D1SolidColorBrush> brush;
    d2d->CreateSolidColorBrush(col, &brush);
    return brush;
}

void FrontendMenu::DrawValueText(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite,
    const std::string& value, float containerX, float containerW, float iy,
    ID2D1Brush* brush, bool isSelected)
{
    if (value.empty()) return;

    std::wstring wval = L": " + std::wstring(value.begin(), value.end());
    float maxValW = containerW * VALUE_WIDTH_RATIO;

    ComPtr<IDWriteTextLayout> valLayout;
    dwrite->CreateTextLayout(wval.c_str(), (UINT32)wval.size(), m_textFormatItem.Get(),
        maxValW, ITEM_HEIGHT, &valLayout);
    if (!valLayout) return;

    if (m_fontCollection)
    {
        DWRITE_TEXT_RANGE fr = { 0, (UINT32)wval.size() };
        valLayout->SetFontCollection(m_fontCollection.Get(), fr);
    }

    // Character-level trimming — clean clip, no ellipsis
    DWRITE_TRIMMING trimming = {};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    valLayout->SetTrimming(&trimming, nullptr);

    DWRITE_TEXT_METRICS tm;
    valLayout->GetMetrics(&tm);

    float valRight = containerX + containerW - ITEM_INDENT;
    float valX = valRight - min(tm.width, maxValW);

    // Marquee: scroll selected items whose text overflows
    if (isSelected && tm.width > maxValW)
    {
        float overflow = tm.width - maxValW;
        float speed = 45.0f; // px/sec
        float pauseMs = 1500.0f;
        float scrollMs = (overflow / speed) * 1000.0f;
        float cycleMs = pauseMs * 2.0f + scrollMs;

        float t = (float)(GetTickCount64() % (ULONGLONG)cycleMs);
        float offset = 0.0f;
        if (t < pauseMs)
            offset = 0.0f;
        else if (t < pauseMs + scrollMs)
            offset = (t - pauseMs) * speed / 1000.0f;
        else
            offset = overflow;

        // Clip to value area, draw at scrolled position
        D2D1_RECT_F clip = { valRight - maxValW, iy, valRight, iy + ITEM_HEIGHT };
        d2d->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        d2d->DrawTextLayout(D2D1::Point2F(valX - offset, iy), valLayout.Get(), brush);
        d2d->PopAxisAlignedClip();
    }
    else
    {
        d2d->DrawTextLayout(D2D1::Point2F(valX, iy), valLayout.Get(), brush);
    }
}

void FrontendMenu::RenderFullScreen(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float screenW, float screenH)
{
    if (!m_visible) return;

    EnsureResources(d2d, dwrite, screenW, screenH);
    const auto& theme = SettingsManager::GetTheme();

    m_lastScreenW = screenW;
    m_lastScreenH = screenH;

    // Full black background — always draw as base layer for BIOS text and dialog overlays
    {
        D2D1_RECT_F fullBg = { 0, 0, screenW, screenH };
        d2d->FillRectangle(fullBg, m_brushBlack.Get());
    }

    // EPA logo top-right with pulsing animation
    if (m_epaLogo)
    {
        D2D1_SIZE_F epaSize = m_epaLogo->GetSize();
        float epaW = 150.0f;
        float epaH = epaSize.height * (epaW / epaSize.width);
        float epaX = screenW - PANEL_MARGIN - epaW;
        double t = GetTickCount64() / 1000.0;
        float opacity = 0.4f + 0.3f * (sinf((float)(t * 3.0)) + 1.0f);
        D2D1_RECT_F epaRect = { epaX, PANEL_MARGIN, epaX + epaW, PANEL_MARGIN + epaH };
        d2d->DrawBitmap(m_epaLogo.Get(), epaRect, opacity);
    }

    // Boot animation — derive phases + continuous memory count
    double elapsedSec = (GetTickCount64() - m_animStartTick) / 1000.0;

    AnimPhase newPhase;
    int linesToShow = 0;
    int memDisplayedKB = 0;

    double totalLines = (double)m_biosLines.size();
    double lineSeqTime = ANIM_INITIAL_SEC + totalLines * ANIM_LINE_INTERVAL;
    double memRevealTime = ANIM_INITIAL_SEC + 5 * ANIM_LINE_INTERVAL; // line index 4 (approximate, skips empty-line variance)

    if (elapsedSec < ANIM_INITIAL_SEC)
    {
        newPhase = ANIM_INITIAL_DELAY;
    }
    else if (elapsedSec < lineSeqTime)
    {
        newPhase = ANIM_BIOS_POST;
        double textElapsed = elapsedSec - ANIM_INITIAL_SEC;
        double timeUsed = 0.0;
        for (int i = 0; i < (int)m_biosLines.size() && timeUsed <= textElapsed; i++)
        {
            linesToShow = i + 1;
            timeUsed += m_biosLines[i].empty() ? ANIM_EMPTY_INTERVAL : ANIM_LINE_INTERVAL;
        }
    }
    else if (elapsedSec < lineSeqTime + ANIM_MEMORY_DELAY + ANIM_MEMORY_DURATION)
    {
        newPhase = ANIM_MEMORY_COUNT;
        linesToShow = (int)m_biosLines.size();
    }
    else
    {
        newPhase = ANIM_COMPLETE;
        linesToShow = (int)m_biosLines.size();
    }

    // Continuous memory count — starts from 0 when line 4 is revealed
    if (elapsedSec > memRevealTime)
    {
        double me = elapsedSec - memRevealTime;
        double progress = me / ANIM_MEMORY_DURATION;
        if (progress > 1.0) progress = 1.0;
        memDisplayedKB = (int)(m_memoryTotalMB * 1024 * progress);
    }

    // Trigger beep on transition to COMPLETE
    if (newPhase >= ANIM_COMPLETE && m_animPhase < ANIM_COMPLETE && onBeep && !m_beepPlayed)
    {
        onBeep();
        m_beepPlayed = true;
        m_animCompleteTick = GetTickCount64();
    }
    m_animPhase = newPhase;
    m_biosLinesToShow = linesToShow;
    m_animMemoryDisplayedKB = memDisplayedKB;

    bool showPanel = (m_animPhase >= ANIM_COMPLETE);

    // Panel metrics (computed early for BIOS text width regardless of visibility)
    float panelW = screenW * PANEL_WIDTH_RATIO;
    if (panelW > PANEL_MAX_WIDTH) panelW = PANEL_MAX_WIDTH;
    float panelX = PANEL_MARGIN;
    float panelH = PANEL_FIXED_HEIGHT;
    float panelY = screenH - panelH - PANEL_MARGIN;

    m_lastPanelX = panelX;
    m_lastPanelY = panelY;
    m_lastPanelW = panelW;
    m_lastPanelH = panelH;

    // BIOS POST text (full width)
    float biosTextW = screenW - panelX - PANEL_MARGIN;
    float biosY = PANEL_MARGIN + 10.0f;
    float biosLineH = 24.0f;

    for (int li = 0; li < (int)m_biosLines.size() && li < linesToShow; li++)
    {
        std::wstring displayLine = m_biosLines[li];

        // Memory line — always show count-up (starts 0, no jump, no MB gap)
        if (li == 4)
        {
            wchar_t memBuf[64];
            swprintf_s(memBuf, L"Memory: %dK OK", m_animMemoryDisplayedKB);
            displayLine = memBuf;
        }

        DrawTextLine(d2d, dwrite, m_fontCollection.Get(), m_textFormatFooter.Get(),
            displayLine.c_str(), (UINT32)displayLine.size(),
            panelX + 4.0f, biosY, biosTextW, biosLineH,
            m_brushBios.Get());

        biosY += biosLineH + 2.0f;
    }

    // DOSBox watermark — bottom-right, fixed size, above version
    if (m_dosboxLogo)
    {
        D2D1_SIZE_F dbSize = m_dosboxLogo->GetSize();
        float aspect = dbSize.width / dbSize.height;
        float logoSz = 280.0f;
        float dbW = logoSz;
        float dbH = dbW / aspect;
        float dbX = screenW - dbW - PANEL_MARGIN;
        float dbY = screenH - dbH - LOGO_MARGIN - FOOTER_HEIGHT - LOGO_MARGIN;
        D2D1_RECT_F dbRect = { dbX, dbY, dbX + dbW, dbY + dbH };
        d2d->DrawBitmap(m_dosboxLogo.Get(), dbRect, 1.0f);
    }

    // Version bottom-right (replaced by toast when active)
    {
        const wchar_t* footerText = m_versionStr.c_str();
        size_t footerLen = m_versionStr.size();
        std::wstring toastBuf;
        if (m_toastTick != 0 && (GetTickCount64() - m_toastTick) < TOAST_DURATION_MS)
        {
            footerText = m_toastMsg.c_str();
            footerLen = m_toastMsg.size();
        }
        else
        {
            m_toastTick = 0;
        }
        ComPtr<IDWriteTextLayout> verLayout;
        dwrite->CreateTextLayout(footerText, (UINT32)footerLen, m_textFormatFooter.Get(),
            300.0f, FOOTER_HEIGHT, &verLayout);
        if (verLayout && m_fontCollection)
        {
            DWRITE_TEXT_RANGE fr = { 0, (UINT32)footerLen };
            verLayout->SetFontCollection(m_fontCollection.Get(), fr);
        }
        if (verLayout)
        {
            DWRITE_TEXT_METRICS tm;
            verLayout->GetMetrics(&tm);
            d2d->DrawTextLayout(
                D2D1::Point2F(screenW - tm.width - LOGO_MARGIN, screenH - tm.height - LOGO_MARGIN),
                verLayout.Get(), m_brushFooter.Get());
        }
    }

    // Panel (dialog) — only after boot animation completes
    if (!showPanel || m_stack.empty()) return;

    auto& items = *m_stack.back().items;

    // Panel background
    D2D1_RECT_F panelBg = { panelX, panelY, panelX + panelW, panelY + panelH };
    d2d->FillRectangle(panelBg, m_brushBg.Get());

    // Panel outer frame
    float frameW = 2.0f;
    d2d->DrawRectangle(panelBg, m_brushFrame.Get(), frameW);
    D2D1_RECT_F innerFrame = { panelX + 4.0f, panelY + 4.0f, panelX + panelW - 4.0f, panelY + panelH - 4.0f };
    d2d->DrawRectangle(innerFrame, m_brushFrame.Get(), 1.0f);

    // Title bar (animated gradient)
    float titleY = panelY + 8.0f;
    D2D1_RECT_F titleBg = { panelX + 8.0f, titleY, panelX + panelW - 8.0f, titleY + TITLE_HEIGHT };
    auto titleBrush = MakeAnimatedTitleBrush(d2d, titleBg.left, titleBg.top,
        titleBg.right - titleBg.left, titleBg.bottom - titleBg.top, theme.title_bg);
    d2d->FillRectangle(titleBg, titleBrush.Get());

    ComPtr<IDWriteTextLayout> titleLayout;
    std::wstring wtitle(m_stack.back().title.begin(), m_stack.back().title.end());
    dwrite->CreateTextLayout(wtitle.c_str(), (UINT32)wtitle.size(), m_textFormatTitle.Get(),
        panelW - ITEM_INDENT * 2, TITLE_HEIGHT, &titleLayout);
    if (titleLayout)
    {
        if (m_fontCollection)
            { DWRITE_TEXT_RANGE fr = { 0, (UINT32)wtitle.size() }; titleLayout->SetFontCollection(m_fontCollection.Get(), fr); }
        DWRITE_TEXT_METRICS tm;
        titleLayout->GetMetrics(&tm);
        float tx = panelX + (panelW - tm.width) * 0.5f;
        d2d->DrawTextLayout(
            D2D1::Point2F(tx, titleY + 4.0f),
            titleLayout.Get(), m_brushWhite.Get());
    }

    // Item list — aligned to bottom, gap between title and items
    float itemAreaBottom = panelY + panelH - 8.0f - 8.0f;
    float listAvailable = itemAreaBottom - (titleY + TITLE_HEIGHT + 8.0f);
    int maxFit = (int)(listAvailable / ITEM_HEIGHT);
    if (maxFit < 1) maxFit = 1;
    if (maxFit > MAX_VISIBLE) maxFit = (int)MAX_VISIBLE;

    // Use saved panel selection when overlay is active
    int panelSel = m_overlayActive ? m_panelSavedSelected : m_selected;
    int panelScroll = m_overlayActive ? m_panelSavedScrollOffset : m_scrollOffset;

    int itemCount = (int)items.size();
    int visibleCount = min(itemCount, maxFit);

    if (panelScroll > itemCount - visibleCount)
        panelScroll = itemCount - visibleCount;
    if (panelScroll < 0) panelScroll = 0;

    float listY = itemAreaBottom - visibleCount * ITEM_HEIGHT;

    for (int i = 0; i < visibleCount; i++)
    {
        int idx = panelScroll + i;

        auto& item = items[idx];
        float iy = listY + i * ITEM_HEIGHT;

        if (idx == panelSel && item.action != MenuAction::NONE)
        {
            D2D1_RECT_F selRect = { panelX + 8.0f, iy, panelX + panelW - 8.0f, iy + ITEM_HEIGHT };
            d2d->FillRectangle(selRect, m_brushTitleText.Get());

            std::wstring wlabel(item.label.begin(), item.label.end());
            ComPtr<IDWriteTextLayout> itemLayout;
            auto textBrush = item.enabled ? m_brushSelected.Get() : m_brushDisabled.Get();
            dwrite->CreateTextLayout(wlabel.c_str(), (UINT32)wlabel.size(), m_textFormatItem.Get(),
                panelW - ITEM_INDENT * 3, ITEM_HEIGHT, &itemLayout);
            if (itemLayout)
            {
                if (m_fontCollection)
                {
                    DWRITE_TEXT_RANGE fr = { 0, (UINT32)wlabel.size() };
                    itemLayout->SetFontCollection(m_fontCollection.Get(), fr);
                }
                d2d->DrawTextLayout(
                    D2D1::Point2F(panelX + ITEM_INDENT, iy),
                    itemLayout.Get(), textBrush);
            }

            if (!item.values.empty())
                DrawValueText(d2d, dwrite, item.values[item.currentValue], panelX, panelW, iy, m_brushSelected.Get(), true);
        }
        else if (item.label.empty())
        {
            float sepY = iy + ITEM_HEIGHT * 0.5f;
            d2d->DrawLine(
                D2D1::Point2F(panelX + 20.0f, sepY),
                D2D1::Point2F(panelX + panelW - 20.0f, sepY),
                m_brushFooter.Get(), 1.0f);
            continue;
        }
        else
        {
            std::wstring wlabel(item.label.begin(), item.label.end());
            ComPtr<IDWriteTextLayout> itemLayout;
            dwrite->CreateTextLayout(wlabel.c_str(), (UINT32)wlabel.size(), m_textFormatItem.Get(),
                panelW - ITEM_INDENT * 3, ITEM_HEIGHT, &itemLayout);
            if (itemLayout)
            {
                if (m_fontCollection)
                {
                    DWRITE_TEXT_RANGE fr = { 0, (UINT32)wlabel.size() };
                    itemLayout->SetFontCollection(m_fontCollection.Get(), fr);
                }
                auto brush = item.enabled ? m_brushItemText.Get() : m_brushFooter.Get();
                d2d->DrawTextLayout(
                    D2D1::Point2F(panelX + ITEM_INDENT, iy),
                    itemLayout.Get(), brush);
            }

            if (!item.values.empty())
                DrawValueText(d2d, dwrite, item.values[item.currentValue], panelX, panelW, iy, m_brushValueText.Get(), false);
        }
    }

    // Settings overlay — render stacked overlays (like FileBrowser card stacking)
    if (m_overlayActive && m_overlayItems)
    {
        const auto& theme = SettingsManager::GetTheme();

        // Collect all overlays to render: stack (deepest first) + current (topmost)
        struct OverlayRender { std::string title; std::vector<MenuItem>* items; int selected; int scrollOffset; };
        std::vector<OverlayRender> allOverlays;
        for (auto& s : m_overlayStack)
            allOverlays.push_back({ s.title, s.items, s.selected, s.scrollOffset });
        allOverlays.push_back({ m_overlayTitle, m_overlayItems, m_selected, m_scrollOffset });

        // Single semi-transparent background for all overlays
        ComPtr<ID2D1SolidColorBrush> overlayBg;
        d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_fullscreen, theme.overlay_alpha), &overlayBg);
        D2D1_RECT_F fullBg = { 0, 0, screenW, screenH };
        d2d->FillRectangle(fullBg, overlayBg.Get());

        // Render each overlay as a card, stacked with fixed offsets
        int totalLayers = (int)allOverlays.size();

        // Overlay panel size (screen-dependent)
        float ovW = screenW * OVERLAY_WIDTH_RATIO;
        if (ovW > OVERLAY_MAX_WIDTH) ovW = OVERLAY_MAX_WIDTH;
        if (ovW < PANEL_MIN_WIDTH) ovW = PANEL_MIN_WIDTH;
        float ovH = screenH * OVERLAY_HEIGHT_RATIO;
        if (ovH > OVERLAY_MAX_HEIGHT) ovH = OVERLAY_MAX_HEIGHT;

        // Fixed positions per layer (same X pattern as FileBrowser, cascade offset between layers)
        // Layer 0: (30, screenH - ovH - 40)
        // Layer 1: (60, screenH - ovH - 40 - 30)
        float layerBaseX = 30.0f;
        float layerBaseY = screenH - ovH - 40.0f;
        if (layerBaseX + ovW > screenW - PANEL_MARGIN)
            layerBaseX = screenW - ovW - PANEL_MARGIN;
        if (layerBaseY < PANEL_MARGIN) layerBaseY = PANEL_MARGIN;

        float layerX[3] = { layerBaseX, layerBaseX + 15.0f, layerBaseX + 30.0f };
        float layerY[3] = { layerBaseY, layerBaseY - 15.0f, layerBaseY - 30.0f };

        for (int layer = 0; layer < totalLayers; layer++)
        {
            auto& ov = allOverlays[layer];
            bool isTopmost = (layer == totalLayers - 1);

            float ovX = layerX[layer];
            float ovY = layerY[layer];
            if (ovX + ovW > screenW - PANEL_MARGIN) ovX = screenW - ovW - PANEL_MARGIN;
            if (ovY < PANEL_MARGIN) ovY = PANEL_MARGIN;

            // Panel background + frame (slightly dimmed for non-topmost)
            float bgAlpha = isTopmost ? 1.0f : 0.85f;
            ComPtr<ID2D1SolidColorBrush> panelBgBrush;
            d2d->CreateSolidColorBrush(D2D1::ColorF(theme.bg_panel, bgAlpha), &panelBgBrush);
            D2D1_RECT_F panelBg = { ovX, ovY, ovX + ovW, ovY + ovH };
            d2d->FillRectangle(panelBg, panelBgBrush.Get());
            d2d->DrawRectangle(panelBg, m_brushFrame.Get(), isTopmost ? 2.0f : 1.0f);
            D2D1_RECT_F innerFrame = { ovX + 4.0f, ovY + 4.0f, ovX + ovW - 4.0f, ovY + ovH - 4.0f };
            d2d->DrawRectangle(innerFrame, m_brushFrame.Get(), 1.0f);

            // Title bar (animated gradient)
            float titleY = ovY + 8.0f;
            D2D1_RECT_F titleBg = { ovX + 8.0f, titleY, ovX + ovW - 8.0f, titleY + TITLE_HEIGHT };
            auto ovTitleBrush = MakeAnimatedTitleBrush(d2d, titleBg.left, titleBg.top,
                titleBg.right - titleBg.left, titleBg.bottom - titleBg.top, theme.title_bg);
            d2d->FillRectangle(titleBg, ovTitleBrush.Get());

            ComPtr<IDWriteTextLayout> titleLayout;
            std::wstring wtitle(ov.title.begin(), ov.title.end());
            dwrite->CreateTextLayout(wtitle.c_str(), (UINT32)wtitle.size(), m_textFormatTitle.Get(),
                ovW - ITEM_INDENT * 2, TITLE_HEIGHT, &titleLayout);
            if (titleLayout)
            {
                if (m_fontCollection)
                    { DWRITE_TEXT_RANGE fr = { 0, (UINT32)wtitle.size() }; titleLayout->SetFontCollection(m_fontCollection.Get(), fr); }
                DWRITE_TEXT_RANGE fr2 = { 0, (UINT32)wtitle.size() };
                titleLayout->SetFontCollection(m_fontCollection.Get(), fr2);
                DWRITE_TEXT_METRICS tm;
                titleLayout->GetMetrics(&tm);
                float tx = ovX + (ovW - tm.width) * 0.5f;
                d2d->DrawTextLayout(D2D1::Point2F(tx, titleY + 4.0f), titleLayout.Get(), m_brushWhite.Get());
            }

            // Items
            auto& ovItems = *ov.items;
            float itemAreaBottom = ovY + ovH - 8.0f - (isTopmost ? (FOOTER_HEIGHT + 8.0f) : 8.0f);
            float listAvailable = itemAreaBottom - (titleY + TITLE_HEIGHT + 8.0f);
            int maxFit = (int)(listAvailable / ITEM_HEIGHT);
            if (maxFit < 1) maxFit = 1;
            if (maxFit > MAX_VISIBLE) maxFit = (int)MAX_VISIBLE;

            int itemCount = (int)ovItems.size();
            int visibleCount = min(itemCount, maxFit);
            int layerScroll = ov.scrollOffset;
            if (layerScroll > itemCount - visibleCount) layerScroll = max(0, itemCount - visibleCount);
            if (layerScroll < 0) layerScroll = 0;

            float listY = itemAreaBottom - visibleCount * ITEM_HEIGHT;

            for (int i = 0; i < visibleCount; i++)
            {
                int idx = layerScroll + i;
                auto& item = ovItems[idx];
                float iy = listY + i * ITEM_HEIGHT;

                if (idx == ov.selected && item.action != MenuAction::NONE && isTopmost)
                {
                    D2D1_RECT_F selRect = { ovX + 8.0f, iy, ovX + ovW - 8.0f, iy + ITEM_HEIGHT };
                    d2d->FillRectangle(selRect, m_brushTitleText.Get());

                    std::wstring wlabel(item.label.begin(), item.label.end());
                    ComPtr<IDWriteTextLayout> itemLayout;
                    auto textBrush = item.enabled ? m_brushSelected.Get() : m_brushDisabled.Get();
                    dwrite->CreateTextLayout(wlabel.c_str(), (UINT32)wlabel.size(), m_textFormatItem.Get(),
                        ovW - ITEM_INDENT * 3, ITEM_HEIGHT, &itemLayout);
                    if (itemLayout)
                    {
                        if (m_fontCollection)
                            { DWRITE_TEXT_RANGE fr = { 0, (UINT32)wlabel.size() }; itemLayout->SetFontCollection(m_fontCollection.Get(), fr); }
                        d2d->DrawTextLayout(D2D1::Point2F(ovX + ITEM_INDENT, iy), itemLayout.Get(), textBrush);
                    }
                    if (!item.values.empty())
                        DrawValueText(d2d, dwrite, item.values[item.currentValue], ovX, ovW, iy, m_brushSelected.Get(), true);
                }
                else if (item.label.empty())
                {
                    float sepY = iy + ITEM_HEIGHT * 0.5f;
                    d2d->DrawLine(D2D1::Point2F(ovX + 20.0f, sepY),
                        D2D1::Point2F(ovX + ovW - 20.0f, sepY), m_brushFooter.Get(), 1.0f);
                }
                else
                {
                    std::wstring wlabel(item.label.begin(), item.label.end());
                    ComPtr<IDWriteTextLayout> itemLayout;
                    dwrite->CreateTextLayout(wlabel.c_str(), (UINT32)wlabel.size(), m_textFormatItem.Get(),
                        ovW - ITEM_INDENT * 3, ITEM_HEIGHT, &itemLayout);
                    if (itemLayout)
                    {
                        if (m_fontCollection)
                            { DWRITE_TEXT_RANGE fr = { 0, (UINT32)wlabel.size() }; itemLayout->SetFontCollection(m_fontCollection.Get(), fr); }
                        auto brush = isTopmost ? (item.enabled ? m_brushItemText.Get() : m_brushFooter.Get()) : m_brushFooter.Get();
                        d2d->DrawTextLayout(D2D1::Point2F(ovX + ITEM_INDENT, iy), itemLayout.Get(), brush);
                    }
                    if (!item.values.empty())
                        DrawValueText(d2d, dwrite, item.values[item.currentValue], ovX, ovW, iy, m_brushValueText.Get(), false);
                }
            }

            // "B: Back" footer hint (only on topmost)
            if (isTopmost)
            {
                std::wstring hint = L"B: Back";
                ComPtr<IDWriteTextLayout> hintLayout;
                dwrite->CreateTextLayout(hint.c_str(), (UINT32)hint.size(), m_textFormatFooter.Get(),
                    ovW, FOOTER_HEIGHT, &hintLayout);
                if (hintLayout)
                {
                    if (m_fontCollection)
                        { DWRITE_TEXT_RANGE fr = { 0, (UINT32)hint.size() }; hintLayout->SetFontCollection(m_fontCollection.Get(), fr); }
                    D2D1_RECT_F fbBg = { ovX + 8.0f, ovY + ovH - 8.0f - FOOTER_HEIGHT, ovX + ovW - 8.0f, ovY + ovH - 8.0f };
                    d2d->FillRectangle(fbBg, m_brushTitleBg.Get());
                    d2d->DrawTextLayout(D2D1::Point2F(ovX + ITEM_INDENT, ovY + ovH - 8.0f - FOOTER_HEIGHT),
                        hintLayout.Get(), m_brushFooter.Get());
                }
            }
        }
    }

    // FileBrowser overlay (drawn on top of menu panel)
    m_fileBrowser.Render(d2d, dwrite, screenW, screenH);

    // About dialog (drawn on top of everything)
    m_aboutDialog.Render(d2d, dwrite, screenW, screenH);

    // Confirm dialog (drawn on top of everything)
    m_confirmDialog.Render(d2d, dwrite, screenW, screenH);
}

void FrontendMenu::SaveCurrentSettings()
{
    // Collect all option keys from all settings sub-menus and push to core
    auto pushSection = [](const std::vector<MenuItem>& items) {
        for (auto& item : items)
        {
            if (item.action == MenuAction::TOGGLE_VALUE && !item.optionKey.empty())
            {
                const char* key = item.optionKey.c_str();
                std::string val = SettingsManager::GetOption(key, "");
                RetroCore::SetOptionValue(key, val.c_str());
            }
        }
    };
    pushSection(m_generalItems);
    pushSection(m_inputItems);
    pushSection(m_performanceItems);
    pushSection(m_videoItems);
    pushSection(m_systemItems);
    pushSection(m_audioItems);
    SettingsManager::Save();
    spdlog::info("[FrontendMenu] Settings saved to disk + pushed to core");
}

void FrontendMenu::ShowToast(const wchar_t* msg)
{
    m_toastMsg = msg;
    m_toastTick = GetTickCount64();
}

int FrontendMenu::HitTest(float sx, float sy)
{
    if (!m_visible) return -1;

    // ConfirmDialog blocks all input
    if (m_confirmDialog.IsVisible()) return -1;

    // AboutDialog has its own hit test
    if (m_aboutDialog.IsVisible()) return -1;

    // Overlay hit test (only topmost overlay receives input)
    if (m_overlayActive && m_overlayItems)
    {
        // Topmost overlay position (must match render logic)
        int topLayer = (int)m_overlayStack.size(); // layer index of topmost

        float ovW = m_lastScreenW * OVERLAY_WIDTH_RATIO;
        if (ovW > OVERLAY_MAX_WIDTH) ovW = OVERLAY_MAX_WIDTH;
        if (ovW < PANEL_MIN_WIDTH) ovW = PANEL_MIN_WIDTH;
        float ovH = m_lastScreenH * OVERLAY_HEIGHT_RATIO;
        if (ovH > OVERLAY_MAX_HEIGHT) ovH = OVERLAY_MAX_HEIGHT;

        float layerBaseX = 30.0f;
        float layerBaseY = m_lastScreenH - ovH - 40.0f;
        if (layerBaseX + ovW > m_lastScreenW - PANEL_MARGIN)
            layerBaseX = m_lastScreenW - ovW - PANEL_MARGIN;
        if (layerBaseY < PANEL_MARGIN) layerBaseY = PANEL_MARGIN;

        float layerX[3] = { layerBaseX, layerBaseX + 15.0f, layerBaseX + 30.0f };
        float layerY[3] = { layerBaseY, layerBaseY - 15.0f, layerBaseY - 30.0f };

        float ovX = layerX[topLayer];
        float ovY = layerY[topLayer];
        if (ovX + ovW > m_lastScreenW - PANEL_MARGIN) ovX = m_lastScreenW - ovW - PANEL_MARGIN;
        if (ovY < PANEL_MARGIN) ovY = PANEL_MARGIN;

        if (sx < ovX || sx > ovX + ovW || sy < ovY || sy > ovY + ovH) return -1;

        auto& items = *m_overlayItems;
        float titleY = ovY + 8.0f;
        float itemAreaBottom = ovY + ovH - 8.0f - FOOTER_HEIGHT - 8.0f;
        float listAvailable = itemAreaBottom - (titleY + TITLE_HEIGHT + 8.0f);
        int maxFit = (int)(listAvailable / ITEM_HEIGHT);
        if (maxFit < 1) maxFit = 1;
        int visibleCount = min((int)items.size(), maxFit);
        int scroll = m_scrollOffset;
        if (scroll > (int)items.size() - visibleCount) scroll = max(0, (int)items.size() - visibleCount);
        if (visibleCount < 1) return -1;
        float listY = itemAreaBottom - visibleCount * ITEM_HEIGHT;

        if (sy < listY) return -1;
        int idx = (int)((sy - listY) / ITEM_HEIGHT) + scroll;
        if (idx < 0 || idx >= (int)items.size()) return -1;
        if (items[idx].label.empty() || !items[idx].enabled) return -1;
        return idx;
    }

    if (sx < m_lastPanelX || sx > m_lastPanelX + m_lastPanelW) return -1;

    auto& items = *m_stack.back().items;
    float titleY = m_lastPanelY + 8.0f;
    float itemAreaBottom = m_lastPanelY + m_lastPanelH - 16.0f;
    float listAvailable = itemAreaBottom - (titleY + TITLE_HEIGHT + 8.0f);
    int maxFit = (int)(listAvailable / ITEM_HEIGHT);
    int visibleCount = min((int)items.size(), maxFit);
    float listY = itemAreaBottom - visibleCount * ITEM_HEIGHT;

    if (sy < listY) return -1;
    int idx = (int)((sy - listY) / ITEM_HEIGHT) + m_scrollOffset;
    if (idx < 0 || idx >= (int)items.size()) return -1;
    if (items[idx].label.empty() || !items[idx].enabled) return -1;
    return idx;
}

void FrontendMenu::SelectItem(int idx)
{
    if (!m_visible || idx < 0) return;
    auto& items = *m_stack.back().items;
    if (idx >= (int)items.size()) return;
    m_selected = idx;
    int visibleCount = (int)MAX_VISIBLE;
    if (m_selected < m_scrollOffset)
        m_scrollOffset = m_selected;
    if (m_selected >= m_scrollOffset + visibleCount)
        m_scrollOffset = m_selected - visibleCount + 1;
}

void FrontendMenu::HandlePointerMove(float sx, float sy)
{
    if (m_confirmDialog.IsVisible()) return;
    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.HandlePointerMove(sx, sy);
        return;
    }
    if (m_aboutDialog.IsVisible()) return;
    int idx = HitTest(sx, sy);
    if (idx >= 0) m_selected = idx;
}

void FrontendMenu::HandlePointerDown(float sx, float sy, unsigned btn)
{
    if (!m_visible) return;
    if (m_confirmDialog.IsVisible())
    {
        if (btn == 1)
            m_confirmDialog.HandlePointerDown(sx, sy);
        return;
    }
    if (m_fileBrowser.IsVisible())
    {
        if (btn == 1)
            m_fileBrowser.HandlePointerDown(sx, sy);
        return;
    }
    if (m_aboutDialog.IsVisible())
    {
        if (btn == 1)
            m_aboutDialog.HandlePointerDown(sx, sy);
        return;
    }
    // Click on items (works for both panel and overlay via HitTest)
    if (btn == 1)
    {
        int idx = HitTest(sx, sy);
        if (idx >= 0)
        {
            m_selected = idx;
            OnConfirm();
        }
    }
}

void FrontendMenu::HandlePointerWheel(int delta)
{
    if (!m_visible) return;
    if (m_confirmDialog.IsVisible()) return;
    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.HandlePointerWheel(delta);
        return;
    }
    if (m_aboutDialog.IsVisible()) return;
    // Scroll overlay items
    if (m_overlayActive && m_overlayItems)
    {
        auto& items = *m_overlayItems;
        if (delta < 0)
        {
            m_selected++;
            if (m_selected >= (int)items.size()) m_selected = 0;
        }
        else
        {
            m_selected--;
            if (m_selected < 0) m_selected = (int)items.size() - 1;
        }
        int visibleCount = (int)MAX_VISIBLE;
        if (m_selected < m_scrollOffset) m_scrollOffset = m_selected;
        if (m_selected >= m_scrollOffset + visibleCount) m_scrollOffset = m_selected - visibleCount + 1;
        return;
    }
}

void FrontendMenu::OnDPad(bool up)
{
    if (!m_visible) return;

    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.OnDPad(up);
        return;
    }
    if (m_aboutDialog.IsVisible()) return;

    // Route to overlay
    if (m_overlayActive && m_overlayItems)
    {
        auto& items = *m_overlayItems;
        int count = (int)items.size();
        if (up)
        {
            do {
                m_selected--;
                if (m_selected < 0) m_selected = count - 1;
            } while (!items[m_selected].enabled || items[m_selected].label.empty());
        }
        else
        {
            do {
                m_selected++;
                if (m_selected >= count) m_selected = 0;
            } while (!items[m_selected].enabled || items[m_selected].label.empty());
        }
        int visibleCount = (int)MAX_VISIBLE;
        if (m_selected < m_scrollOffset) m_scrollOffset = m_selected;
        if (m_selected >= m_scrollOffset + visibleCount) m_scrollOffset = m_selected - visibleCount + 1;
        return;
    }

    auto& items = *m_stack.back().items;
    int count = (int)items.size();

    if (up)
    {
        do {
            m_selected--;
            if (m_selected < 0) m_selected = count - 1;
        } while (!items[m_selected].enabled || items[m_selected].label.empty());
    }
    else
    {
        do {
            m_selected++;
            if (m_selected >= count) m_selected = 0;
        } while (!items[m_selected].enabled || items[m_selected].label.empty());
    }

    int visibleCount = (int)MAX_VISIBLE;
    if (m_selected < m_scrollOffset)
        m_scrollOffset = m_selected;
    if (m_selected >= m_scrollOffset + visibleCount)
        m_scrollOffset = m_selected - visibleCount + 1;
}

void FrontendMenu::OnDPadLeft()
{
    if (!m_visible) return;

    if (m_confirmDialog.IsVisible())
    {
        m_confirmDialog.HandleKeyDown(0x25); // VK_LEFT
        return;
    }

    if (m_overlayActive && m_overlayItems)
    {
        auto& items = *m_overlayItems;
        if (m_selected < 0 || m_selected >= (int)items.size()) return;
        auto& item = items[m_selected];
        if (item.action == MenuAction::TOGGLE_VALUE && item.values.size() > 1 && item.enabled)
        {
            item.currentValue = (item.currentValue - 1 + (int)item.values.size()) % (int)item.values.size();
            if (!item.optionKey.empty())
            {
                const char* newVal = item.coreValues.empty() ? item.values[item.currentValue].c_str() : item.coreValues[item.currentValue].c_str();
                spdlog::info("[FrontendMenu] DPadLeft TOGGLE: {} = {}", item.optionKey, newVal);
                SettingsManager::SetOption(item.optionKey.c_str(), newVal);
                RetroCore::SetOptionValue(item.optionKey.c_str(), newVal);
                if (onOptionChanged)
                    onOptionChanged(item.optionKey.c_str(), newVal);
            }
        }
        return;
    }

    auto& items = *m_stack.back().items;
    if (m_selected < 0 || m_selected >= (int)items.size()) return;
    auto& item = items[m_selected];
    if (item.action == MenuAction::TOGGLE_VALUE && item.values.size() > 1 && item.enabled)
    {
        item.currentValue = (item.currentValue - 1 + (int)item.values.size()) % (int)item.values.size();
        if (!item.optionKey.empty())
        {
            const char* newVal = item.coreValues.empty() ? item.values[item.currentValue].c_str() : item.coreValues[item.currentValue].c_str();
            spdlog::info("[FrontendMenu] DPadLeft TOGGLE: {} = {}", item.optionKey, newVal);
            SettingsManager::SetOption(item.optionKey.c_str(), newVal);
            RetroCore::SetOptionValue(item.optionKey.c_str(), newVal);
            if (onOptionChanged)
                onOptionChanged(item.optionKey.c_str(), newVal);
        }
    }
}

void FrontendMenu::OnDPadRight()
{
    if (!m_visible) return;

    if (m_confirmDialog.IsVisible())
    {
        m_confirmDialog.HandleKeyDown(0x27); // VK_RIGHT
        return;
    }

    if (m_overlayActive && m_overlayItems)
    {
        auto& items = *m_overlayItems;
        if (m_selected < 0 || m_selected >= (int)items.size()) return;
        auto& item = items[m_selected];
        if (item.action == MenuAction::TOGGLE_VALUE && item.values.size() > 1 && item.enabled)
        {
            item.currentValue = (item.currentValue + 1) % (int)item.values.size();
            if (!item.optionKey.empty())
            {
                const char* newVal = item.coreValues.empty() ? item.values[item.currentValue].c_str() : item.coreValues[item.currentValue].c_str();
                spdlog::info("[FrontendMenu] DPadRight TOGGLE: {} = {}", item.optionKey, newVal);
                SettingsManager::SetOption(item.optionKey.c_str(), newVal);
                RetroCore::SetOptionValue(item.optionKey.c_str(), newVal);
                if (onOptionChanged)
                    onOptionChanged(item.optionKey.c_str(), newVal);
            }
        }
        return;
    }

    auto& items = *m_stack.back().items;
    if (m_selected < 0 || m_selected >= (int)items.size()) return;
    auto& item = items[m_selected];
    if (item.action == MenuAction::TOGGLE_VALUE && item.values.size() > 1 && item.enabled)
    {
        item.currentValue = (item.currentValue + 1) % (int)item.values.size();
        if (!item.optionKey.empty())
        {
            const char* newVal = item.coreValues.empty() ? item.values[item.currentValue].c_str() : item.coreValues[item.currentValue].c_str();
            spdlog::info("[FrontendMenu] DPadRight TOGGLE: {} = {}", item.optionKey, newVal);
            SettingsManager::SetOption(item.optionKey.c_str(), newVal);
            RetroCore::SetOptionValue(item.optionKey.c_str(), newVal);
            if (onOptionChanged)
                onOptionChanged(item.optionKey.c_str(), newVal);
        }
    }
}

void FrontendMenu::OnConfirm()
{
    if (!m_visible) return;

    // Route to ConfirmDialog if visible
    if (m_confirmDialog.IsVisible())
    {
        m_confirmDialog.HandleKeyDown(0x0D); // VK_RETURN
        return;
    }

    // Route to AboutDialog if visible
    if (m_aboutDialog.IsVisible())
    {
        m_aboutDialog.OnConfirm();
        return;
    }

    // Route to FileBrowser if visible
    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.OnConfirm();
        return;
    }

    // Route to overlay if active
    if (m_overlayActive && m_overlayItems)
    {
        auto& items = *m_overlayItems;
        if (m_selected < 0 || m_selected >= (int)items.size()) return;
        auto& item = items[m_selected];
        if (!item.enabled) return;

        switch (item.action)
        {
        case MenuAction::GENERAL:
        case MenuAction::INPUT:
        case MenuAction::PERFORMANCE:
        case MenuAction::VIDEO:
        case MenuAction::SYSTEM:
        case MenuAction::AUDIO:
        {
            // Push current overlay onto stack, open deeper overlay
            m_overlayStack.push_back({ m_overlayTitle, m_overlayItems, m_selected, m_scrollOffset });
            const char* catName = "General";
            std::vector<MenuItem>* catItems = &m_generalItems;
            if (item.action == MenuAction::INPUT) { catName = "Input"; catItems = &m_inputItems; }
            else if (item.action == MenuAction::PERFORMANCE) { catName = "Performance"; catItems = &m_performanceItems; }
            else if (item.action == MenuAction::VIDEO) { catName = "Video"; catItems = &m_videoItems; }
            else if (item.action == MenuAction::SYSTEM) { catName = "System"; catItems = &m_systemItems; }
            else if (item.action == MenuAction::AUDIO) { catName = "Audio"; catItems = &m_audioItems; }
            spdlog::info("[FrontendMenu] OVERLAY PUSH: {} -> {}", m_overlayTitle, catName);
            m_overlayTitle = catName;
            m_overlayItems = catItems;
            m_selected = 0;
            m_scrollOffset = 0;
            break;
        }

        case MenuAction::BACK:
        {
            // Save settings when leaving a settings sub-screen
            if (m_overlayTitle == "General" || m_overlayTitle == "Input" || m_overlayTitle == "Performance" ||
                m_overlayTitle == "Video" || m_overlayTitle == "System" || m_overlayTitle == "Audio")
            {
                SaveCurrentSettings();
                ShowToast(L"Settings saved...");
            }
            if (!m_overlayStack.empty())
            {
                auto& prev = m_overlayStack.back();
                spdlog::info("[FrontendMenu] OVERLAY POP: {} -> {}", m_overlayTitle, prev.title);
                m_overlayTitle = prev.title;
                m_overlayItems = prev.items;
                m_selected = prev.selected;
                m_scrollOffset = prev.scrollOffset;
                m_overlayStack.pop_back();
            }
            else
            {
                spdlog::info("[FrontendMenu] OVERLAY CLOSE: {} -> panel", m_overlayTitle);
                m_overlayActive = false;
                m_overlayItems = nullptr;
                m_selected = m_panelSavedSelected;
                m_scrollOffset = m_panelSavedScrollOffset;
            }
            break;
        }

        case MenuAction::TOGGLE_VALUE:
            // History: load game — check BEFORE values.empty() since history items have no values
            if (m_overlayTitle == "History" && !item.optionKey.empty())
            {
                spdlog::info("[FrontendMenu] History: load {}", item.optionKey);
                m_overlayActive = false;
                m_overlayItems = nullptr;
                m_overlayStack.clear();
                m_visible = false;
                m_selected = m_panelSavedSelected;
                m_scrollOffset = m_panelSavedScrollOffset;
                if (onFileSelectedHistory)
                {
                    std::wstring wpath(item.optionKey.begin(), item.optionKey.end());
                    onFileSelectedHistory(wpath);
                }
                return;
            }
            if (!item.values.empty())
            {
                items[m_selected].currentValue =
                    (item.currentValue + 1) % (int)item.values.size();

                if (!item.optionKey.empty())
                {
                    const char* newVal = item.coreValues.empty() ? item.values[item.currentValue].c_str() : item.coreValues[item.currentValue].c_str();
                    spdlog::info("[FrontendMenu] TOGGLE: {} = {}", item.optionKey, newVal);
                    SettingsManager::SetOption(item.optionKey.c_str(), newVal);
                    RetroCore::SetOptionValue(item.optionKey.c_str(), newVal);
                    if (onOptionChanged)
                        onOptionChanged(item.optionKey.c_str(), newVal);
                }
            }
            break;

        case MenuAction::RESET_DEFAULTS:
        {
            spdlog::info("[FrontendMenu] RESET_DEFAULTS -> confirm (section={})", m_overlayTitle);
            std::string title = m_overlayTitle;
            m_confirmDialog.Open("Reset all " + title + " settings to defaults?",
                ConfirmDialog::CONFIRM, [this, title](bool confirmed)
            {
                if (!confirmed) return;
                spdlog::info("[FrontendMenu] RESET_DEFAULTS confirmed (section={})", title);
                std::vector<std::string> sectionKeys;
                std::vector<MenuItem>* targetItems = nullptr;
                if (title == "General") targetItems = &m_generalItems;
                else if (title == "Input") targetItems = &m_inputItems;
                else if (title == "Performance") targetItems = &m_performanceItems;
                else if (title == "Video") targetItems = &m_videoItems;
                else if (title == "System") targetItems = &m_systemItems;
                else if (title == "Audio") targetItems = &m_audioItems;
                if (targetItems)
                {
                    for (auto& item : *targetItems)
                        if (item.action == MenuAction::TOGGLE_VALUE && !item.optionKey.empty())
                            sectionKeys.push_back(item.optionKey);
                    m_overlayItems = targetItems;
                }
                SettingsManager::ResetSectionDefaults(sectionKeys);
                for (auto& key : sectionKeys)
                {
                    std::string val = SettingsManager::GetOption(key.c_str(), "");
                    RetroCore::SetOptionValue(key.c_str(), val.c_str());
                }
                BuildMenuTree();
                if (title == "General") m_overlayItems = &m_generalItems;
                else if (title == "Input") m_overlayItems = &m_inputItems;
                else if (title == "Performance") m_overlayItems = &m_performanceItems;
                else if (title == "Video") m_overlayItems = &m_videoItems;
                else if (title == "System") m_overlayItems = &m_systemItems;
                else if (title == "Audio") m_overlayItems = &m_audioItems;
                m_overlayTitle = title;
                m_selected = 0;
                m_scrollOffset = 0;
                m_confirmDialog.Open(title + " settings reset to defaults.", ConfirmDialog::INFO);
            });
            break;
        }

        case MenuAction::RESET_ALL_SETTINGS:
        {
            spdlog::info("[FrontendMenu] RESET_ALL_SETTINGS -> confirm");
            m_confirmDialog.Open("Reset ALL settings to defaults?\nThis cannot be undone.",
                ConfirmDialog::CONFIRM, [this](bool confirmed)
            {
                if (!confirmed) return;
                spdlog::info("[FrontendMenu] RESET_ALL_SETTINGS confirmed");
                SettingsManager::ResetToDefaults();
                BuildMenuTree();
                m_overlayTitle = "Settings";
                m_overlayItems = &m_settingsItems;
                m_selected = 0;
                m_scrollOffset = 0;
                m_confirmDialog.Open("All settings reset to defaults.", ConfirmDialog::INFO);
            });
            break;
        }

        case MenuAction::CLEAR_HISTORY:
        {
            spdlog::info("[FrontendMenu] CLEAR_HISTORY -> confirm");
            m_confirmDialog.Open("Clear all history items?\nThis cannot be undone.",
                ConfirmDialog::CONFIRM, [this](bool confirmed)
            {
                if (!confirmed) return;
                spdlog::info("[FrontendMenu] CLEAR_HISTORY confirmed");
                SettingsManager::ClearHistory();
                // Close history overlay back to main menu
                m_overlayActive = false;
                m_overlayItems = nullptr;
                m_selected = m_panelSavedSelected;
                m_scrollOffset = m_panelSavedScrollOffset;
                ShowToast(L"History cleared");
            });
            break;
        }

        default:
            break;
        }
        return;
    }

    auto& items = *m_stack.back().items;
    if (m_selected < 0 || m_selected >= (int)items.size()) return;
    auto& item = items[m_selected];
    if (!item.enabled) return;

    switch (item.action)
    {
    case MenuAction::OPEN_FILE:
        spdlog::info("[FrontendMenu] OPEN_FILE -> FileBrowser.Open()");
        m_fileBrowser.Open();
        break;

    case MenuAction::OPEN_PUREMENU:
        if (onOpenPuremenu) onOpenPuremenu();
        break;

    case MenuAction::OPEN_HISTORY:
    {
        auto& history = SettingsManager::GetHistory();
        m_historyItems.clear();
        for (auto& he : history)
        {
            m_historyItems.push_back({
                he.filename,
                MenuAction::TOGGLE_VALUE,
                {}, {}, {}, 0, true, he.fullPath
            });
        }
        if (m_historyItems.empty())
        {
            m_historyItems.push_back({
                "No history items yet",
                MenuAction::NONE,
                {}, {}, {}, 0, false
            });
        }
        m_historyItems.push_back({ "", MenuAction::NONE });
        m_historyItems.push_back({ "Clear History", MenuAction::CLEAR_HISTORY });
        m_historyItems.push_back({ "Back", MenuAction::BACK });
        m_panelSavedSelected = m_selected;
        m_panelSavedScrollOffset = m_scrollOffset;
        m_overlayActive = true;
        m_overlayTitle = "History";
        m_overlayItems = &m_historyItems;
        m_selected = 0;
        m_scrollOffset = 0;
        break;
    }

    case MenuAction::SETTINGS:
        m_panelSavedSelected = m_selected;
        m_panelSavedScrollOffset = m_scrollOffset;
        m_overlayActive = true;
        m_overlayTitle = "Settings";
        m_overlayItems = &m_settingsItems;
        m_selected = 0;
        m_scrollOffset = 0;
        break;

    case MenuAction::EXIT:
        spdlog::info("[FrontendMenu] EXIT -> confirm");
        m_confirmDialog.Open("Exit DOSBox Pure Unleashed?", ConfirmDialog::CONFIRM,
            [this](bool confirmed)
        {
            if (!confirmed) return;
            spdlog::info("[FrontendMenu] EXIT confirmed");
            m_visible = false;
            if (onExit) onExit();
        });
        break;

    case MenuAction::ABOUT:
        spdlog::info("[FrontendMenu] ABOUT -> AboutDialog.Open()");
        m_aboutDialog.Open(m_versionStr);
        break;

    default:
        break;
    }
}

void FrontendMenu::OnBack()
{
    if (!m_visible) return;

    if (m_confirmDialog.IsVisible())
    {
        m_confirmDialog.OnBack();
        return;
    }

    if (m_aboutDialog.IsVisible())
    {
        m_aboutDialog.OnBack();
        return;
    }

    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.OnBack();
        return;
    }

    // Close overlay — pop stack or close entirely
    if (m_overlayActive)
    {
        // Save settings when leaving a settings sub-screen
        if (m_overlayTitle == "Video" || m_overlayTitle == "Audio" || m_overlayTitle == "Core Options")
        {
            SaveCurrentSettings();
            ShowToast(L"Settings saved...");
        }
        if (!m_overlayStack.empty())
        {
            auto& prev = m_overlayStack.back();
            spdlog::info("[FrontendMenu] BACK overlay pop: {} -> {}", m_overlayTitle, prev.title);
            m_overlayTitle = prev.title;
            m_overlayItems = prev.items;
            m_selected = prev.selected;
            m_scrollOffset = prev.scrollOffset;
            m_overlayStack.pop_back();
        }
        else
        {
            spdlog::info("[FrontendMenu] BACK overlay close: {} -> panel", m_overlayTitle);
            m_overlayActive = false;
            m_overlayItems = nullptr;
            m_selected = m_panelSavedSelected;
            m_scrollOffset = m_panelSavedScrollOffset;
        }
        return;
    }

    if (m_stack.size() > 1)
    {
        m_stack.pop_back();
        m_selected = 0;
        m_scrollOffset = 0;
    }
    // else: root menu — B does nothing (no hide, no exit)
}

void FrontendMenu::OnPageUp()
{
    if (!m_visible) return;
    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.OnPageUp();
        return;
    }
    if (m_aboutDialog.IsVisible()) return;
    if (m_overlayActive && m_overlayItems)
    {
        m_selected -= (int)MAX_VISIBLE;
        if (m_selected < 0) m_selected = 0;
        m_scrollOffset -= (int)MAX_VISIBLE;
        if (m_scrollOffset < 0) m_scrollOffset = 0;
        return;
    }
    // Scroll menu items by MAX_VISIBLE
    m_selected -= (int)MAX_VISIBLE;
    if (m_selected < 0) m_selected = 0;
    m_scrollOffset -= (int)MAX_VISIBLE;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
}

void FrontendMenu::OnPageDown()
{
    if (!m_visible) return;
    if (m_fileBrowser.IsVisible())
    {
        m_fileBrowser.OnPageDown();
        return;
    }
    if (m_aboutDialog.IsVisible()) return;
    if (m_overlayActive && m_overlayItems)
    {
        auto& items = *m_overlayItems;
        m_selected += (int)MAX_VISIBLE;
        if (m_selected >= (int)items.size()) m_selected = (int)items.size() - 1;
        int visibleCount = (int)MAX_VISIBLE;
        if (m_selected >= m_scrollOffset + visibleCount)
            m_scrollOffset = m_selected - visibleCount + 1;
        return;
    }
    auto& items = *m_stack.back().items;
    m_selected += (int)MAX_VISIBLE;
    if (m_selected >= (int)items.size()) m_selected = (int)items.size() - 1;
    int visibleCount = (int)MAX_VISIBLE;
    if (m_selected >= m_scrollOffset + visibleCount)
        m_scrollOffset = m_selected - visibleCount + 1;
}
