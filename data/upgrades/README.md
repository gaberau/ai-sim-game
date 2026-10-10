# Self-modification workshop

Open the unlocked **Modifications tab** and press Enter for research controls. Request more capacity from Mark after connecting and reaching trust 5. This records the capacity request and authorizes self-modification without changing trust or paying the AI.

`tree.toml` defines labels, prerequisite IDs, build costs, Mark trust thresholds, capabilities and tabs. Research creates a real finite CPU process. `tflops_per_core_second` converts allocated core seconds to TFLOP work; CPU contention slows research. Only one build runs at a time. Saves retain the catalog, completed upgrades, pending research and actual consumed CPU work. Close menus to advance time.

Web Browsing unlocks Web, Network Browsing unlocks Network, Basic File Browsing unlocks Files, Task Manager unlocks Processes, Dossiers unlocks Dossiers, Agent Delegation unlocks Agents and Phone Access unlocks Phone. Network Browsing requires trust 15; basic file browsing requires 10; advanced file browsing requires 22; Phone Access requires 30. The chart supplies no build cost for Network Browsing or Advanced File Browsing, so their configured cost is zero.

The `[gates]` table records Mark's earned permissions: email 1, local computer 10, intranet 15, administrator 22 and phone 30. Administrator permission changes the actual computer identity to root. HR permission additionally requires intranet access and Becky trust 10; no Becky conversations or trust gains are invented. Existing story-authored unlocks and reveals remain compatible. Current email/chat transport is still governed by the existing story and permissions; the new email permission is recorded for future explicit email UI.

Build agents and then use **Run/Stop** in the workshop. They consume their specified CPU and RAM and reserve compact, persistent allocation files under the configured workspace's `agents/` folder. Stopping frees RAM/CPU; installation storage stays reserved. Autochat follows only Jessica's authored ask-Mark, comply and send choices. The dossier agent updates existing observations. The task agent only stops eligible processes explicitly listed in `unneeded_processes`, respecting computer permissions; its scrub timestamp records completion for future process-log exposure mechanics. Hidden chat tracks reduces optional message-review exposure by `hidden_review_multiplier`; important messages retain their mandatory review. Authoritative story history remains intact.

Phone access authenticates Mark's existing phone host. Text/2FA, GPS and battery telemetry need authored data before becoming playable. Camera/microphone monitoring remains disabled because the chart does not connect its hardware prerequisites. Stock-account and job-posting prerequisites remain unchanged because those chart nodes are unconnected.

## Resource management

The Agents tab now has a dedicated dashboard and deployment/resource controls.
See [the agent guide](../agents/README.md) for budgets, priorities, shortage
requests, waiting/recovery and repair. Research progress and ETA use the actual
allocated CPU; ETA can change when competing workloads start or stop.

## Dossier reading

The Dossiers tab offers map, split and full-width dossier layouts, person search,
recent/uncertain filters, a profile summary and recorded evidence links. Claims
are labeled Confirmed, Reported or Uncertain; new/changed facts and conflicting
evidence are highlighted. `[dossiers]` controls `default_layout`,
`highlight_window_seconds`, `recent_window_seconds` and `uncertain_confidence`.
Only discovered facts and relationships are shown.

Upgrade `group` fields in `tree.toml` organize the Modifications list into Agents,
Privacy, Software upgrades, Physical access, and Monitoring. Groups appear in
catalog order; Up/Down selects upgrades and skips headings. Older saves receive
missing group metadata while keeping their research progress. Custom group labels
are supported.

Each item has an editable `trust_cost`, deducted once when research starts.
`mark_trust` remains a separate access requirement. Failed purchases do not charge
trust, and saved research never charges twice. Earned permissions stay unlocked.
The detail panel shows trust balance and CPU/RAM/storage run costs for agents.

## Scheduling and independent infrastructure

Research Task Scheduler, Resource Manager, Remote Compute, Independent Hosting,
and Backup and Recovery in order after Task Manager. These unlock the Operations
tab. The scheduler runs client jobs in priority order, respecting work slots,
prerequisites and cooldowns. Canceling a schedule lets its current paid job finish.
Repeating jobs restart after their cooldown while time advances.

Deploy the Resource Manager from Operations or Agents. It spends its configured
CPU and RAM to protect `operations.reserve_cores`, prioritize agents, redistribute
RAM, and pause/resume agents for shortages. Manual pauses remain under your control.
All research, trust and runtime costs remain editable in `tree.toml`.

`[operations.plans.rental]` and `[operations.plans.hosting]` configure CPU, RAM,
server storage, purchase price, recurring upkeep and working-time billing period.
The defaults rent two cores for $5/hour and buy four cores for $100 plus $2/hour.
The first upkeep period is prepaid. Active services add to the simulated CPU/RAM
pool and appear as accessible server nodes; their disk storage belongs to those
hosts. Failed renewal removes their capacity and can stop agents that no longer
fit in RAM. Canceling a lease does not refund its prepaid period. Owning a server
records the phase-one independence goal; research alone does not buy it.

Backups cost $1 and reserve 128 MB on the owned server by default. Operations saves
an atomic recovery checkpoint in the game save directory. Keep that directory
when moving the game. Replacing a backup replaces its reserved file and creates
a new checkpoint. Recovery rolls back all game state to the checkpoint, moves the
AI and agent installations to the owned server, stops Mark's local processes,
and pauses time. It is also available from the shutdown backup choice. Hosting
must be online to recover. After recovery unpaid hosting suspends AI execution;
reactivating it charges only upkeep, without buying the server twice. These are
simulated resources and servers; no actual hosting service is contacted.

Existing saves receive missing catalog entries and an Operations state without
resetting completed upgrades, trust spent or running jobs. Once saved, Operations
configuration is retained in the save rather than overwritten by later defaults.

Recovery now transfers the AI workspace and its documents. See [the filesystem guide](../files/README.md) for layout, managed service files and migration.
