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
panes, F1 opens the game menu, and F2 pauses/resumes time. F1 offers saving, loading,
waiting, restarting and quitting. Preferences remain in `config/`.

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

`Simulation::update` coordinates workday processing, discovery and conversations.
Current-story tests check new games and old-save retirement. Historical backend
and draw.io converter tests use `tests/fixtures/legacy-game`, never the playable
scenario. These retain coverage for permissions, work queues, delegation and saves.
