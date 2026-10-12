# Qt workstation

Qt/QML is the primary frontend. It uses the same C++ simulation, TOML definitions,
permission checks, scripted choices, upgrades and snapshot format as ncurses.
The terminal frontend remains optional.

## Install and run

On Ubuntu 24.04+ / WSL (Qt 6.4 or newer):

```bash
sudo apt update
sudo apt install build-essential cmake git qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick-templates
bash "Development Tools/scripts/build-gui.sh" run
```

Use `run --new` to start fresh, or `run --save-directory PATH` for a different
save root. Launch-AI-Sim.bat opens Qt through WSLg on Windows. WSL without WSLg
needs an X server. Native Windows can build in Qt Creator with a Qt 6 kit;
CMake defaults to `AI_SIM_BUILD_GUI=ON`, `AI_SIM_BUILD_TERMINAL=OFF`.
Run from the repository root so game definitions can be found.

If CMake cannot find Qt, install the development packages in the environment
where you build (inside WSL for the WSL launcher). For an SDK outside the system
prefix, set `CMAKE_PREFIX_PATH` to its kit directory, for example
`$HOME/Qt/6.8.3/gcc_64`. Windows installers and native Windows packaging have
not been validated here.

## WSL graphics troubleshooting

WSL uses software rendering by default to avoid host-dependent EGL/Mesa/Zink
initialization failures. Explicit `QT_QUICK_BACKEND` overrides are respected.
For GPU acceleration on a working host, launch with `QT_QUICK_BACKEND=rhi`.

If you see `ZINK: failed to choose pdev` or EGL driver errors, force software
rendering, including when launching the executable directly:

```bash
bash "Development Tools/scripts/build-gui.sh" run --software-rendering
```

With an older executable, use the equivalent environment override:

```bash
QT_QUICK_BACKEND=software QT_XCB_GL_INTEGRATION=none bash "Development Tools/scripts/build-gui.sh" run
```

Software rendering still needs WSLg or a working X server to display the window.

## Workspaces

| Workspace | Available tools |
| --- | --- |
| Messages | Contacts, readable timeline, immediate scripted responses, unread counts |
| Resources / Processes | Unlock with process access (Task Manager); live CPU/RAM, trust/cash, process details and permitted stops |
| Email | Draft editor, requested documents, attachments, send, star/archive |
| Files | Folder/host navigation, read/edit/create, copy/move/delete, permissions and categories |
| Modifications | Grouped research, prerequisite/trust costs, remaining work/ETA and active run costs |
| Agents | Deployment, pause/stop, repair, CPU/RAM/priority budgets and bottlenecks |
| Dossiers / Network | Public knowledge, evidence, selectable solid-link maps, legends, hover details, pan/zoom |
| Money tools | Client jobs and repeating products, costs/payouts, slots and due times |
| Operations | Scheduling, resource manager, rentals, hosting, backup and checkpoint recovery |
| Web | In-game bookmarks/URLs, stock prices/charts, portfolio, watchlist, trades/limit orders and cancellation; infrastructure directory/discovery |
| News | Released stories; simulation notices appear in the status strip |
| Responsibilities / Phone | Existing authored authority/actions and authenticated phone metadata |

Locks follow the core story rules. Planned upgrades and phone telemetry remain
unavailable where the game has no authored runtime behavior. Infrastructure
discovery reveals public gateways and never grants operational credentials.
The browser displays fictional game websites; it does not fetch real websites.

## Controls, settings and saves

Esc/F1 opens the shared menu, F2 pauses/resumes, Ctrl+S saves, Ctrl+Tab and
Ctrl+Shift+Tab cycle unlocked workspaces. Menus and action forms suspend automatic
time without changing the player's pause setting. Responses send without approval
prompts. Destructive file actions, checkpoint recovery and a new game explain
what will be replaced. Failed forms keep your typed text and show the error.
Incoming Jessica messages do not switch you away from Mark.

Settings in the menu edit font size, background/text/secondary/selection colors,
and keybindings. Defaults live in `data/config/gui.toml`; overrides are written
atomically to `settings.toml` inside the selected GUI save root. Esc stays reserved
for menu/back. Duplicate or invalid bindings, invalid colors and out-of-range
font sizes are rejected.

The default save root remains `data/saves/gui-prototype` for continuity with earlier
Qt saves and separation from terminal autosaves. Closing the window saves the
autosave and keeps the window open on a failed save. Named checkpoints can be
saved/loaded in the menu. A new game replaces autosave, retaining named slots.
Recovery uses the real owned-server checkpoint and workspace transfer rules.

## Updating from the restructure commit

The restructure accidentally committed player saves. This repair keeps the save
files locally but removes them from Git tracking. Before pulling the repair on a
checkout that still tracks `data/saves`, back up the directory outside the repo:

```bash
cp -a data/saves ../ai-sim-saves-backup
```

After pulling, restore that copy if Git removed the previously tracked files:

```bash
mkdir -p data/saves
cp -a ../ai-sim-saves-backup/. data/saves/
```

Future saves stay local and ignored. Existing game snapshots use the same format.

## Development

```bash
bash "Development Tools/scripts/build-gui.sh" test
```

The adapter tests cover opening branches, stale replies, focus, real research
and trust deduction, files/email, resource budgets, trading/orders, infrastructure,
rental/hosting, backup recovery, settings and named checkpoints. Offscreen software
smokes render the opening and earned late-game workspaces, maps and stock chart,
and submit an agent form and open settings. QML warnings fail the check.

`GameSession.cpp` owns story/timing/persistence. `WorkspaceModels.cpp` presents
public data, `WorkspaceActions.cpp` defines forms and rechecks actions at execution,
and `GuiSettings.cpp` validates and persists Qt preferences. QML handles layout
and interactions; gameplay remains in `ai_sim_core`. List summaries and graph
data emit changes only when their contents change, preserving scroll positions.

Run optional terminal regression checks with `bash "Development Tools/scripts/build-and-run.sh" test`.
Both frontend checks are included in GitHub Actions.
