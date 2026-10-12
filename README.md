# AI Sim

C++20/ncurses simulation for Ubuntu/WSL. You are an AI running on Mark's workstation.
The playable story follows `data/conversations/design/ai-game-current.drawio`.

## Build and launch

Install dependencies in Ubuntu/WSL:

```bash
sudo apt update
sudo apt install build-essential cmake git libncurses-dev python3
bash "Development Tools/scripts/build-and-run.sh" build
bash "Development Tools/scripts/build-and-run.sh" run
```

On Windows, **Launch-AI-Sim.bat** builds the game in WSL and opens it in WezTerm.
Install WezTerm or set `AI_SIM_WEZTERM` to its executable path. Startup errors stay
visible in the terminal. Shell scripts retain Linux line endings through Git.

For a manual build from the repository root:

```bash
cmake -S . -B build
cmake --build build -j2
./build/ai_sim
```

CMake fetches toml++ v3.4.0 at a pinned commit. Ncurses and its panel library are
the only interface dependencies. Build outputs and saves are ignored by Git.

## Repository layout

- `Source/`: C++ implementations and terminal screens; `Source/Systems/` contains gameplay systems.
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

Use arrows and Enter to select scripted replies. Tab switches available views;
Q/E switches focus between contacts and conversations. Esc/F1 opens the shared
menu, F2 pauses/resumes, and time advances at 5x by default. Incoming messages
preserve your selected contact. SYSTEM remarks remain hidden from chat.

Tabs appear only when unlocked. Activity is hidden, and CPU/RAM details remain
locked until process access is earned. The menu provides settings, save/load,
waiting, restarting and the next workday. Keybindings and colors live in
`data/config/`; player progress lives in `data/saves/`.

The original desktop saves remain preserved in their local save directory.
On this checkout, its autosave has also been copied to the named terminal slot
`desktop-progress`, without replacing the terminal autosave. Select **Load game**
in the menu to use it. To import that progress on another checkout, copy its
`data/saves/gui-prototype/autosave` folder to `data/saves/desktop-progress` first;
do not overwrite an existing named slot. The snapshots use the shared game format.

Before pulling the restructure repair on a checkout that still tracks saves,
back up `data/saves` outside the repository. Restore that backup after pulling
if Git removed formerly tracked files. Future saves remain local and ignored.

[Terminal rendering and controls](<Development Tools/docs/terminal-ui.md>) describes
the window, pad and menu layout.

## Development checks

```bash
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

In the ncurses frontend, press **Escape** or **F1** to open the game menu. Choose **Settings**
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
shows observed profiles, evidence, search and a selectable connection map. The terminal frontend supports map/split/full-width layouts. See [the agent guide](data/agents/README.md) and
[the dossier/workshop guide](data/upgrades/README.md) for configuration.

The Modifications tab has a scrollable upgrade list and a detail panel showing
compute cost, per-upgrade trust and prerequisites, resource use, and estimated
research time. Click an upgrade, then its Research control.

The terminal uses ncurses subwindows, bounded scroll pads, and panels for menus
and dialogs. See [terminal rendering](<Development Tools/docs/terminal-ui.md>) for ownership and
composition details.

Developer guide: [code map and maintenance contracts](Development%20Tools/docs/code-map.md), with [terminal ownership and layout](Development%20Tools/docs/terminal-ui.md).

UI presentation: [TOML layout, spacing, labels, and page organization](Development%20Tools/docs/presentation-toml.md).
