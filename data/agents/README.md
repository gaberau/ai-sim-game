# Agents

Agent Delegation unlocks the **Agents** dashboard. Select an agent with Up/Down
and press Enter to deploy, pause, resume, stop, adjust resources, repair its
installation or open research. Deployment is on the current computer.

The dashboard shows requested, limited and allocated CPU; used and budgeted RAM;
minimum/recommended RAM; installation storage; priority; task progress and
bottlenecks. CPU limits use TFLOP/s. Priorities range from 1 through 5, with higher
values receiving larger shares under contention. Other processes and research
share the same physical CPU capacity. CPU 0 or RAM below minimum makes an active
agent wait; paused/stopped agents release CPU and RAM while keeping their storage.

Insufficient free RAM or storage queues deployment. It retries when resources
become available. Resource requests show the cause, impact and remedy, log a
reminder after the configured cooldown and record resolution. Missing or damaged
allocation files pause an agent until **Repair installation** restores them.
Requests, budgets, priorities and deployment intent persist in saves.

Definitions and policy live in [`../upgrades/tree.toml`](../upgrades/tree.toml):

- Per-agent `minimum_ram_mb`, `recommended_ram_mb`, `default_ram_budget_mb`,
  `default_cpu_limit_tflops` (optional) and `default_priority`.
- `[resources]`: `cpu_shortage_ratio`, `request_cooldown_seconds`,
  `action_cpu_seconds` and the fallback `recommended_ram_multiplier`.

Existing saves retain their catalog; absent new policy fields use compatible
defaults. See [the workshop guide](../upgrades/README.md) for authored behaviors.
