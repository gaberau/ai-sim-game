# Tab visibility fix

Extract this ZIP into your existing project folder and replace matching files. Run your existing Launch-AI-Sim.bat to rebuild. No new game is needed. Config, saves and launcher files are excluded from the ZIP.

The interface now tracks visible tabs separately from system access. Connecting chat, meeting contacts, granting access and completing work no longer expose Network, People or other tabs automatically. Tab navigation skips hidden views.

Existing saves from previous builds receive a Chat-only visibility record once. Files, conversations, completed work, permissions and the calendar remain intact. Those builds did not distinguish automatic tab unlocks from explicit ones, so prior view openings cannot be reconstructed reliably. Reopen Files, News or Processes through their authored chat choices; future reveals persist.

Story choices can explicitly introduce any view with reveal_tabs; see data/conversations/README.md for TOML and draw.io authoring.

Validation: eleven CTest suites, five draw.io converter tests, and terminal checks at 120x40 and 80x24 covering startup, connection/contact gating, legacy saves, explicit reveals and resume.
