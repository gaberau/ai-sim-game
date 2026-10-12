# Code map and maintenance contracts

The game builds two libraries. `ai_sim_core` owns the simulation and persisted
world; `ai_sim_ui` owns the ncurses interface and links the core. `Source/main.cpp`
only parses startup options and starts the application. UI tests reuse the same
UI library as the executable instead of compiling separate renderer/settings copies.

## Ownership

`WorldState` contains the complete saved computer, network, files, people,
mailbox, pending events, scenario identity, random state and pause state.
`TerminalUI` owns one live world and borrows references to its fields. It bridges
existing gameplay actions to `RootUI`, which owns the terminal session, renderer,
children, navigation and focus. Each child owns its `PageState`; restoring a
world does not recreate the root or invalidate the bridge's references.

`ScreenRenderer` owns windows/pads/panels. Pages stage content, the root composes
one frame, and the renderer presents once. Its surfaces are destroyed before
`endwin`. `TextLayout` supplies shared prose wrapping and clipping; source file
previews deliberately preserve indentation and hard-wrap lines instead.

## Units and boundaries

- Simulation timestamps and authored delays use game seconds. `Simulation::advance`
  and `Computer::advance` accept a duration; subsystem event methods generally
  evaluate an absolute game clock. Playback speed converts wall time only in the UI loop.
- Process work uses core-seconds, CPU allocations use cores, agent budgets use
  TFLOP/s, and agent RAM/storage display values use decimal MB. Computer/file
  capacity is stored in bytes. Money, prices, fees and balances use integer cents.
- Reality belongs to people/relationships; only dossiers and observations expose
  evidence to the player. Archive filtering hides chats in the UI without changing
  their authored runs, discovery, incoming messages or history.
- Host reachability, identity and filesystem permissions are checked by the core.
  A UI preview cannot bypass access checks. Managed storage changes go through
  its agent, workload or backup owner rather than ordinary file editing.
- World saves stage snapshots before publishing a manifest. Definition migration
  stages new layouts and preserves originals. These paths remain compatibility code.

## Where changes belong

| Area | Implementation | Public contracts |
| --- | --- | --- |
| CPU, processes and clock | `Source/Computer*.cpp`, `ProcessFields.cpp` | `Computer.h`, `ProcessFields.h` |
| Network and file access | `Source/Network*.cpp`, `FileSystem.cpp` | `Network.h`, `FileSystem.h` |
| Definitions and saves | `DefinitionLoader.cpp`, `DataMigration.cpp`, `Systems/SaveSystem.cpp` | `DefinitionLoader.h`, `SaveSystem.h` |
| Conversations and knowledge | `Systems/ChatSystem.cpp`, `Systems/ConversationSystem.cpp`, `PeopleSystem.cpp` | Matching system headers |
| Work, review and delegation | `Source/Systems/*Work*.cpp`, `*Review*.cpp`, `DelegationSystem.cpp` | Matching system headers |
| Research and agents | `Source/Systems/UpgradeSystem.cpp`, `AgentSystem.cpp` | `UpgradeSystem.h`, `AgentSystem.h` |
| Income, market and infrastructure | Economy/PaidWork/Operations/Infrastructure systems and `StockMarketSystem.cpp` | Matching system headers |
| Terminal composition | `Source/ui/RootUI.cpp`, `ScreenRenderer.cpp`, `TextLayout.cpp` | `data/include/ai_sim/ui/` |
| Page interactions | `Source/ui/*View.cpp`, `PageInput.cpp`, `ActionMenus.cpp` | `TerminalUI.h`, `PageState.h` |

Every maintained implementation and header states its responsibility at the top.
Public API comments describe ownership, mutation, units and constraints. Local
comments explain non-obvious behavior; names and types explain straightforward
assignments and getters. Add documentation when changing a contract, not just
when adding a file.

## Cleanup boundaries

The retired Activity page, old chat-arrow/email-selection helpers and duplicate
agent dashboard are removed. Notifications still use bounded history, reachable
from the global menu. Mark's active definition is `data/people/mark.toml`, with
stable ID `engineer`; the old unreferenced base `people/engineer.toml` is removed.
The dossier profile at `people/profiles/engineer.toml` remains active.

Historical content under `Development Tools/tests/fixtures/legacy-game` is
required by regression tests. It never supplies active game conversations.
Old snapshot migrations, legacy authored tab IDs, and acceptance of the retired
`interface.activity_bar_width` key remain so existing saves/settings still load.
Player saves and local backups are data, not obsolete source files.

## Validation

Run `bash "Development Tools/scripts/build-and-run.sh" test` to build and run
C++ tests, Drawio conversion tests, and the real pseudo-terminal gameplay checks.
The terminal check copies authored data to an isolated folder, uses shortened
human reply delays there, and exercises archive/restore, minimum-size layouts,
text entry, research, income, trading, recovery and save retention.
