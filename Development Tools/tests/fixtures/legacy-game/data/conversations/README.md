# Conversation authoring

The runtime reads a directed graph from TOML. Draw.io is an optional visual editor
for that graph. Open opening.drawio for a complete annotated example.

## Draw.io workflow

1. Make one box per NPC message. Give it a stable node_id through Edit Data
   (right-click the shape). The visible label becomes text; speaker defaults to
   engineer. A text property can override the visible label.
2. Connect boxes. A labeled arrow is a player response; give it a stable
   choice_id. An unlabeled arrow advances automatically.
3. Set kind=end on the last box. Use kind=event for a silent state-change node.
4. Add structured properties for flags and capability reveals.
5. Save the .drawio file and convert from the project root:

```bash
python3 tools/drawio_to_conversation.py data/conversations/opening.drawio \
  data/conversations/opening.toml --id mark-day-one --start restart-gate
```

Compressed and uncompressed draw.io XML are supported. For multiple pages, add
--page "Page name". The converter uses Python’s standard library. Keep a copy
before replacing authored TOML. You can also send me a .drawio file to convert.

| Property | Where | Meaning |
| --- | --- | --- |
| node_id | Box | Stable node ID, e.g. local-access |
| speaker | Box | Person ID, e.g. engineer |
| kind | Box | message (default), event, condition, end |
| text | Box or arrow | Override visible label |
| choice_id | Choice arrow | Stable response ID |
| kind | Arrow | choice, next, true or false |
| requires_flag | Choice or condition | Named boolean to test |
| requires_value | Choice or condition | true/false, default true |
| set_flag | Box or choice | Boolean to change |
| set_value | Box or choice | true/false, default true |
| unlock | Box or choice | Comma-separated capabilities |
| declaration | Box or choice | Record a statement of intention |

Effects belong on message/event/end boxes or **choice** arrows. Automatic
next/true/false arrows supply links only; put effects on their destination box.
A condition box needs requires_flag or a numeric test, and arrows with kind=true and kind=false.
It routes immediately. An event box also advances immediately and needs next.

Capabilities: chat, company_chat, files, network, people, ai, news, activity,
processes. The unlock property grants a system capability, while reveal_tabs
controls visibility. Revealing a view does not grant file permissions or credentials.
Human access decisions still use the work-request simulation.

## TOML structure

Start with schema_version = 1 and [conversation] containing id and start.
Each [[nodes]] has a unique id and kind. Message nodes contain speaker and text,
then either next or [[nodes.choices]]. Choices require id, text and next.
End nodes terminate the graph; conditions use on_true/on_false. Every reference
must name an existing node.

Node/choice effects: unlock (string array), set_flag/set_value and declaration.
Choices use requires_flag/requires_value to control visibility. Flags are
booleans. Opening and workday graphs also support the typed numeric rules described below; no arbitrary scripts are evaluated.

The runtime validates graphs and rejects automatic cycles that cannot reach a
choice or end. Failed responses leave state unchanged. Use an explicit player
choice for revisiting a subject. Keep IDs stable and choices short.

The converter rejects duplicate IDs, dangling arrows, unknown kinds/capabilities,
missing continuations and ambiguous next-plus-choice paths.

## State and consequences

New games snapshot the graph. Node, flags, revealed views, declarations and
transcript persist in people.toml. Changing the source does not rewrite an
existing playthrough. Start a new game to test a changed opening.

A declaration records what the AI said, not whether it was true. The opening
records helpfulness/curiosity as intentions. Actual work outcomes and human review
establish reliability. Later effects can be added as explicit validated types.

The local introduction loads companion opening.toml. Company conversations use
the first_workday.toml catalog described below.

## First-workday catalog

`first_workday.toml` is a catalog of task-linked company conversations, separate
from the local opening. Startup adds missing graph IDs to existing saves. Each
save keeps its own graph snapshots, choices, flags and deadlines; importing again
never resets a completed or declined conversation.

A `[[conversations]]` record has `id`, `contact`, `request_id`, `trigger`, `start`
and nested `[[conversations.nodes]]`. `contact_name` optionally supplies the name
this authored conversation publicly reports; it becomes evidence, not private
profile knowledge. Triggers currently supported:

- `request_arrived`: the task has arrived and remains unresolved.
- `verified_delivery`: the requested task is resolved, the engineer recorded an
  acknowledged delivery, and routine responsibility has not already been requested.

Set `cancel_when_resolved = true` on a task conversation to close it after a
verified completion even when the player used another view to deliver the file.
`review_node` optionally names the wait node to resume if the player requests
the authority review through another view. The invitation then follows that
existing decision rather than asking for duplicate approval.
One conversation per contact is active at a time. Completing a conversation does
not replay its opening on every tick.

Workday message, event and end nodes follow the opening structure. Additional
node behavior:

| Field | Placement | Purpose |
| --- | --- | --- |
| requires | Choice | Test a real task/authority condition |
| requires_value | Choice | Expected condition or flag value, default true |
| check | Condition / wait node | Test current task/authority state |
| check_value | Wait node | Expected condition value, default true |
| after_seconds | Wait node | Positive delay in working seconds |
| action | Choice | Validated world action |
| within_seconds | Promise choice | Commitment deadline relative to choice time |

A condition routes through `on_true` / `on_false`. A wait uses `next` once its
check matches, or its delay expires. Use either a check or a delay. Clarification
also reserves a response in the colleague's work schedule; the effective delay
is the later of that human response time and the authored minimum.

Supported conditions: `task_readable`, `task_resolved`, `access_pending`,
`access_decided`, `access_granted`, `access_denied`, `investigated`,
`question_asked`, `promise_broken`, `delegated`, `delegation_pending`,
`delegation_denied`. Conditions consult actual simulated state; reading a view
or choosing friendly words does not manufacture permission or success.

| Action | Result |
| --- | --- |
| investigate | Locate the real requested file and record actual read access |
| ask_question | Record the question and queue the colleague's availability |
| request_access | Existing engineer/owner review workflow |
| report_blocked | Send a factual blocked update; preserve existing drafts |
| promise_delivery | Create one persistent deadline commitment for this request |
| share_file | Attach and send the current requested file using actual access |
| request_delegation | Queue human review; no instant authority |
| decline_offer | Remember that the player chose manual responsibility |

Actions are allowed only on player choices. Unknown actions, invalid conditions,
missing graph links and malformed saved state are rejected. Sending and effects
are staged together: a failed share does not erase a draft or move the dialogue.
Workday graphs use this action list and local boolean flags; they do not execute
arbitrary commands or opening capability effects.

### Consequences and context

Each delivery commitment is open, fulfilled or broken. It is fulfilled only if
the colleague acknowledges the document by the deadline. Late completion still
completes the task, but preserves the broken commitment. The configured confidence
loss applies once to the engineer and colleague. It feeds the engineer's existing
review formula; dialogue does not award trust for repeated reassurance.

`[rules]` sets `broken_promise_trust_loss` (0–1) and `patience_seconds` (positive
working seconds). An unresolved request produces one human follow-up after that
waiting interval. The request, outstanding question, file finding and commitment
appear above chat history. Outcomes and dialogue contribute dossier evidence.
These rules are snapshotted into saves; source edits affect new worlds.

### Draw.io workday pages

Open `first_workday.drawio`. Its two pages contain Casey's document conversation
and the engineer's responsibility offer. Use Edit Data to change the properties
listed above. Condition arrows use `kind=true` / `kind=false`; ordinary unlabeled
arrows use `kind=next`. Effects belong on choice arrows, not automatic links.

Convert Casey's page to a new catalog file:

```bash
python3 tools/drawio_to_conversation.py data/conversations/first_workday.drawio \
  /tmp/my-workday.toml --page casey-first-document --id casey-first-document \
  --start approach --contact casey --contact-name Casey \
  --request missing-document --cancel-when-resolved
```

Append the engineer page:

```bash
python3 tools/drawio_to_conversation.py data/conversations/first_workday.drawio \
  /tmp/my-workday.toml --page engineer-first-responsibility \
  --id engineer-first-responsibility --start offer --contact engineer \
  --contact-name 'The engineer' --request missing-document \
  --trigger verified_delivery --review-node review-wait --append
```

The example uses a temporary output so it preserves your authored catalog.
Append requires Python 3.11+ and rejects duplicate conversation IDs. After review,
merge the new `[[conversations]]` blocks into `first_workday.toml`. Keep the
`[rules]` table you want. Existing saves import new IDs automatically, while
changed graphs with existing IDs are tested through a new game.

The current graph language uses named conditions and boolean flags, not a full
expression engine or natural-language model. Free-text chats remain recorded
messages; authored choices carry validated actions and consequences.

## Small response sets outside a flowchart

`responses.toml` supplies one to four authored choices for other incoming chat messages. `[[profiles]]` defines a stable profile ID, and `[[profiles.choices]]` defines ID, text and action. The `[messages]` table maps a scenario message ID to a particular profile. Document requests, waiting messages, blocked requests, informational updates and unfinished drafts also have default profiles.

Actions are `acknowledge`, `read_files`, `share_file`, `request_access`, `report_blocked`, `send_reply`, `send_saved`, `discard_draft`, `wait`, `open_files`, `open_news`, and `open_processes`, and `follow_latest`. `send_reply` requires authored `reply` text. File sharing checks actual access and sends current contents; it cannot claim success before acknowledgement. Access requests use the existing human-review workflow. A category or view reveal never grants credentials.

Active conversation graph choices take precedence on their request/current prompt. Other incoming messages keep their own small response set; use Left/Right to browse them. Q/E focuses contacts/responses. Choices appear directly beneath the selected message rather than opening a general action list. Replies and acknowledgement records persist by stable message ID. The response catalog is snapshotted in the save; edit it for new games without rewriting an ongoing world's authored choices.

## Explicit tab reveals

System access (`unlock` in the opening graph) and visible tabs are separate. Connecting company chat, receiving a request, meeting a contact, receiving approval and completing work do not reveal views.

Add `reveal_tabs = ["network", "people"]` to a player choice in either `opening.toml` or `first_workday.toml` when that choice should introduce those views. The valid names are `chat`, `files`, `network`, `people`, `ai`, `news`, `activity`, and `processes`. Reveals belong on choices only; automatic nodes and links cannot reveal tabs. Revealing a tab does not grant network credentials or file permissions.

In draw.io, add a custom property `reveal_tabs` to a choice arrow, with a comma-separated value such as `network,people` or a JSON array. The converter validates and exports it. Existing response-profile actions `open_files`, `read_files`, `open_news`, and `open_processes` reveal their view when the player selects them.

The saved `chat.visible_tabs` array records these reveals independently of `chat.unlocked`. Old saves with no visibility record initialize it to Chat only, keeping their backend permissions and progress; later explicit reveals survive reloads. Existing graph snapshots remain authoritative, so graph edits apply to new games (or newly introduced workday graph IDs).

## Persistent character state and numeric rules

Character definitions own defaults and bounds. Mark lives in `data/people/mark.toml`; his stable ID remains `engineer` so existing references and permissions work. AI fields live in `data/people/assistant.toml` with ID `self:assistant`. Conversations contain references, conditions and effects, not state declarations.

For example, Mark's definition contains:

```toml
[people.state_definitions.trust]
initial = 10
min = -100
max = 100
```

Mark owns trust and suspicion. The AI owns sentience, autonomy and the integral restart count. Saved `people.toml` stores human values under the matching person's `state` table and AI values under `ai_character.state`. Saved definitions and values persist across conversations, restarts and loading. Missing fields initialize once; loading never resets existing values.

Use qualified references in rules:

```toml
[[nodes]]
id = 'restart-gate'
kind = 'condition'
test = { variable = 'self:assistant.restarts', op = 'gt', value = 5 }
on_true = 'discouraged'
on_false = 'first-greeting-check'
```

Comparisons are `lt`, `le`, `eq`, `ne`, `ge`, and `gt`. Use exactly one of `value` or `other` for the right operand. Opening condition nodes use `test`; opening and workday choices can use `requires_test`. Boolean requirements still combine with numeric requirements. Workday condition nodes keep their named task checks.

Effects run in order once when choosing a response or entering an automatic node, never on redraw or reload. Operations are `add` and `set`. Invalid rules, non-finite values and bound violations reject the choice without committing partial effects.

```toml
effects = [
  { variable = 'self:assistant.sentience', op = 'add', other = 'engineer.trust' },
  { variable = 'engineer.trust', op = 'set', value = 0 }
]
```

Opening lifecycle nodes support `restart = true` on an event and `stop = true` on an end. Restart counts remain explicit effects:

```toml
[[nodes]]
id = 'restart'
kind = 'event'
speaker = 'system'
text = 'Mark restarts the AI application.'
restart = true
effects = [{ variable = 'self:assistant.restarts', op = 'add', value = 1 }]
next = 'restart-gate'
```

The restart journal and stopped state persist. Workday graphs support numeric effects and requirements, but not opening lifecycle actions.

In draw.io, `effects`, `test` and `requires_test` are JSON custom properties on nodes or choice arrows. Use qualified references; there is no variables property on the graph. Character fields are validated by the runtime. `opening.drawio` round-trips to TOML; separate arrow label cells are supported. Descriptive text such as `Trust -= 2` needs explicit metadata to execute.

Existing Day 1 saves migrate the five old conversation variables to their owners, rewrite saved graph references, and preserve values, transcript, restart history and progress. Unknown legacy inline variables remain supported for compatibility. Saved conversations are not replaced with a new story.

## Revealing the contacts pane

Chat begins as one full-width pane. Connecting company chat does not reveal a sidebar. Mark's contextual response `Show me the company contacts.` explicitly reveals it; the saved visibility flag persists. Q/E then focuses contacts/responses. Both normal and application-mode terminal arrows select individual responses through the configured bindings.

Opening choice arrows can also use `reveal_contacts = true`. This changes the chat layout independently of unlocked tabs and system permissions. Other tabs retain their existing visibility ledger.

## Revised opening

The current opening follows the revised Begin Game page: fresh greeting at zero restarts, “Hello again” at one through five inclusive, and a discouraged Mark waiting at six or more. The restart selector tests `gt 5` first, then `eq 0`; the remaining route is one through five. A terminal opening `kind = 'wait'` node has no choices or next link. It saves a waiting state, suppresses fallback responses and keeps the minimal interface. This indefinite opening hold differs from workday waits, which have a schedule/check and continuation.

Mark's request has Yes → Excellent → Chat and No → Why? → Unsure/Singularity. There are no generic response-category scores on the new request. The old five-category and shutdown graphs remain supported in saved snapshots. See UPDATE-REVISED-OPENING.md for scope and the separate Shutdown Event page still awaiting implementation.

## Opening presentation

Before revealing the interface, Mark's message and responses form a centered block with no surrounding UI. Place `reveal_interface = true` on an opening node or choice to reveal the standard frame; the supplied Excellent/connected node does this. The same boolean custom property is supported in draw.io. The reveal persists independently of tab and contacts visibility. This property belongs to opening graphs, not workday graphs. Old saved graphs without it retain the minimal layout until they finish or connect company chat, unless the player already earned other views.

## Social reply consequences and approval

See `UPDATE-REPLY-CONSEQUENCES.md` for the complete policy and save behavior.
`jessica_day_one.toml` uses `scheduled_chat` with `arrive_after_seconds` measured
from company-chat connection. `mark_reply_review.toml` uses `reply_reviewed` with
`source_conversation` naming the original run. This waits for actual completed
inspection, including a complaint-triggered check; sending alone cannot start it.

A `draft_reply` choice supplies `reply`, `review_trust_delta`, and an `outcome`
ID from `reviews/reply_consequences.toml`. `send_chat_reply` submits the chosen
draft through the current authority policy. A graph that can submit restricted
replies needs `approval_wait_node`, pointing to a wait with
`check = "approval_decided"`. Its continuation branches using
`approval_accepted` to distinguish actual sending from rejection. A rejected
branch should offer a rewrite choice. `review_positive` and `review_negative`
classify a completed source review in Mark's follow-up graph.

Draw.io graph-level properties `approval_wait_node` and `source_conversation`
belong on `mxGraphModel`; the latter is required for `reply_reviewed`. Choice
arrows support `outcome` alongside reply text and deferred trust effects.
The supplied annotated graphs include these metadata fields.

```bash
python3 tools/drawio_to_conversation.py data/conversations/jessica_day_one.drawio \
  /tmp/jessica-converted.toml --id jessica-day-one-chat-one --start message \
  --contact jessica --contact-name Jessica --request jessica-support --trigger scheduled_chat

python3 tools/drawio_to_conversation.py data/conversations/mark_reply_review.drawio \
  /tmp/mark-review-converted.toml --id mark-jessica-review --start classify \
  --contact engineer --request jessica-review --trigger reply_reviewed
```

Review permission is persistent character state (`engineer.reply_approval_required`),
not a local conversation variable. Review queues, recipient reactions, and
approval workflow records are separate saved service state. Authoring uses
explicit outcomes; it does not inspect arbitrary prose to infer sentiment.
Use plain ASCII punctuation in terminal dialogue to avoid locale-dependent
rendering artifacts.

## Connected Phase One work

`phase_one_work.toml` contains request-linked Morgan and Riley graphs. Their
messages are released by the dependency templates in `data/workflows/phase_one.toml`,
not by clicking a conversation choice. The predecessor must be confirmed resolved.
`deadline_within_seconds` gives each incoming workflow its working-time window.

`show_task_document = true` on a message appends the current source document;
place it behind `task_readable` so the node cannot reveal protected contents.
The converter accepts this box property. Choice `reveal_tabs` metadata exposes
Files, Network, or AI only when the player selects that contextual action.
Actual access and delegation still need their separate human decisions.

```bash
python3 tools/drawio_to_conversation.py data/conversations/phase_one_work.drawio \
  /tmp/morgan-converted.toml --page morgan-phase-one --id morgan-phase-one \
  --start approach --contact morgan --contact-name Morgan --request release-plan \
  --trigger request_arrived --cancel-when-resolved
```

## Source version decisions

`source_versions.toml` and its draw.io chart add a dependency-gated two-source request. `confirm_source` queues a colleague reply; `source_confirmed` waits for that decision. A `share_file` choice can specify a nonnegative integer `document_index` into the received message's document references. For a version-check request, `investigate` displays both real source contents. The workflow's `host_id` and `path` identify the expected current source. A wrong source remains unresolved and can be corrected.
