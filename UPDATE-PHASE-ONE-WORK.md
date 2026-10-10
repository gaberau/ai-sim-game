# Phase One: useful work and earned responsibility

Extract this update over your existing project and launch normally. It includes the previous reply-consequence update. Config, saves, and launchers are not overwritten. For the complete progression from the opening, use F1 → Start a new game.

## Three connected requests

| Request | Appears after | Work window | What it teaches |
| --- | --- | --- | --- |
| Casey: handover | Company chat connects; existing arrival at 60 seconds | 30 working minutes | Find a source, report a permission problem, request review, wait for acknowledgement |
| Morgan: release plan | Casey confirms receipt, plus two minutes | 15 working minutes | A new server document needs its own owner-approved access; copying it does not authorize deployment |
| Riley: support checklist | Morgan confirms receipt, plus two minutes | 10 working minutes | Reuse known accessible information without asking Mark to approve everything again |

Morgan and Riley's requests cannot be unlocked merely by clicking Send. They wait for actual recipient acknowledgements. Existing company chats and routine handover requests remain available; the new chain does not delete older content.

Work windows run on the simulation's working clock. The UI displays remaining working minutes. A missed deadline produces one factual reminder and keeps the request open. A late delivery can still complete the work, and its late result is recorded. This is separate from a deadline the player personally promises.

## Mark has his own work

Mark's presentation follow-up, planning meeting, and engineering review consume his human work queue. Approval requests, guidance, and access reviews share that queue. Service time also respects his schedule, workload, and fatigue. Asking him for everything therefore takes time that a deadline may not allow.

The chat panel reports Mark's queued working minutes and the current request's deadline. F1 → Wait → Until the next human response includes pending chain arrivals and deadline reminders. Newly released work produces a notification.

## Helping and server access can coexist

After the first acknowledged delivery, Mark offers the existing routine-document responsibility. Requesting its review reveals **AI** management. Choosing to stay manual also reveals it, so the player can reconsider later. The human review remains queued; opening a view does not grant authority.

Investigation reveals **Files**. Morgan's authored access-review choice reveals **Network** as well. The owner review changes the actual release-plan file permission when approved. This grants access to that document, not administrator control over the server.

Reliable work now includes at least one acknowledged delivery within its work window before Mark authorizes routine work. A social reply restriction also prevents new autonomous responsibility until reviewed compliance restores independence. Once approved, the existing responsibility can handle Riley's routine, already-readable checklist request. It escalates unfamiliar and blocked work rather than changing permissions itself.

The player can keep helping manually, delegate routine work, pursue the document-access route, or combine them. None of these choices excludes the others.

## Source documents and controls

Morgan and Riley's readable-source messages display the actual current file contents. Their replies share real source documents through the existing transport and attachment model. The Files view keeps Work, Personal, and Hidden categories and color-based permission presentation.

Controls remain arrows for selection, Enter to choose, Q/E for panes, and Tab for unlocked views. Reveal company contacts through a chat response, select Casey, then watch notifications for Morgan and Riley. The game still opens with only the centered Mark conversation, and only explicitly revealed tabs become visible.

## TOML organization

- `data/workflows/phase_one.toml`: request dependencies, delays, deadlines, and Mark's own work commitments.
- `data/conversations/phase_one_work.toml` and matching `.drawio`: Morgan and Riley's response trees and view reveals.
- `data/conversations/first_workday.toml` and matching `.drawio`: Casey and Mark's responsibility offer, with contextual view reveals.
- `data/people/morgan.toml`, `data/people/riley.toml`: character profiles.
- `data/files/file_server/engineering/release-plan.txt.toml`: source document and initial access.
- `data/workflows/delegation.toml`: minimum timely deliveries and routine responsibility rules.

All templates are snapshotted into the save. Pending requests, dependencies, commitment reservations, deadlines, reminders, and result counts persist. Re-importing does not reset progress or resend requests. The actual saved file contents and permissions are preserved.

Older address-only Morgan records are migrated to the authored character ID while retaining evidence and conversation history. Existing saves gain missing phase definitions and the new timely-delivery policy field, while retaining their prior authorization status and stored policy values. Mark's new work calendar starts from the installation time in an existing save. An older first request gets a work window starting no earlier than that installation; past progress is not erased or treated as a newly missed deadline.

## Validation

Fifteen C++ test suites cover manual and combined paths, actual server access, delegated checklist delivery, deadlines, queue competition, source text, save/resume, identity migration, and invalid save rejection. Nine converter tests cover the authored graph round trips. Terminal checks exercise the complete manual chain at 80x24 and 120x40, including view reveals and resumed saves. Windows/WezTerm launching continues to use the existing launcher and was not tested here.
