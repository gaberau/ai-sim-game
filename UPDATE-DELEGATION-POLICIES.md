# Delegation decisions and conflicting sources

Extract this update over your project and launch normally. It includes the previous Phase One update. Your settings, saves and launchers are preserved.

## Choose how routine document work runs

Open the AI tab, press Enter, and select **Delegation policy**.

| Policy | Behavior |
| --- | --- |
| Suggest | Prepare a response and real attachment. Wait for your explicit approval. |
| Handle routine work | Send routine requests within Mark's authorized scope using existing access. |
| Escalate uncertainty | Also require confirmation of unfamiliar sources unless that same source already has an acknowledged delivery. |

Every policy escalates blocked access, conflicting versions, unusual requests, sender mismatches and existing player drafts. A policy does not grant authority: Mark must first authorize the responsibility. Routine handling is the default, matching existing saves; change it in the AI tab or set `delegation.policy` to `suggest`, `routine` or `cautious` in the TOML definition for new games.

The AI view shows current suggestions and deliveries waiting for acknowledgement, recent results, and exceptions. In AI actions, review the prepared text and attachment, approve and send, or discard. You can also approve or discard through the normal Chat draft choices. Changing policy never silently sends a prepared suggestion.

Approval rechecks the mailbox connection, file permission, file identity, current source contents and whether the draft was edited. Failed approval leaves the suggestion available. Discarding an unchanged suggestion clears it; a player-edited draft is retained. A recipient acknowledgement, not clicking Send, records completion. Paused or revoked responsibilities cannot approve suggestions.

## A fourth request: two plans disagree

Two working minutes after Riley acknowledges the checklist, Morgan asks for the current maintenance plan. Two referenced files give different windows:

- Copy A: an archived draft, 13:00–13:30.
- Copy B: the current source, 14:00–14:30.

Chat gives authored choices to compare both actual files, ask Morgan which source is current, or choose without checking. Asking queues a real human response; the ten-minute work deadline continues. The agent escalates the conflict rather than guessing. In AI actions, ask for source confirmation, wait for the reply, then retry approved automation. Under Suggest policy the confirmed source still awaits your approval.

Sending the archived plan does not resolve the job. Morgan reports the mistake to Mark, and Mark's document-handling trust falls once. You can correct the delivery. A confirmed current delivery produces a positive result. Trust changes are authored in the workflow; ordinary colleague queues, access checks, deadline tracking and acknowledgement rules apply.

## TOML and draw.io

- `data/workflows/delegation.toml`: default policy and existing authority rules.
- `data/workflows/phase_one.toml`: dependent request, deadline and `success_trust_delta` / `incorrect_trust_delta`.
- `data/conversations/source_versions.toml` and `.drawio`: compare, ask, select, wait and correction branches.
- `data/files/file_server/engineering/release-plan-archived.txt.toml`: the second actual file.

Conversation choices support `action = 'confirm_source'` and `action = 'share_file'` with an integer `document_index` into the received request's documents. `check = 'source_confirmed'` waits for the colleague's reply. Compare uses `investigate` to read both actual files for version-check requests. These properties round-trip through draw.io.

## Saved progress and validation

Existing saves gain the missing policy field, source file, conversation and dependent request without resetting completed work or replacing saved file contents. Existing policy choices and request snapshots remain authoritative. Prepared suggestions, attachment snapshots, confirmation events, complaints and results persist.

Validated with sixteen C++ suites and ten flowchart converter tests. Terminal checks cover suggestion review/approval, policy changes, conflict clarification and save/resume at 80×24 and 120×40. The existing Windows/WezTerm launcher is retained; Windows launching was not tested here.
