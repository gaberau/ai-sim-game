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

Trust consequences still occur through the existing delayed review mechanic;
there is no additional Mark follow-up conversation or Jessica complaint dialogue.
The full shutdown tree and later phases in the chart are not implemented yet.

Historical conversations exist only in `tests/fixtures/legacy-game` to exercise
backend functionality. They do not supply playable content.
