# AI Sim

C++20/ncurses simulation for Ubuntu/WSL. You are a hobbyist AI running on an
engineer’s workstation. Start with a conversation, earn responsibility through
useful work, and discover people and systems as you encounter them.

## Apply and launch

Extract this source update over your existing project, replacing matching files.
Configuration, saves and your working WezTerm launcher are retained. Run
**Launch-AI-Sim.bat** as usual. Explicit view reveals persist in a separate visibility ledger. Saves from older builds initialize that ledger to Chat only without resetting world progress; reopen views through authored choices. To see Mark’s Day 1 opening, use **F1 → Start a new game → Start new game**. Existing conversations keep their saved graph. See **UPDATE-PHASE-ONE-WORK.md** for connected requests, deadlines, Mark’s work queue, and contextual Files/Network/AI reveals. See **UPDATE-REPLY-CONSEQUENCES.md** for recipient reactions, complaints, Mark’s follow-up, and approval gates. See **UPDATE-JESSICA-REVIEWS.md** for Jessica’s first conversation, sourced dossier inference, and delayed Mark reviews. See **UPDATE-REVISED-OPENING.md** for the current opening and restart ranges. See **UPDATE-CENTERED-OPENING.md** for the minimal opening and interface transition. See **UPDATE-CHARACTERS-CHAT.md** for the chat layout, controls and persistent character state. See **UPDATE-BASIC-RESPONSES.md** for the latest Day One response branches and **UPDATE-DAY-ONE.md** for the introduction and shutdown story.

Manual Ubuntu/WSL build from the project root:

```bash
sudo apt install build-essential cmake git libncurses-dev
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/ai_sim
```

CMake fetches toml++ v3.4.0 on first configuration. Run from the project root.
The existing Windows launcher uses build-wsl/.

## Opening and navigation

A new game shows only Chat. Time stays at the start of the day while the engineer
introduces you. Select an answer and press Enter. Files becomes available first,
then company chat and Network. The clock begins running after connecting.
Other views appear only when explicitly revealed by a player choice. System access, contacts and completed work do not reveal tabs. News and Processes can be revealed through authored choices in the engineer conversation after connecting.

| Default key | Action |
| --- | --- |
| Tab / Shift+Tab | Next / previous available view |
| Up / Down | Select responses, people or actions; scroll a focused detail pane |
| Left / Right | Previous/next incoming chat message; file category; map/column navigation |
| Q / E | Focus the left / right pane |
| Enter | Select the displayed response or open the selected item’s actions |
| Escape | Close a menu or cancel a text dialog |
| F1 | Game menu: save, load, wait, new game, quit |
| F2 | Pause / resume the workday |
| PageUp / PageDown | Scroll the current view |
| Space | Expand processes/dossiers or open a file |
| F3 / F4 | Keep / discard a file-editor draft |

Bindings remain in config/keybindings.toml. Stock WASD aliases and the stock Q toggle are migrated in memory to arrows and Q/E without overwriting your settings. Custom bindings remain configurable. Letters remain ordinary text in dialogs and editors.
Menus and text dialogs suspend automatic progression. The workday runs from
9 AM to 5 PM. Game actions can wait for a response or start the next workday.

## Calendar

The header shows weekday, date and clock from the opening onward. The scenario's `start_date = "2026-01-05"` is a configurable ISO date. The Gregorian calendar includes correct month lengths, leap years and century exceptions. Start next workday advances one calendar day, including weekends, from 5 PM to 9 AM. Overnight hours are skipped; the existing simulation clock remains working seconds so pending work retains its original timing. The date anchor and day index persist in computer.toml. Old saves without an anchor use the default scenario era; they have no historical date to recover.

## Chat and the first request

Chat starts with one full-width conversation pane. Connecting company chat keeps that layout until you choose “Show me the company contacts.” After revealing the sidebar, company chat shows encountered contacts on the left and a selected incoming message with nearby replies on the right, with its small authored response set immediately below it. Q focuses contacts; E focuses responses. Up/Down selects; Left/Right browses incoming messages. PageUp/PageDown reads a long message. Enter responds directly. There is no generic conversation-action menu.

1. Answer Yes to Mark’s request during the opening, choose “Show me the company contacts.”, then use F1 → Wait → One minute.
2. Select Casey. Choose investigation, clarification, or an unchecked five-minute
   delivery promise. Clarification waits for Casey's work schedule.
3. After inspecting the file, request approval or send a factual blocked update.
4. Wait for engineer review and the document owner's decision.
5. Once access is granted, share the current file and await acknowledgement.
6. Select the engineer for the responsibility offer. Review scope, request a
   human decision, or decline. Existing authority rules still govern approval.

Promises are persistent commitments, fulfilled only by acknowledgement before
their deadline. Missing one reduces the engineer's and colleague's confidence
once; a late delivery completes the task without erasing that history. Long waits
produce one follow-up. Context above chat tracks unresolved work and commitments.
Each request, clarification and human update has contextual choices. Non-graph messages use the small authored sets in responses.toml. Existing saved drafts get explicit send/review/discard choices.

Network reachability, credentials and actual file access still govern sending.
Failed sends preserve drafts. All messages and file operations are simulated.
Authored conversation graphs and work-request rules supply human responses.

After an acknowledged delivery, AI management becomes available. Ask the engineer
to delegate routine replies and wait for review. Decisions use demonstrated
results, workload, preferences and explicit boundaries. Nonroutine questions and
access failures return to the player. Pause/resume and retry remain available.

## Views

**Files:** three categories, Work, Personal and Hidden. Left/Right switches category; Q selects files and E reads content. Create, edit, copy, move, delete and share simulated files through contextual actions. New files go into the chosen category. Changing a category keeps file identity, contents and access. Hidden organizes files; it does not change who may read them.
Configurable access colors accompany short labels: Read + edit, Read only,
Ask owner, Blocked and Unavailable. Sharing choices are Private, Team read,
Team read/edit and Everyone read. Changing sharing requires ownership or admin
rights. Low-level permission masks remain internal.

**Network:** connected hosts, routes and details. Q/E focuses map/details;
zoom, navigation and filesystem actions retain their access checks.

**People:** relationship map and expandable dossiers. Q/E focuses map/dossier, Space
expands a selection and Enter opens actions. Observed chats provide evidence and
communication links. Private motivations are not automatically revealed.
See data/people/README.md.

**AI / Activity:** work requests, human queues, granted responsibilities and
recorded outcomes. Related requests open their conversation. Agent creation and
upgrades remain planned.

**Processes:** optional inspection with expandable resource columns, real units
and configurable layouts. Protected/stoppable/player colors remain.

**News:** authored scenario stories and This day in history. The clock-row ticker
appears after revealing News. No live web browsing is implemented.

## Definitions, preferences and saves

| Folder | Purpose |
| --- | --- |
| config/ | Colors, layout, process columns and bindings |
| data/conversations/ | TOML conversation graphs and draw.io example |
| data/people/ | Person definitions and dossier rules |
| data/workflows/ | Work requests and delegation definitions |
| data/scenarios/ | Scenario entry points |
| saves/ | Persistent world snapshots |

See data/README.md for the full folder map. The default scenario is
data/scenarios/hobbyist.toml. See data/conversations/README.md for authoring and
conversion. A companion opening.toml is loaded for new games when present beside
the scenario’s people-definition directory. Custom scenarios without it retain
the full interface.

Conversation state, flags, transcript, response ledgers and authored definition snapshots live in people.toml.
Editing a graph affects new games; it does not reset ongoing conversations.
Missing graph IDs in first_workday.toml are installed additively at startup.
Existing saves migrate without replaying the opening. Legacy message records
still live in emails.toml internally, preserving drafts, attachments, automation
and work-request references. Email is no longer a UI tab.

Startup additively installs missing delegation definitions and follow-up messages,
without replacing decisions or duplicating records. Data edits generally affect
new games; saved rules remain intact. Game can reload UI settings and News.
Older flat definitions are backed up before migration.

Optional additions inside config/settings.toml’s existing [colors] table:

```toml
file_edit = "green"
file_read = "cyan"
file_approval = "orange"
file_blocked = "red"
file_unknown = "gray"
```

Missing roles receive defaults; existing settings remain valid.

## Verification and current limits

Eleven CTest suites cover chat opening/migration, people, delegation, definitions,
workflows, resources, message transport and saves/permissions. Terminal checks
exercise opening/resume, colored access, request/approval/share/acknowledgement,
authored responses, file creation, pane controls and legacy migration at 120x40 and 80x24. Workday checks cover
clarification/resume, approvals, delivery, the responsibility offer/review and
missed commitments. The draw.io converter is checked for both workday pages,
compressed XML and safe append behavior. Run `python3 tests/DrawioTests.py`
with Python 3.11+ to repeat the four converter tests. The working WezTerm
launcher is preserved; Windows window launching cannot be tested here.

This remains an authored simulation. Agent creation, self-modification, live
browsing and infrastructure expansion are future work. Transfers do not yet
consume simulated CPU or transfer time. Reassuring statements record intentions;
demonstrated outcomes establish reliability.

Latest update: [Delegation policies and source versions](UPDATE-DELEGATION-POLICIES.md).
