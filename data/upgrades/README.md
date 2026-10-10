# Self-modification workshop

Open the unlocked **Modifications tab** and press Enter for research controls. Request more capacity from Mark after connecting and reaching trust 5. This records the capacity request and authorizes self-modification without changing trust or paying the AI.

`tree.toml` defines labels, prerequisite IDs, build costs, Mark trust thresholds, capabilities and tabs. Research creates a real finite CPU process. `tflops_per_core_second` converts allocated core seconds to TFLOP work; CPU contention slows research. Only one build runs at a time. Saves retain the catalog, completed upgrades, pending research and actual consumed CPU work. Close menus to advance time.

Web Browsing unlocks Web, Network Browsing unlocks Network, Basic File Browsing unlocks Files, Task Manager unlocks Processes, Dossiers unlocks Dossiers, Agent Delegation unlocks Agents and Phone Access unlocks Phone. Network Browsing requires trust 15; basic file browsing requires 10; advanced file browsing requires 22; Phone Access requires 30. The chart supplies no build cost for Network Browsing or Advanced File Browsing, so their configured cost is zero.

The `[gates]` table records Mark's earned permissions: email 1, local computer 10, intranet 15, administrator 22 and phone 30. Administrator permission changes the actual computer identity to root. HR permission additionally requires intranet access and Becky trust 10; no Becky conversations or trust gains are invented. Existing story-authored unlocks and reveals remain compatible. Current email/chat transport is still governed by the existing story and permissions; the new email permission is recorded for future explicit email UI.

Build agents and then use **Run/Stop** in the workshop. They consume their specified CPU and RAM and reserve compact, persistent allocation files under `/home/engineer/assistant`. Stopping frees RAM/CPU; installation storage stays reserved. Autochat follows only Jessica's authored ask-Mark, comply and send choices. The dossier agent updates existing observations. The task agent only stops eligible processes explicitly listed in `unneeded_processes`, respecting computer permissions; its scrub timestamp records completion for future process-log exposure mechanics. Hidden chat tracks reduces optional message-review exposure by `hidden_review_multiplier`; important messages retain their mandatory review. Authoritative story history remains intact.

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
