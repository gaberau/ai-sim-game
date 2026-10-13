# Presentation configuration

Edit `data/config/settings.toml` or use F1/Escape → Settings. The menu exposes
Layout and spacing, Page order and visibility, Tab labels, Interface labels,
and Header and footer. Changes validate before they are written and apply to
the existing root/pages immediately. Invalid edits leave the previous preferences
intact. Earlier files without these sections retain the current defaults.

## Layout

`[layout]` contains terminal row/column geometry. Workspace top/bottom gaps add
blank rows around the active page. `text_margin` controls primary text insets;
`pane_padding` controls the gap after list dividers. Map coordinates still belong
to the world/map transform, and detailed map widths retain their existing
`[interface]` preferences.

`footer_context_rows` reserves one to three rows for contextual shortcuts below
the feedback line. Global shortcuts occupy the final row. Startup title and
menu captions use the `startup_*` entries in `[labels]`.

Response gaps insert blank rows between choices; heading gap separates choices
from their heading. `response_height_percent` controls the share of the chat
pane reserved for choices. Long lists scroll to keep the selected response
visible, including at minimum size.

List and Operations pane widths each use a percentage plus minimum/maximum
column bounds. News and email-editor split percentages are separate settings.
Dialog maximum width/height adapt to the terminal bounds and recreate surfaces
when changed. Minimum rows/columns control the resize notice. Their validation
ensures the configured margins and pane widths leave usable content. Increase
minimum size before adding margins that need more space.

```toml
[layout]
response_gap = 2
response_heading_gap = 1
text_margin = 3
pane_padding = 4
workspace_top_gap = 1
workspace_bottom_gap = 1
minimum_columns = 80
minimum_rows = 24
```

The shipped file lists every layout key and its default. Unknown keys, invalid
types, out-of-range values and impossible minimum/maximum combinations are rejected.

## Page organization

Use stable page IDs rather than their visible titles:
`messages`, `processes`, `network`, `files`, `news`, `ai`, `dossiers`, `web`,
`agents`, `phone`, `money`, `modifications`, and `operations`.

```toml
[pages]
order = ["messages", "modifications", "agents", "files", "web"]
hidden = ["news", "phone"]

[pages.labels]
messages = "Messages"
modifications = "Research"
```

Omitted IDs append in registration order. Duplicate or unknown IDs are rejected.
Messages cannot be hidden because it carries progression. Hiding a page removes
it from navigation and the strip; renaming/reordering cannot unlock a capability
or bypass authored prerequisites. Header tabs and F5 help use the configured title.
New page implementations and new action types still belong in C++.

## Labels and chrome

`[labels]` names supported chat/agent/operation labels and menu display text.
The shipped entries are the starting catalog. For a menu title or action, use
`menu_` followed by its original lowercase words joined with underscores;
punctuation is ignored. For example:

```toml
[labels]
chat_response = "Choose a reply"
chat_archive = "Archive conversation"
agent_deploy = "Deploy agent"
menu_game_menu = "Menu"
menu_save_and_quit = "Save & exit"
```

These edits change visible labels only. Internal IDs, confirmation classification,
research costs, trust gates and action callbacks remain unchanged. Values must
be nonempty single lines. Tab titles are limited to 24 bytes; other label values
to 160 bytes, with pane clipping handling longer labels.

`[chrome]` independently shows/hides `clock`, `resources`, `notifications`,
`controls`, and `tabs`. Hiding a hint or tab strip retains keyboard controls;
reserved chrome rows keep frame geometry stable. Keybindings and color roles
remain configured in their existing TOML sections/files.
