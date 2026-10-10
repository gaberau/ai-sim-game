# Saved games

The game creates save slots in this folder. Slot names use letters, digits,
hyphens, and underscores. `save` and `load` default to the `autosave` slot.

Each slot contains a small `manifest.toml` commit pointer and complete snapshot
folders under `snapshots/`. The game writes and validates a new snapshot before
atomically replacing the pointer. Previous complete snapshots remain available;
uncommitted folders do not participate in loading.

A snapshot contains computer.toml, network.toml, files.toml, people.toml,
emails.toml, world.toml, and its own manifest.toml. Settings and starting scenario
files are never rewritten by saving. Copy an entire slot folder to back it up.

Normal exit saves `autosave` unless interface.autosave_on_quit is false. Normal
startup resumes it if present. Manual loads pause the game. `--new` starts from
the scenario instead, and `new` does the same inside the running game. Neither
removes named slots. Forced termination does not perform an exit autosave.
