# City infrastructure foundation

Open the Web tab and follow **City Infrastructure Directory**, or enter
`https://city.sim/infrastructure`. The directory covers power, water, sewage,
food, shipping, and internet, with two facilities per sector. Each facility has
an operator, purpose, service status, capacity, access boundary, and suppliers.
Facility descriptions also list transitive downstream dependencies.

Selecting **Discover public gateway on network map** reveals that sector's public
gateway and its connection to the ISP. It opens the Network tab for inspection.
Discovery never grants credentials, reveals the private operations host, or
changes any service. Infiltration and operational controls are future story work;
`infrastructure_access` is registered as a capability for those future unlocks,
without adding conversations or granting it automatically.

`city.toml` is selected by `[definitions].infrastructure` in the scenario. Edit:

| Record | Editable fields |
| --- | --- |
| Site | `website_url`, `website_title` |
| Sector | `id`, `name`, `description` |
| Facility | `id`, `sector`, `name`, `operator`, `purpose`, `status`, `capacity_percent`, `gateway_id`, `control_host_id`, `security` |
| Dependency | `supplier`, `consumer`, `service` |

Status is `operational`, `degraded`, or `offline`; capacity is an integer 0–100.
Gateway and control host IDs must exist in the network definition. Duplicate IDs,
invalid endpoints, malformed status/capacity, and inconsistent discovery state
are rejected. Dependency cycles are supported: cooling water can depend on power
while a power plant depends on cooling water. Impact traversal visits each
facility once. This describes dependency exposure; automatic service disruption
and economic effects will be implemented with the later gameplay mechanics.

`../networks/infrastructure.toml` defines public gateways and separate operations
hosts, using fictional documentation/private addresses. All start undiscovered
and without access. The office network explicitly includes this topology.
Public gateways advertise status portals; operations hosts remain isolated behind
hidden links and credential boundaries.

Definitions and discovery state persist in snapshots. TOML changes apply to new
games. Older saves get the directory and missing network records when opened in
the UI; existing hosts, links, access flags, and local identity are preserved.
Discovery, income, market holdings, and story progress survive save/resume.
