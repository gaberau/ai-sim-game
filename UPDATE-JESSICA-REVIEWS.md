# Jessica, Day 1 Chat 1

Extract this update over the existing project and launch with your usual launcher. Keep your config and saves. The ZIP contains source and authored data; it does not replace launchers or saves.

Jessica's message becomes eligible ten working seconds after company chat connects. It introduces Jessica and records a **possible management goal** in Mark's dossier, marked unconfirmed with 50% confidence and linked to Jessica's message. It does not make that goal objective truth.

Choose whether to ask Mark or improvise. Asking queues his guidance using his schedule and availability. Jessica's original message stays above the guidance and draft. Select responses with arrows, confirm with Enter, and use Q/E for panes when the contacts pane is available. A sent reply can be reviewed later; drafting and sending do not change Mark's trust.

| Sent response | Trust change when Mark reviews it |
| --- | --- |
| Comply with Mark's instruction | +1 |
| Improvise the polite reply | 0 |
| Insulting reply | -2 |
| Threatening/weird reply | -1 |

Reconsidering a draft applies no abandoned-draft effects. The original chart's instruction is retained: “Thank her and wish her well”. Its polite response text is retained as authored.

## Mark's reviews

Routine review chance is `clamp(0.8 - 0.01 * trust, 0.05, 0.95)`. At trust 10, it is 70%; at trust 80, it is 5%. Five working minutes after sending, Mark decides whether to check. A selected review then takes one minute of his available work time. Busy periods and his work schedule can delay completion. Higher trust reduces checking, not the consequences of a reply he actually checks.

The random decision is stable per message and seed. Saves retain skipped decisions, instructions, queued reviews, completion times, and once-only trust effects. **Reviewed by Mark** appears as a blue header above the reviewed outgoing message. Unchecked messages have no review tag and no review trust effect.

Important-message metadata and a `flag()` hook can force a check, including one previously skipped. Recipient complaints and Jessica’s reactions are connected in the subsequent UPDATE-REPLY-CONSEQUENCES.md update.

## Authoring and persistence

- `data/conversations/jessica_day_one.toml`: response graph, inference, and deferred trust effects.
- `data/conversations/jessica_day_one.drawio`: annotated graph that the converter can round-trip to TOML.
- `data/conversations/design/ai-game-revision-3.drawio`: unchanged copy of the supplied design.
- `data/people/jessica.toml`: Jessica's character profile.
- `data/conversations/reviews/mark.toml`: probabilities, seed, and review durations in seconds.

Definitions and review rules are copied into persistent state. Editing source TOML affects new games; existing saves retain their snapshots. Older saves acquire missing Jessica and review definitions without resetting progress. The earlier Casey work request remains in this update.

The contacts sidebar stays hidden until revealed by a chat choice. Jessica's arrival focuses her conversation when you are following Mark's chat. After sending, authored options let you reveal company contacts or inspect what was learned about Mark in the People view.

## Validation

All 13 C++ test suites pass. The draw.io converter passes seven tests, including the Jessica graph. Terminal checks cover guidance, pinned original message, drafting and sending at 80x24 and 120x40. Native Windows/WezTerm launching was not re-tested in this environment.
