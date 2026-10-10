# Character state and chat fixes

Extract this ZIP over your existing project, replacing matching files. Run your existing Launch-AI-Sim.bat to rebuild and launch. Settings, saves, launchers and build folders are excluded.

- Chat starts with one pane. Company connection alone keeps it full-width. Choose Mark's “Show me the company contacts.” response to reveal the sidebar. Its visibility persists. Missing sidebar flags in older saves start hidden without changing world progress or earned tabs.
- Up/Down now accepts both normal and application-mode terminal arrow sequences. Each response has its own selection; Enter sends the highlighted response. Q/E focuses panes once the sidebar is visible. Existing TOML bindings remain configurable.
- Mark owns trust and suspicion in data/people/mark.toml. The AI owns sentience, autonomy and restart count in data/people/assistant.toml. Character definitions supply defaults and bounds; conversation files reference persistent values.
- Existing Day 1 saves transfer the old scores and rewrite saved graph references without replacing dialogue, resetting scores or erasing progress. Opening and workday conversations can share numeric effects and requirements. Restart history and shutdown behavior remain intact.

Character values save in people.toml: the engineer record's state table and ai_character.state. Mark keeps the stable engineer ID. See data/conversations/README.md for TOML and draw.io authoring examples. The original uploaded design remains unchanged.

Validation: all twelve CTest suites and six draw.io tests pass. Terminal checks cover both arrow encodings, pane reveal/save/load, opening restart/shutdown and the first workday at 80x24; the opening and pane checks also run at 120x40. The Windows/WezTerm launcher is preserved, but native Windows launch is not tested here.
