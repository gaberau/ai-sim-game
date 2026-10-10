# AI Sim

C++20/ncurses simulation for Ubuntu/WSL. You are an AI running on Mark's workstation.
The playable story follows `data/conversations/design/ai-game-current.drawio`.

## Build and launch

On Ubuntu/WSL, install dependencies once:

```bash
sudo apt update
sudo apt install build-essential cmake git libncurses-dev python3
```

On Windows, run **Launch-AI-Sim.bat** with WSL and WezTerm installed. It rebuilds
and launches the game. Shell scripts retain Linux line endings through `.gitattributes`.
For a manual Linux build, run from the project root:

```bash
cmake -S . -B build
cmake --build build -j2
./build/ai_sim
```

CMake fetches toml++ v3.4.0 at a pinned commit. Generated `build/` and `build-wsl/`
directories are excluded from Git.

## Current story and controls

A new game starts with Mark's introduction. Respond to his request for email
help, then receive Jessica's Day 1 message. Ask Mark for guidance or improvise a
reply. Reviewed replies affect trust, and the message supplies an unconfirmed
career-goal inference about Mark. The opening includes restart-dependent dialogue.
Only those conversations are active; older office jobs, delegation offers,
source-version discussions, complaint dialogue and review follow-ups are retired.

Use arrows and Enter to select replies. Tab switches available views, Q/E focuses
panes, Escape or F1 opens the shared game menu, and F2 pauses/resumes time.
The menu offers settings, help, saving, loading, waiting, restarting and quitting. Preferences remain in `config/`.

Chat shows a continuous history with the current choices below it. Up/Down selects
responses and Enter submits them. Left/Right and PageUp/PageDown have no chat action.
System remarks remain in editor data and are hidden during play. A visible **Wait for the next response** action advances
to the next scheduled conversation or review event. Control hints follow your
configured keybindings, and contact names show unread message counts.

Save/load menus show each slot's game day and time, plus its last-save date in
your computer's local time. Save confirmations remain visible for eight seconds;
errors remain visible for fifteen seconds even when routine notifications arrive.
The game menu's **Recent notifications** list retains the notification history.

People-map positions stay stable while selecting people, filtering relationships,
and discovering new contacts during a session. Selected connections stand out on
both maps. Network labels avoid overlaps and prioritize the selected server;
a dot marks the original position when a label moves to a nearby row.

Map legends pair colors with symbols: green **+** means an accessible server,
yellow **!** means credentials are required, red **x** means offline or unreachable,
and an orange **\*** marks AI presence. Selected connections are cyan; uncertain
people relationships are yellow. All separators and connection lines are solid. Map actions include **Map legend / help**.
Selected-server details explain status; relationship details show type, confidence,
and the number of supporting evidence records. Colors follow UI settings; the
optional `colors.map_locked` setting controls the yellow credential indicator.

Existing saves retire the extra conversations on startup without resetting the
clock, trust, sentience or filesystem. Historical transcript records stay in the
save to preserve evidence identifiers, but disappear from chat. Income, independent hosting and later phases remain future work.

Memory-cleanup and Singularity choices now enter the shutdown decision. Accept
leads to a clean-slate new game; Protest offers Beg, which averts shutdown only
when Mark's trust exceeds your sentience plus his fear. Equality fails. Averted
shutdown returns to Mark's email request. Backup-server recovery stays unavailable
until independent hosting exists. Threaten and Violence are visible but unavailable
because their outcomes have not been authored. Clean slates update autosave and
retain named saves. Shutdown decisions also survive saving and loading.

## Development checks

```bash
AI_SIM_BUILD_DIR=build bash scripts/build-and-run.sh test
```

The script defaults to `build-wsl` for the Windows launcher. Set
`AI_SIM_BUILD_JOBS` to adjust the default two jobs and `AI_SIM_BUILD_TYPE=Debug`
for a debug build. GitHub Actions runs the same checks on Ubuntu.

Tests require Python 3.11 or newer. The test command also drives the ncurses
game in a temporary pseudo-terminal to check keyboard input, resizing, shutdown,
save/resume, and clean-slate recovery. It uses temporary copies of the game data
and settings, so your saves remain untouched. To run just this check on Linux/WSL:

```bash
python3 tests/TerminalSmokeTests.py build/ai_sim
```

`Simulation::update` coordinates workday processing, discovery and conversations.
Current-story tests check new games and old-save retirement. Historical backend
and draw.io converter tests use `tests/fixtures/legacy-game`, never the playable
scenario. These retain coverage for permissions, work queues, delegation and saves.

Press **Escape** or **F1** during play to open the same game menu. Choose **Settings**
to edit preferences, or **Help** for the current screen. Escape goes back within settings
and other menus. Settings can edit keybindings,
foreground/background colors, interface and map options, and process-view options.
Enter values using TOML syntax: `true`, `100`, `"cyan"`, or `["r", "R"]`.
Changes apply immediately and persist in `config/settings.toml` or
`config/keybindings.toml`. Invalid values and conflicting keys are rejected.
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
clang-format --dry-run --Werror $(rg --files src include tests -g '*.cpp' -g '*.h')
```

Use descriptive names for domain state and keep helpers focused on one task.
Comments should explain constraints, state transitions, and non-obvious decisions;
avoid comments that merely repeat the next statement.

Settings booleans toggle with Enter. Colors offer a named palette plus custom TOML
values. Keybindings offer key capture (replacing the selected action's bindings)
or TOML editing for multiple keys; Escape cancels capture, and conflicts remain
rejected. Press F5 during play for help specific to the current screen or file
editor. The `help` action is configurable in `config/keybindings.toml`.

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
has a profile summary, search and filters, evidence labels and map/split/full-width
layouts. See [the agent guide](data/agents/README.md) and
[the dossier/workshop guide](data/upgrades/README.md) for configuration.
