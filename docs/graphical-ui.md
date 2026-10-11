# Qt workstation prototype

This is a separate Qt 6 / QML frontend, built against the existing C++ game core.
Messages, scripted responses, contact selection, CPU/RAM usage, process listings,
Mark trust, cash, pause, and save/resume are live game state. System remarks stay
out of chat, scripted choices send immediately, and incoming messages preserve
the selected contact. The timer advances at the scenario's playback speed and
stops while the menu is open. Responses are checked against the current story
node, so an old button cannot send a stale choice.

Only Messages and Resources are migrated. Files, agents, modifications, browser,
recovery management, and editable settings remain in the terminal frontend.
Their Qt tabs explicitly indicate that they are not implemented. Authored actions
that require those views are disabled with an explanation. This is a prototype to
evaluate the desktop layout, not a replacement for all current gameplay yet.

## Linux / WSL

Install Qt development tools and runtime QML modules:

```bash
sudo apt update
sudo apt install build-essential cmake git qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick-templates
bash scripts/build-gui.sh run --new
```

WSL needs WSLg (normally available on Windows 11) or an X server to display the
window. It launches directly as a desktop window; WezTerm is not required. Native
Windows builds can use Qt Creator with a Qt 6 kit and these CMake options:

```text
AI_SIM_BUILD_GUI=ON
AI_SIM_BUILD_TERMINAL=OFF
```

Run from the repository root so the TOML definitions can be found. Native
Windows packaging and an installer have not been implemented in this prototype.
If Qt is installed outside the system prefix, pass its installation directory
through `CMAKE_PREFIX_PATH`.

## Saves and controls

The default save directory is `saves/gui-prototype`; the terminal's autosave is
not overwritten. Start fresh with `--new`, or select a save directory with
`--save-directory PATH`. The prototype uses the same snapshot format, but keep
experiments separate until the remaining tools are migrated. Closing the window
saves to its own autosave slot. A save error is shown in the status strip.

Click contacts and response buttons. Esc or F1 toggles the workstation menu,
F2 pauses/resumes time, and Ctrl+S saves. These prototype shortcuts are currently
fixed; editable bindings and themes remain a migration task. The timeline follows
new messages until you scroll upward, then offers a Latest messages button.

## Validation

```bash
bash scripts/build-gui.sh test
```

The adapter test exercises real opening branches, company-chat connection,
resource data, stale-choice rejection, contact focus, menu pause/resume, and shared save/resume.
The offscreen software-rendering smoke check loads the actual QML window and
fails on QML warnings, a missing rendered frame, or broken menu, response-button,
and resource-tab bindings. It uses a temporary save folder. The normal ncurses build and its tests remain available through
`scripts/build-and-run.sh`.
