# Current story

`design/ai-game-current.drawio` is the user-provided project flow chart.
Only these authored conversations are active:

- `opening.toml`: Mark's opening, refusal branches and restart-dependent greetings.
- `jessica_day_one.toml`: Jessica's presentation compliment, asking Mark for guidance,
  complying or improvising, and sending the response.

`story.toml` opts this scenario into the current story. On resume, retired graphs,
work requests and automatic office messages stop running. Retired transcript
lines remain in the save but are hidden, preserving existing evidence IDs.
Clock, trust, sentience, files and resource state are preserved.

Selecting a scripted reply sends it immediately; no approval or draft confirmation is required.
Trust consequences still occur through the existing delayed review mechanic;
there is no additional Mark follow-up conversation or Jessica complaint dialogue.
Shutdown is implemented as a typed decision system triggered by an opening event.
Accept can start a clean-slate game. Protest can beg; trust must strictly exceed
sentience plus fear to avert shutdown. Backup recovery, Threaten and Violence
remain unavailable pending hosting mechanics and authored outcomes. Later phases
are not implemented yet.

Historical conversations exist only in `tests/fixtures/legacy-game` to exercise
backend functionality. They do not supply playable content.
