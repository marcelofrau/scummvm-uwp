# Controls — ScummVM UWP

Input reference for ScummVM UWP on Xbox / Windows. The shell is the ScummVM
libretro core running inside the RetroArch UWP frontend, so **all mappings are
the core's stock RetroPad defaults** — identical to the ScummVM core on any
RetroArch platform.

## RetroPad mapping (Xbox controller)

The ScummVM core converts RetroPad input into ScummVM keyboard/mouse events.
On the Xbox controller the RetroPad buttons are:

| Xbox input | RetroPad | ScummVM function |
|-----------|----------|------------------|
| Left Stick | Left Analog | Mouse cursor movement |
| D-Pad | D-Pad | Mouse cursor movement |
| **A** | RetroPad A | `SPACE` |
| **B** | RetroPad B | `RETURN` (Enter) |
| **X** | RetroPad X | `F5` — in-game menu |
| **Y** | RetroPad Y | `ESCAPE` |
| **LB** | RetroPad L | Left mouse button (click) |
| **RB** | RetroPad R | Right mouse button (click) |
| **RT** | RetroPad R2 | Cursor fine control (slow move) |
| **LT** | RetroPad L2 | Unmapped by default (`---`) |
| **Left stick click** | L3 | Unmapped by default (`---`) |
| **Right stick click** | R3 | Unmapped by default (`---`) |
| **Select** | RetroPad Select | Toggle virtual keyboard (VKBD) |
| **Start** | RetroPad Start | ScummVM GUI / launcher |
| Right Stick | Right Analog | Arrow keys (↑ ↓ ← →) |

Source of truth: `scummvm_mapper_*` defaults in
`backends/platform/libretro/include/libretro-core-options.h` of the bundled
core (`extern/scummvm`), matching the
[libretro ScummVM docs](https://docs.libretro.com/library/scummvm/).

## In-game usage

- **Click on verbs / hotspots** — move cursor with Left Stick or D-Pad, press
  **A** (Space) or **LB** (left click) to interact. The classic LucasArts verb
  bar works like the original: highlight a verb (e.g. "Walk to"), then click a
  hotspot.
- **Right click** — **RB**; games expose contextual actions or the "I can't"
  line (e.g. *The Dig*, *Curse of Monkey Island*) through it.
- **Open the game menu** — **X** (F5). Same menu as the desktop build: Save,
  Load, Options, Return to Launcher, Quit.
- **Cancel / back** — **Y** (Escape) closes dialogs, menus and the F5 overlay.
- **Confirm dialogs** — **B** (Enter).
- **Text input** (save-game names, game "talk" typing, the VKBD) — press
  **Select** to toggle the on-screen virtual keyboard. Type with the cursor;
  **A** presses the highlighted key, **B** confirms, **Y** cancels.
- **Fine cursor control** (pixel-precise clicking, e.g. pixel-hunt puzzles) —
  hold **RT** while moving the Left Stick. Speed drops to ~20% of normal by
  default (see options below).
- **Skip dialogue lines** — click or press **A**/**LB** repeatedly (engines
  usually skip on the next action; some need a key held, per game).

## Cursor behavior options

Configured in ScummVM launcher → **Global Options → Backend** (or the core
options in RetroArch):

| Option | Default | Note |
|--------|---------|------|
| `scummvm_gamepad_cursor_only` | disabled | Only the RetroPad drives the cursor; ignores physical mouse/touch. |
| `scummvm_gamepad_cursor_speed` | 1.0 | Cursor speed multiplier. 1.0 for 320×200/240 games, 2.0 for 640×400/480. |
| `scummvm_gamepad_cursor_acceleration_time` | 0.2 | Seconds to ramp to full speed (`off`, `0.1`–`1.0`). |
| `scummvm_analog_response` | linear | `linear` = 1:1; `quadratic` = more precise on small stick movements. |
| `scummvm_analog_deadzone` | 15 | Deadzone (%) to avoid cursor drift. Options: 0–30 in 5s. |
| `scummvm_mouse_speed` | 1.0 | Speed multiplier when using a real mouse. 0.05–3.0. |
| `scummvm_mouse_fine_control_speed_reduction` | 4 | Cursor speed while fine control is held: 2=50%, 4=20%, 10=10%. |

## Keyboard / mouse (USB)

ScummVM's desktop key bindings apply unchanged:

| Key | Function |
|-----|----------|
| **F5** | In-game menu |
| **Ctrl+F5** | Quick save |
| **Ctrl+U** | Mute / unmute |
| **Ctrl+Q** | Quit |
| **Ctrl+Alt+d** | Debug console (if built) |
| Mouse **left / right** | Left / right click |

Remap anything under **Global Options → Keymaps** — the mapping lives in
`scummvm.ini` (`[keymapper]` / per-game sections).

## Notes

- Input is delivered via RetroArch's RetroPad → the ScummVM core translates
  to keyboard/mouse events, so **core remapping inside RetroArch** (Quick
  Menu → Controls → Save Core Remap File) also works and persists per core.
- **Rumble / tilt / camera** are unsupported by the core (see
  [Features](https://docs.libretro.com/library/scummvm/) table).
