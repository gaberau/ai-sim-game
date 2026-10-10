# Mark's Day 1 opening

Extract this ZIP over the existing project and replace matching files. Launch with your existing Launch-AI-Sim.bat. Config, saves, launchers and build folders are not included.

Use F1 → Start a new game → Start new game to see Mark's opening. Existing saves retain their existing conversation snapshot, permissions, calendar and progress. Installing this update does not replace an ongoing story.

## Implemented

Mark introduces himself using the dialogue from your Day 1 chart. Silence and a polite greeting lead to his email request. Denying a creator leads to uncertainty or a correction. The Singularity branch applies the chart's +2 sentience/-2 trust effect, leaves autonomy and suspicion unchanged, and causes a restart.

A restart returns to the introduction without erasing the transcript or accumulated numeric state. Every restart receives a saved session number and journal entry. Five restarts lead to a shutdown conversation. A plea tests trust > sentience; success applies sentience += trust, then trust = 0, and allows another attempt. Failure or allowing shutdown ends the application in the simulation. The terminal stays open so the player can read the journal or use the game menu. Saving and loading cannot revive a stopped AI.

The normal path continues into the existing example, company chat connection and first real workday. Opening system permissions do not reveal tabs. Chat remains the only visible tab until the player chooses an explicit view reveal.

## Authoring

Edit data/conversations/opening.toml. Dialogue, choices, the five-restart threshold, comparisons and numeric effects live there. Character defaults and bounds now live in data/people/mark.toml and assistant.toml; see UPDATE-CHARACTERS-CHAT.md. opening.drawio is an annotated counterpart that round-trips through tools/drawio_to_conversation.py. Your uploaded seven-page design is preserved unchanged at data/conversations/design/ai-game-original.drawio.

The five story variables are trust, sentience, autonomy, suspicion and restarts. Trust is the opening's provisional Mark confidence score, separate from the workday's evidence-based relationship model. Sentience is an authored story indicator, not a claim that a simulation can measure consciousness. Autonomy and suspicion are recorded independently. These are dimensionless scores; restart count is integral. The sparse opening does not expose a stat dashboard. State is available in saved people.toml.

Restarts preserve flags, declarations, graph state variables, permissions, the transcript and world data. They restart the authored conversation, not the actual operating-system process. No code or save files are erased. Backup rollback, threats, physical actions and migration to another machine are later capabilities; this update does not pretend the starting AI has them. The chart's incomplete basic-response and later-phase pages are retained as design material.

## Validation

Twelve CTest suites cover the new branches, ordered effects, bounds, counters, restart journals, save/resume, shutdown, old graph compatibility and existing workday behavior. Six draw.io tests cover round-trip conversion, separate arrow labels and invalid metadata. Terminal checks cover the opening and shutdown at 120x40 and 80x24. The Windows/WezTerm launcher is preserved; native Windows launching is not exercised here.
