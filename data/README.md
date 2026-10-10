# Game definitions

These TOML files define starting worlds and reusable content. `config/` holds
UI preferences and keys; `saves/` holds the mutable world. The folders are not
scanned automatically: scenario manifests and explicit `sources` lists decide
what is loaded, in a stable order.

| Folder | Contents | Main file |
| --- | --- | --- |
| applications | Reusable application defaults and workload profiles | custom_email_assistant.toml |
| computers | Hardware, simulation settings, installed process instances; hosts/ holds network device definitions | engineer_pc.toml |
| people | One person per file; office.toml chooses the starting people | office.toml |
| places | Place definitions for future location behavior | office.toml |
| files | Individual simulated file/directory entries grouped by host and path | office.toml |
| scenarios | Manifests choosing the world definitions and seed | hobbyist.toml |
| networks | Topology, links and references to device definitions | office.toml |
| emails | Mailbox transport settings; no legacy starter messages | office.toml |
| news | Individual articles split between world/ and history/ | feed.toml |
| conversations | Mark opening and Jessica Day 1; current chart and story policy | story.toml |
| infrastructure | City utilities, facilities, operators, access boundaries and dependency graph | city.toml |
| economy | Paid work, products and fictional stock-market definitions | tools.toml / stocks.toml |
| agents | Agent documentation; runtime definitions live in upgrades | README.md |
| upgrades | Research tree, trust gates and agent resource costs | tree.toml |

## References and catalogs

References are relative to the TOML file containing them, not to your terminal's
working directory. `../people/office.toml` in scenarios/hobbyist.toml points at
people/office.toml. A catalog contains `schema_version` and a `sources` array
listing its member files. Those files retain the existing record format, e.g.
`[[people]]`, `[[entries]]`, `[[emails]]`, `[[articles]]` or `[[hosts]]`.

Sources append record arrays and combine compatible metadata. Conflicting
scalar metadata, missing files, circular references and invalid records are
rejected. Duplicate IDs are rejected by the category's loader. Simply dropping
a new file into a folder does not activate it: add it to the relevant catalog.

Application profiles use `[application]` and contain default resource/workload
settings plus `[application.details]`. Each installed `[[processes]]` entry
in a computer definition gives its ID, owner, AI ownership and an `application`
reference. An instance can override values, including nested `[processes.details]`.
Host IDs belong to installed instances. Overrides replace arrays rather than
append them. Snapshots store expanded runtime values, not template references.

Filesystem definitions mirror the simulated host/path, e.g.
files/file_server/engineering/deployment-checklist.txt.toml. Directory metadata
has its own TOML file; @root.toml represents a host root. Contents remain inside
the TOML definition. These paths describe simulated files, not host OS files.
Historical workday definitions now exist only in the backend test fixture.

## Migration and saves

First normal launch detects the old data/scenarios/hobbyist/ layout. It validates
the legacy definitions, stages the conversion, validates the new scenario,
then moves the entire previous data directory to backups/data-layout-<number>/.
The generated layout becomes data/. Failed validation leaves the old data
active. Original files, including their comments, remain in the backup; converted
TOML retains values and is reformatted. Unrelated data files are retained.
`ai_sim --migrate-data` performs the same conversion without opening the UI.
Migration is idempotent. Do not copy the old scenario folder back into data:
that is the trigger for legacy conversion.

Existing saves keep their state, file IDs, permissions, conversations and queues.
Changing a definition changes new-game defaults. `email import` and `workday import`
continue to merge missing content without resetting existing saved records.
Place, agent and upgrade data do not add behavior that the backend lacks yet.
