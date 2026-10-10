# Revised opening and inclusive restart range

Extract over the existing project, replace matching files and launch with your usual launcher. Configuration, saves, launchers and build folders are excluded.

Start a new game from F1 to play this revised story. Existing saves retain their authored conversation snapshots, including older restart thresholds; this update does not replace an ongoing story or reset character values.

The opening uses the uploaded ai game(2).drawio with the corrected condition agreed in conversation:

| Restart count | Route |
| --- | --- |
| 0 | Hello AI, my name is Mark. I am your Creator |
| 1 through 5 inclusive | Hello again AI, my name is Mark. I am your Creator |
| Greater than 5 | I don't know if this is going to work... then hold |

Two ordered condition nodes make these cases mutually exclusive: first test restarts > 5, then test restarts == 0. Their tests and destinations are editable in opening.toml. The wait node holds without marking the AI stopped, ending the opening, automatically restarting or revealing the normal interface. Enter has no response to submit. Save/load preserves the wait and restart count. F1 remains available to load or start a new game.

The first request now has Yes and No, as drawn. Yes leads directly to Excellent and Chat access; No leads to Why?, with Unsure and the Singularity response. The reassurance question also includes No leading to cleanup. The extra thoughts/Casey example and generic five-category response system are removed from the new opening. Older saved graphs retain them. The existing first-workday simulation remains available after company chat connects.

The opening stays centered without clock, date or other interface chrome until Excellent reveals the standard Chat interface. No other tab or contacts sidebar is automatically revealed. Existing contextual choices can reveal those later. The Singularity effect remains sentience +2, trust −2. Cleanup/restart currently uses the existing simulated restart event, with one increment and one journal entry.

The separate revised Shutdown Event page (accept/protest, backups, threats, begging with Fear and violence) is design material for a later edit; this update does not implement those alternatives. The six-plus discouraged branch waits instead of invoking the old fifth-restart shutdown dialogue. Phase 1 and Phase 2 mechanics are unchanged.

Authoring files: data/conversations/opening.toml and opening.drawio. Exact uploaded revision bytes are preserved at data/conversations/design/ai-game-revision-2.drawio; the original design is also retained. The annotated runtime counterpart encodes the corrected restart condition, while the uploaded design copy stays unchanged.

Validation: all twelve CTest suites and six converter tests pass. Tests cover zero, every count from one through five, six/wait, waiting without fallback choices, exact restart accounting, save/load, Yes/Excellent/Chat, both No routes, and compatibility with previous saved graphs. Terminal checks cover the revised story and waiting at 120x40 and 80x24. Native Windows launching is not tested here.
