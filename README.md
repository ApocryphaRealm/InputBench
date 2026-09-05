# InputBench

**Version 0.1.1** (this build; there is no upstream - original code)

Press keys, gamepad buttons and mouse buttons, and move the mouse, *inside Skyrim's own input
dispatch* - driven from outside the game over HTTP. Every mod, every menu framework and the game
itself see the input exactly as if it came from hardware, and the game does not need to be the
foreground window.

Built on [DevBench](https://www.nexusmods.com/skyrimspecialedition/mods/181326): InputBench
registers one tool, `bench.input`, on DevBench's local REST/MCP server. If you can send an HTTP
request, you can press a key in the game.

## Why it exists

Automated testing of Skyrim mods needs a way to press things. The obvious route - Windows
`SendInput` from a script - is blocked by the foreground lock the moment the game is not the
active window, and it never reaches a game that reads raw input directly. InputBench instead
splices the event into the list the game hands to its own input dispatch every frame, so
`ControlMap`, `MenuControls`, ImGui-based menu frameworks and every mod's input sink receive it
the same way they receive a real keypress.

## Requirements

- SKSE64
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [DevBench](https://www.nexusmods.com/skyrimspecialedition/mods/181326) - the server the tool
  registers with. Without it InputBench loads and does nothing.

Built and tested on Skyrim SE 1.5.97. Addresses resolve through Address Library, so AE builds
are expected to work but are not verified here.

## Installation

Install with a mod manager like any SKSE plugin. The package is:

    SKSE\Plugins\InputBench.dll
    SKSE\Plugins\InputBench.pdb      (debug symbols, read by Crash Logger - keep them)

Load order does not matter. Confirm it is live by fetching `GET http://127.0.0.1:8920/api/tools`
(DevBench's default port - read the real one from
`Data\SKSE\Plugins\devbench\runtime.json`) and looking for `bench.input` in the list.

## Using it

Everything is `POST http://127.0.0.1:8920/api/tool/bench.input` with a JSON body.

### Press a key or button

    {"op":"press","device":"keyboard","code":59,"hold":4}

| Field | Meaning |
| --- | --- |
| `op` | `press` |
| `device` | `keyboard` (default), `gamepad` or `mouse` |
| `code` | keyboard: the **DirectInput scan code** (F1 = 59 / 0x3B, Enter = 28, Tab = 15, arrows = 200/208/203/205); gamepad: the XInput button mask; mouse: the button index (0 = left, 1 = right, 2 = middle) |
| `hold` | how many input dispatches (frames) the button stays down; default 4 |

The press goes down on the next dispatch, stays down for `hold` dispatches, then goes up.

### Move the mouse

    {"op":"move","dx":40,"dy":-12}

A relative move in display pixels, delivered as a real `MouseMoveEvent`. A framework that
integrates mouse deltas into its own cursor (Apocrypha Menu Framework does; the game recentres
the OS cursor every frame, so this is the only kind of move that reaches an in-game cursor)
moves by exactly this amount.

### Worked examples (PowerShell)

    $t = 'http://127.0.0.1:8920/api/tool/bench.input'
    # open a menu framework bound to F1
    Invoke-RestMethod -Method POST -Uri $t -ContentType 'application/json' -Body '{"op":"press","code":59}'
    # walk down a list and confirm
    foreach ($k in 208, 208, 28) { Invoke-RestMethod -Method POST -Uri $t -ContentType 'application/json' -Body ('{"op":"press","code":' + $k + '}'); Start-Sleep -Milliseconds 300 }
    # nudge the mouse and left-click
    Invoke-RestMethod -Method POST -Uri $t -ContentType 'application/json' -Body '{"op":"move","dx":25,"dy":0}'
    Invoke-RestMethod -Method POST -Uri $t -ContentType 'application/json' -Body '{"op":"press","device":"mouse","code":0}'

Every call returns immediately with `"queued":true`; the input lands on the game's next input
dispatch. Log lines in `Documents\My Games\Skyrim Special Edition\SKSE\InputBench.log` show each
spliced event.

### From an MCP client

DevBench exposes the same tool over MCP at `/mcp`; call `bench.input` with the same arguments.

## How it works

The game calls `BSInputDeviceManager::PollInputDevices` every frame with the list of input events
gathered from hardware. InputBench hooks that call and splices its own `ButtonEvent` /
`MouseMoveEvent` nodes onto the head of the list before the game processes it. That is the
whole trick: nothing is simulated at the OS level, so there is nothing for the foreground lock
to block, and the events carry the same `userEvent` name the ControlMap would assign to real
hardware so bindings resolve normally.

One thing deliberately *not* used, for anyone extending this: `BSInputEventQueue::GetSingleton()`
- on SE 1.5.97 its relocation returned a bad pointer and crashed. The dispatch-list splice needs
no queue at all.

## Building

CMake + vcpkg, the same as the rest of this project's plugins. `configure.bat` finds Visual
Studio with `vswhere`; set `VCPKG_ROOT` to your vcpkg checkout, then run `configure.bat` and
`build.bat`. The DLL lands in `build\relwithdebinfo-se-only\`.

## Licence

MIT (`LICENSE`). The DevBench C-ABI header and its companion `.cpp` are vendored under their own
MIT licence (`include\DevBench\DevBenchAPI.LICENSE.txt`) - that interface is licensed
separately from the DevBench plugin itself precisely so any plugin can talk to it. DevBench
(GPL-3.0) is a runtime requirement and is not linked.

## Debugging

Send `Documents\My Games\Skyrim Special Edition\SKSE\InputBench.log` with any bug report. It
ships at the most verbose level, so it already contains every spliced event.

## WHAT CHANGED

Version 0.1.1
Added a relative mouse move: op "move" with dx and dy, delivered as a real mouse-move event.
Version 0.1.0
First build: keyboard, gamepad and mouse button presses spliced into the game's input dispatch, exposed as the DevBench tool bench.input.
