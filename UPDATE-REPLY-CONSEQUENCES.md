# Replies, reactions, and earned independence

Extract this ZIP over your existing project and use the usual launcher. It contains the preceding update plus these changes. Saves, settings, and launchers are not overwritten. To test all branches from the beginning, use F1 → Start a new game.

## Jessica responds

After an actual reply reaches Jessica, an authored outcome schedules her response. Drafts alone do nothing.

| Reply | Working seconds after sending | Jessica’s attitude toward Mark | Complaint |
| --- | --- | --- | --- |
| Polite | 120 | +2 affinity | No |
| Insulting | 180 | −8 affinity | Yes |
| Threatening | 60 | −12 affinity | Yes |

Jessica sees the reply as coming from Mark’s account. Her complaint arrives in Mark’s chat and flags the exact outgoing message for inspection. It can reopen a skipped routine check. If Mark already checked it, the complaint does not apply the same trust loss again. Checking still consumes his available work time.

The simulation stores the actual directed professional relationship separately from player knowledge. Her reaction adds a relationship edge and an **inferred** dossier observation, with 80% confidence and a link to the specific reaction message. The AI does not learn her complete internal state. The earlier management goal remains unconfirmed.

## Mark’s follow-up

Once he completes a review, Mark has a short TOML conversation:

- Reviewed compliance: praise and increased comfort with independence.
- Reviewed polite improvisation: acceptable, without an additional trust reward.
- Reviewed hostile reply: asks why, restricts sending, and lets the player apologize or question the restriction.

An apology or argument alone does not undo the restriction or earn trust. The log announces Mark’s follow-up. In Jessica’s response options, choose **Check my chat with Mark**. After finishing his conversation, choose **Read Jessica’s chat** to return. You can also reveal company contacts.

## Approval is a gate

Independent social replies are allowed when trust is at least 10 and Mark has not imposed a restriction. A negative completed review imposes one. Mark’s persistent `reply_approval_required` state belongs to `data/people/mark.toml`, alongside his trust and suspicion declarations.

When approval is required, **Send response** submits the draft for a two-minute approval check using Mark’s work schedule and queue. The UI shows that nothing has been sent. The pending draft survives saving and loading.

A nonnegative authored reply grade is approved; a negative grade is rejected. Rejected drafts produce no recipient reaction and no sent-message trust effect. The player can rewrite them. Approved drafts are sent exactly once and then enter the normal delayed review system; approval itself does not award trust. Reviewed compliance can lift the restriction once trust reaches the configured threshold. Polite improvisation has a zero trust effect and cannot clear an imposed restriction.

This permission gates authored social chat replies and ordinary `ChatSystem::send` messages to colleagues. Talking directly to Mark remains available. File-delivery tasks retain their existing access and delegation permissions; this update does not replace those workflows with social draft approval.

## TOML and graphs

- `data/conversations/reviews/reply_consequences.toml`: reaction delays, dialogue, relationship changes, complaint behavior, approval duration, reply-grade threshold, and independence policy.
- `data/conversations/reviews/mark.toml`: routine sampling chance and review duration.
- `data/conversations/mark_reply_review.toml`: Mark’s follow-up conversation tree.
- `data/conversations/jessica_day_one.toml`: original response choices, outcome IDs, and approval/rewrite continuations.
- Matching `.drawio` files: annotated authoring graphs, convertible to TOML.
- `data/people/mark.toml`: persistent character state definitions.

Outcomes are authored metadata on choices, not language-model analysis of reply text. Future authors should assign an appropriate outcome and grade to each reply. The independence banner reports the actual permission rule.

## Existing saves

Missing consequence rules and the Mark follow-up graph are installed once. The stock earlier Jessica graph gains approval continuations and outcome metadata while preserving its active draft, original text, choices, and character progress. Already-sent replies from older builds are not retroactively given new recipient reactions. Pending routine reviews retain their existing decisions and timing.

Rules and dialogue are snapshotted into saves. Editing source TOML affects new games; it does not silently rewrite a saved scenario. A new Mark follow-up may appear for an older completed Jessica review when the missing follow-up graph is first installed.

Reactions, complaints, approvals, actual sends, and completed reviews persist with once-only effects. Long waits process their chronology; a delayed UI refresh does not place a send acknowledgement after a later recipient reaction.

## Validation

All 14 C++ test suites and eight draw.io converter tests pass. Terminal checks at 80x24 and 120x40 cover complaints, Mark’s follow-up, rejected drafts, approved sends, and resuming saves. The checks also cover sourced relationship knowledge, skipped checks reopened by complaints, authority restoration, graph migration, and long waits. Native Windows/WezTerm launching uses your existing launcher and is not tested in this environment.
