# Terminal rendering

`TerminalUI` owns game and interaction state. `ScreenRenderer` owns ncurses
resources for the duration of a terminal session and destroys them before
`endwin()`. Resizing recreates surfaces without resetting selection, editor
buffers, focus, or scroll offsets.

The main screen has shared-buffer header, body, and footer subwindows. Split
views add left and right subwindows; writes clip to their own pane. Menus and
text dialogs use a centered, bordered panel while retaining the existing
configurable controls and custom menu behavior. Their coordinates are local to
the dialog. The workspace is rendered underneath without an intermediate screen
update, so resizing an open menu keeps the current view visible.

Messages, files, upgrades, and agents share the same list-column sizing. Agents
show selected-agent details instead of a long dashboard of every agent. Upgrade
and agent details wrap and scroll independently of selection. The persistent
tab strip shows locked tabs dimmed and keeps the selected tab visible when the
whole strip cannot fit. Tab navigation continues to skip locked tabs.

The header shows the clock and a short resource summary; the news ticker appears
on the news view. The footer separates contextual actions from the short global
navigation hint. The default palette uses neutral text and borders, reserving
strong colors for selection and warnings; saved color settings remain editable.

Chat history, activity history, file previews, and editor drafts use bounded
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
