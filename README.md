# AI Sim

C++20/Qt simulation for Ubuntu/WSL, with an optional ncurses frontend. You are an AI running on Mark's workstation.
The playable story follows `data/conversations/design/ai-game-current.drawio`.

## Build and launch

On Ubuntu 24.04+ / WSL, install dependencies once:

```bash
sudo apt update
sudo apt install build-essential cmake git python3 qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick-templates
bash "Development Tools/scripts/build-gui.sh" run
```

On Windows, **Launch-AI-Sim.bat** now opens the Qt workstation through WSLg.
It builds automatically and resumes the GUI autosave. WezTerm is needed only for
the optional terminal interface (`& "Development Tools/scripts/launch.ps1" -ProjectRoot PATH -Terminal`).
Qt 6.4 or newer is required. See [Qt setup and controls](<Development Tools/docs/graphical-ui.md>).

CMake builds Qt by default; build/run manually from the repository root:

```bash
cmake -S . -B build-gui
cmake --build build-gui --target ai_sim_gui -j2
./build-gui/ai_sim_gui
```

The ncurses build remains available with `AI_SIM_BUILD_GUI=OFF` and
`AI_SIM_BUILD_TERMINAL=ON`, or through `Development Tools/scripts/build-and-run.sh`.
CMake fetches toml++ v3.4.0 at a pinned commit. Build directories are ignored.

## Repository layout

- `Source/`: C++ implementations and Qt/QML screens; `Source/Systems/` contains gameplay systems.
- `data/include/`: C++ headers consumed by the compiler.
- `data/`: authored TOML content, `data/config/` preferences, and local `data/saves/` progress.
- `Development Tools/`: build/launch scripts, tests, converters, and documentation.

Build outputs and saves are ignored by Git. Run commands from the repository root;
the shell scripts also resolve that root when launched from another directory.

## Current story and controls

A new game starts with Mark's introduction. Respond to his request for email
help, then receive Jessica's Day 1 message. Ask Mark for guidance or improvise a
reply. Reviewed replies affect trust, and the message supplies an unconfirmed
career-goal inference about Mark. The opening includes restart-dependent dialogue.
Only those conversations are active; older office jobs, delegation offers,
source-version discussions, complaint dialogue and review follow-ups are retired.

In Qt, select contacts and scripted responses with the mouse. New messages preserve
your selected contact; SYSTEM remarks remain hidden. Scroll the workspace strip
or use Ctrl+Tab / Ctrl+Shift+Tab to switch unlocked views. Esc/F1 opens the shared
menu; F2 pauses time and Ctrl+S saves. Time advances at 5x by default and stops
while menus or editors are open.

The menu provides settings, named save/load checkpoints, waiting, a new game,
and the next workday. Modifications and money tools also have menu shortcuts.
Keybindings, font size, and colors are editable there and in TOML. GUI saves stay
in `data/saves/gui-prototype` to preserve existing terminal saves and earlier Qt progress.
You can select another save folder with `--save-directory PATH`.

The Files and Email workspaces provide editors, document attachments and replies.
Agents expose deployment, pause/stop, resource budgets, repair and shortages.
Dossiers and Network have selectable maps with solid connections, legends,
hover details, pan/zoom, and a readable detail panel. Stock trading lives in the
Web browser, including portfolio, watchlist, price charts and limit orders.
Infrastructure is the browser's second bookmark. Operations handles scheduling,
remote compute, independent hosting, backups and recovery. Existing story gates,
trust costs, fees, permissions and TOML rules apply to both interfaces.

[Terminal controls and rendering](<Development Tools/docs/terminal-ui.md>) remain available for the
optional ncurses frontend.

## Development checks

```bash
bash "Development Tools/scripts/build-gui.sh" test
AI_SIM_BUILD_DIR=build bash "Development Tools/scripts/build-and-run.sh" test
```

The script defaults to `build-wsl` for the Windows launcher. Set
`AI_SIM_BUILD_JOBS` to adjust the default two jobs and `AI_SIM_BUILD_TYPE=Debug`
for a debug build. GitHub Actions runs the same checks on Ubuntu.

Tests require Python 3.11 or newer. The test command also drives the ncurses
game in a temporary pseudo-terminal to check keyboard input, resizing, shutdown,
save/resume, and clean-slate recovery. It uses temporary copies of the game data
and settings, so your saves remain untouched. To run just this check on Linux/WSL:

```bash
python3 "Development Tools/tests/TerminalSmokeTests.py" build/ai_sim
```

`Simulation::update` coordinates workday processing, discovery and conversations.
Current-story tests check new games and old-save retirement. Historical backend
and draw.io converter tests use `Development Tools/tests/fixtures/legacy-game`, never the playable
scenario. These retain coverage for permissions, work queues, delegation and saves.

For the optional ncurses frontend, press **Escape** or **F1** to open the game menu. Choose **Settings**
to edit preferences, or **Help** for the current screen. Escape goes back within settings
and other menus. Settings can edit keybindings,
foreground/background colors, interface and map options, and process-view options.
Enter values using TOML syntax: `true`, `100`, `"cyan"`, or `["r", "R"]`.
Changes apply immediately and persist in `data/config/settings.toml` or
`data/config/keybindings.toml`. Invalid values and conflicting keys are rejected.
Escape remains reserved for menu/back; selection and submit keys cannot be empty.
The first edit preserves the original file as `.bak`. Files are rewritten as
formatted TOML; original comments remain in the backup.

Additional `[interface]` options include notification durations, opening text
width, people/network panel widths, map label length, and the uncertain-relationship
threshold. Existing TOML files still work with default values. Simulation and
story parameters remain in `data/`; UI settings do not grant story permissions.

C++ source, headers, and tests use the repository `.clang-format` policy
(clang-format 18.1.8). Check formatting before submitting changes:

```bash
rg --files Source data/include "Development Tools/tests" -g '*.cpp' -g '*.h' -0 | xargs -0 clang-format --dry-run --Werror
```

Use descriptive names for domain state and keep helpers focused on one task.
Comments should explain constraints, state transitions, and non-obvious decisions;
avoid comments that merely repeat the next statement.

In ncurses, settings booleans toggle with Enter. Colors offer a named palette plus custom TOML
values. Keybindings offer key capture (replacing the selected action's bindings)
or TOML editing for multiple keys; Escape cancels capture, and conflicts remain
rejected. Press F5 during play for help specific to the current screen or file
editor. The `help` action is configurable in `data/config/keybindings.toml`.

Paid work is available under the unlocked **Money tab**, with balance and running
jobs also shown in AI management. Coding contracts provide the first outside
income; data cleanup, research reports, software sales, and hosted services have
additional unlock requirements. Mark's work remains unpaid. All tools require
Mark's explicit self-modification authorization. In the **Modifications tab**, request more capacity after reaching trust 5. Costs, durations, payouts, cooldowns, capacity, and prerequisites are
editable in [data/economy/tools.toml](data/economy/tools.toml); see
[data/economy/README.md](data/economy/README.md) for story integration and save behavior.

The simulated stock market is under **Web tab -> Sim Exchange bookmark**.
Buy and sell whole shares using earned cash, with quantity entry and confirmation.
The portfolio shows holdings, cost basis, and realized/unrealized profit or loss.
Trading requires both `self_modification` and `stock_trading`; current story unlocks
remain unchanged. Fictional companies, price bands, quote timing, seed, and trading
fees are editable in [data/economy/stocks.toml](data/economy/stocks.toml).

The Web tab unlocks by building Web Browsing in the self-modification workshop.
Network Browsing separately unlocks the Network tab at Mark trust 15. Its home page has
bookmarks and a URL entry control. Open the Sim Exchange bookmark or enter
`https://exchange.sim` to visit the stock-market website. Quotes update on the
page while simulation time runs; trading forms pause time like other dialogs.
Website address and title are configurable in `data/economy/stocks.toml`.

The **Web tab -> City Infrastructure Directory** (`https://city.sim/infrastructure`)
covers power, water, sewage, food, shipping, and internet. Inspect facilities,
operators, capacities, and dependencies, then discover public gateways on the
Network map. Operations networks stay hidden and restricted for later infiltration
gameplay. Edit [data/infrastructure/city.toml](data/infrastructure/city.toml) and
[data/networks/infrastructure.toml](data/networks/infrastructure.toml); see the
[infrastructure guide](data/infrastructure/README.md) for fields and save behavior.

The workshop uses actual CPU allocations to build upgrades, including contention.
Agent Delegation unlocks Agents; researched agents can run or stop with their
configured CPU, RAM and persistent installation storage costs. Task Manager,
Files, Dossiers and Phone unlock their corresponding views. Trust 22 grants
administrator access, and trust 30 plus Phone Access research authenticates
Mark's phone. Phone telemetry and unconnected camera/microphone upgrades remain
unavailable. See [data/upgrades/README.md](data/upgrades/README.md).

The **Agents** dashboard provides deployment, pause/resume/stop, CPU limits, RAM
budgets, priorities and installation repair. Resource shortages create saved
requests with cooldowns and recovery notifications. The **Dossiers** view now
shows observed profiles, evidence, search and a selectable connection map. The
optional terminal frontend also supports map/split/full-width layouts. See [the agent guide](data/agents/README.md) and
[the dossier/workshop guide](data/upgrades/README.md) for configuration.

The Modifications tab has a scrollable upgrade list and a detail panel showing
compute cost, per-upgrade trust and prerequisites, resource use, and estimated
research time. Click an upgrade, then its Research control.

The terminal uses ncurses subwindows, bounded scroll pads, and panels for menus
and dialogs. See [terminal rendering](<Development Tools/docs/terminal-ui.md>) for ownership and
composition details.

The [Qt/QML workstation](<Development Tools/docs/graphical-ui.md>) is the primary interface. The
ncurses frontend remains available for terminal play and regression checks.
