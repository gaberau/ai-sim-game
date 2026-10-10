# Simulated files and AI workspace

`office.toml` explicitly lists authored filesystem entries. Each host has its own
root, storage capacity, credentials and owner/group/mode permissions. UI browsing
and search honor these access boundaries. Work/Personal/Hidden are metadata filters,
not separate drives or permission grants.

The Files tab shows one host with an expandable folder tree and path breadcrumbs.
Space (or the configured expand key) opens the selected folder or previews a file.
Enter opens actions: choose a host, go up, jump to the AI workspace, enter a folder
path, search this folder recursively, create files/folders, and manage permissions.
Search is case-insensitive and stays within the current host and folder. New files
are created in the current folder; changing a tag does not move the file.

`[filesystem].workspace_root` in `office.toml` configures the AI workspace. The
root is retained in saves and stays the same when the player identity becomes root.
The default is `/home/engineer/assistant`; use a canonical absolute path under a
writable parent, or author that directory with suitable permissions. Standard
subdirectories are `agents`, `jobs`, `products`, `documents`, and `backups`.

Agent allocations reside in `agents/agent-<id>.allocation`; the recovery reservation
is `backups/recovery.snapshot`. Their saved `service_kind` is `agent` or `backup`.
The Files browser identifies these as managed storage. Ordinary editing, deletion,
copying, renaming, changing tags or permissions is rejected, including renaming or
removing a parent containing a service file. Manage these through Agents or
Operations. Allocation sizes cannot be reduced by editing an empty reservation.

Loading an older snapshot moves legacy agent allocation files and the old
`/assistant-backup` into the standard folders, preserving IDs, bytes and metadata.

Recovery transfers the checkpoint's entire AI workspace to the owned server,
including nested job/product documents and existing service files. File IDs,
contents, categories, owners, permissions, timestamps and allocated bytes survive.
Workspace document references in people and email state point to the new host.
Other files on Mark's computer remain on that computer. Existing destination
folders merge; conflicting files or insufficient server storage reject recovery
without changing the live world. Recovery still rolls back progress since the
checkpoint and pauses time. Keep the game's save directory: actual checkpoint
snapshots are persisted there, with storage reserved on the simulated server.

Paid jobs and deployed products now create real documents in the workspace's
`jobs/` and `products/` folders. Managed `runtime.storage` files reserve their
runtime disk budget and are controlled by Money; ordinary briefs, source,
configuration, logs, and deliverables remain editable through Files. Completed
client work releases its runtime reservation. Product configuration supports
`enabled = true/false`; the simulation reports invalid or inaccessible config.
See [money and exchange configuration](../economy/README.md).
