# Terminal rendering

`RootUI` is the root of the terminal interface. It owns the terminal session,
one `ScreenRenderer`, workspace children and the overlay stack. It composes the
header, unlocked tab strip, active workspace and footer, routes global navigation,
and sends other input to the focused child. Menus and text dialogs capture focus;
editors occupy the workspace beneath them. The root presents each frame once.

`TerminalUI` bridges existing gameplay actions and view drawing to registered
child pages. Each typed child owns its selection, scroll offsets, pane focus,
and editor state in `PageState.h`; the bridge borrows those objects. Existing
view files provide the child rendering and actions, without separate terminal
sessions or independent screen refreshes. The live `WorldState` and settings are
shared through `UIContext`, while simulation rules stay in the game core.

The renderer destroys ncurses surfaces before the root closes the terminal.
Resizing recreates surfaces without resetting child state. Add a workspace by
registering a child with an ID, title, availability predicate, draw callback and
input callback; locked children are omitted from both tabs and navigation.

The main screen has shared-buffer header, body, and footer subwindows. Split
views add left and right subwindows; writes clip to their own pane. Global menus and
text dialogs use a centered, bordered panel while retaining the existing
configurable controls and custom menu behavior. Their coordinates are local to
the dialog. The workspace is rendered underneath without an intermediate screen
update, so resizing an open menu keeps the current view visible.

Messages, files, upgrades, and agents share the same list-column sizing. Agents
show selected-agent details instead of a long dashboard of every agent. Upgrade
and agent details wrap and scroll independently of selection. The persistent
tab strip hides locked tabs and keeps the selected tab visible when the whole
strip cannot fit. The retired Activity page is removed; resource details unlock with process access.

The header shows the clock and a short resource summary; the news ticker appears
on the news view. The footer separates contextual actions from the short global
navigation hint. The default palette uses neutral text and borders, reserving
strong colors for selection and warnings; saved color settings remain editable.

Chat history, file previews, and editor drafts use bounded
pads. Each pad holds the visible slice, so ncurses allocation stays proportional
to terminal size rather than document length. The view still owns its offset.
Pads copy into the owning panel's buffer before composition; this keeps cursor
highlighting and overlapping panels consistent.

`update_panels()` stages window updates and one `doupdate()` presents the frame.
No view calls physical refresh directly. Ncurses already compares screen cells;
this structure provides ownership, clipping, and consistent composition rather
than promising an unmeasured speedup. `full_redraw` still requests repainting for
terminal compatibility. Input comes from the active window, with the configured
timeout and keypad handling.

The build requires the ncurses panel library as well as ncurses itself. Ubuntu's
`libncurses-dev` package supplies both. The renderer test exercises pane clipping,
large document offsets, modal overlays, cursor styling, and repeated resizing;
the pseudo-terminal smoke test exercises actual game controls and flows.

Gameplay action lists appear in the workspace's right pane. They keep the selected
object visible, do not pause simulation time, and close when switching tabs.
Escape returns to the page; F1 opens the global menu. Settings, save/load, numeric
or text entry, and consequential confirmations retain centered dialogs. F5 help
opens in the page as well.

Agents expose deployment, pause/stop, repair and resource controls beside the
selected agent. Q/E moves between the agent list and its controls; Enter applies
the selected control (or deploys/pauses the agent when the list has focus). CPU,
RAM and priority use one numeric entry dialog and return directly to the page.
Files preview the selection automatically. Enter opens folders, edits writable
files, or focuses read-only content; Space opens additional file actions. The
footer shows the action available for the current selection. Messages retain
contacts, history and response choices together, with extra room for responses.

Workspace content has a blank row above and below it, with wider text insets
from screen edges and pane dividers. Response choices have a blank line between
them and a gap beneath their heading; the selected choice remains visible when
the response area scrolls. Opening conversation choices use the same spacing.

Messages has Archive/Restore and Active/Archived buttons beneath the contact
list. A and V are their default shortcuts and are editable in keybindings.
Archive state is stored with the world save; history and ongoing conversations
remain intact. Archived chats stay available in the Archived list.

The shared frame, list/dialog geometry, response spacing, page order/visibility,
and visible labels now come from presentation sections in settings.toml. See
[presentation configuration](presentation-toml.md) for keys and validation rules.
