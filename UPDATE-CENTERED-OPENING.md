# Centered opening

Extract this ZIP over your existing project, replace matching files, and run your usual launcher. Settings, saves, launchers and build folders are excluded.

Before the interface is revealed, the screen shows only Mark's current message and your response choices in a centered block. There is no date, clock, tabs, resource summary, news, separator, contact pane, message counter, log or footer. The block recenters as messages change and terminal dimensions change. Long content scrolls, keeping the selected response visible. Up/Down selects; Enter responds. Left/Right still browses earlier messages; PageUp/PageDown reads long content. F1 opens the game menu when requested.

The opening remains minimal through normal dialogue, refusal, restarts and shutdown. A node or choice in opening.toml may set reveal_interface = true. The supplied connected node uses this to reveal the standard interface when Mark connects company chat. This persists in people.toml as chat.interface_visible. It reveals the interface frame, not additional tabs or the contacts sidebar; those keep their separate reveal rules.

Old saves in an unfinished opening use the centered layout automatically. Completed old openings and explicitly earned views retain the standard interface. Saved conversation graphs remain authoritative; old graphs lacking the new property reveal the interface when company chat is connected or the opening finishes. No progress is reset. Start a new game only if you want the latest authored conversation branches.

Validation: all twelve CTest suites and six draw.io tests pass. Terminal checks verify the absence of surrounding UI, horizontal/vertical block centering, response selection, save/load, refusal and restart at 120x40, 80x24 and 60x18. Standard interface reveal and resume are checked at 120x40 and 80x24. Native Windows launching is not tested here.
