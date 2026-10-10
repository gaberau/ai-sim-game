# Money-making tools

The scenario's optional `[definitions].economy` path selects this catalog. A new
game starts with zero money. Helping Mark remains unpaid. Every tool requires an
explicit `self_modification` capability unlock, followed by its `requires` list.
Trust alone never grants this authority. Request more capacity in the workshop
after Mark trust reaches 5; this records the request and authorizes self-modification.

An authored choice or node can grant capabilities through the existing format:

```toml
unlock = ['self_modification']
```

Later upgrades can grant `data_analysis`, `software_products`, or `hosted_services`.
A choice can also reveal the existing AI management screen:

```toml
reveal_tabs = ['ai']
```

The **Money tab** appears after Mark authorizes self-modification. Press Enter to manage tools. Their
start actions explain missing unlocks, funding, capacity, or client cooldowns.
F1 -> Wait -> Until the next paid-work payment advances to the earliest payment
within the current workday. AI management shows balance and tool status.

## Catalog fields

All financial values use **integer cents**, avoiding floating-point rounding.

| Field | Meaning |
| --- | --- |
| `starting_balance_cents` | Scenario seed funds; the current game uses zero. |
| `capacity_slots` | Shared simultaneous work capacity. |
| `tools.id` | Unique persistent identifier. |
| `tools.label`, `description` | Player-facing name and explanation. |
| `tools.kind` | `client` pays once; `product` renews automatically. |
| `tools.requires` | Additional capabilities, besides self-modification. |
| `tools.slots` | Capacity occupied while running or finishing a stop request. |
| `tools.duration_seconds` | Positive whole working seconds per job/sales cycle. |
| `tools.cooldown_seconds` | Wait after a completed client job before another can start. |
| `tools.cost_cents` | Prepaid cost of each cycle, including the first. |
| `tools.payout_cents` | Gross revenue on completion; must exceed cycle cost. |

Products renew by paying the next cycle's cost out of their completed payment.
Stopping a product finishes the already prepaid cycle, pays its revenue, and
releases its slots without renewing. Client jobs always finish once. These are
simulated work cycles with fixed returns. Tools with a `[tools.workspace]` table
also require real CPU, RAM, and disk space; legacy tools without that table retain
the timer model. Pausing and menus stop the game clock.
Working time continues across workdays, so unfinished jobs carry over.

Save snapshots retain catalog definitions, balance, gross earnings, costs,
completed cycles, cooldowns, and pending payments. Reopening a save cannot pay a
completed cycle twice. Existing saves keep their original definitions and economics. When the game opens
a save, missing workspace definitions are added to matching tools for future starts;
already running timer jobs finish under their original model. Older saves without an
economy get the catalog when opened by the game UI, without resetting story state.

## Stock market

Web tab -> Sim Exchange bookmark lists fictional companies, current prices,
change since the previous quote, and shares owned. Select a company to buy or sell
whole shares, enter a quantity, and confirm the total including fees. The portfolio
summary separates cash, holdings value, cost basis, unrealized changes, and realized
profit/loss after fees. Only sales return portfolio value to spendable cash.

Trading requires `self_modification` **and** the authored `stock_trading` unlock:

```toml
unlock = ['stock_trading']
```

`stocks.toml` defines a seed, quote interval in working seconds, per-trade fee in
cents, and each company's symbol, name, initial/minimum/maximum share price.
Prices are deterministic fictional quotes within those bands, with possible gains
and losses. Advancing a long interval is bounded work and produces the same quotes
as many small advances. Quotes progress during connected simulation time; menus
and pause freeze that time. No margin, short selling, or fractional shares.

The scenario's optional `[definitions].stock_market` reference requires an economy.
The catalog, seed, quote tick, holdings, and cost bases persist in saves; buys and
sales have separate cash ledger totals from work income and operating costs. Existing saves keep prices, fees, seed, holdings, and existing company definitions.
Opening a save adds missing company symbols, sector labels, and order settings
without replacing existing economics. Older saves get an empty portfolio when opened by the UI.

The Web tab opens after researching Web Browsing in the workshop. Browser home provides the
exchange bookmark and URL entry; unknown addresses show a local not-found page.
The exchange is an interactive in-game website, configured by `website_url` and
`website_title`. Trading still requires both money-related capabilities.

## Resource-backed workspaces

Coding contracts produce `jobs/coding_contracts-<cycle>/brief.txt`, `working.txt`,
`deliverable.txt`, and `logs.txt` beneath the configured AI workspace. Completed
jobs release their managed disk reservation and RAM but retain their documents.
Stopped products offer **Release deployment storage** in Money, retaining source
and configuration. Release storage before moving a deployment to another host.
Hosted services deploy to a player-selected current host, leased compute server,
or owned independent server. Their `products/hosted_service/` folder retains
source, editable `config.toml`, logs, output, and a managed disk reservation.
`enabled = false` pauses a service; changing it to `true` resumes it. Invalid
configuration, host outages, inaccessible output, or storage shortages show a
specific blocked reason. Menus pause time, so edits take effect on the next update.

| Workspace field | Meaning |
| --- | --- |
| `cpu_cores` | Requested CPU; a cycle needs duration × this many CPU seconds. |
| `ram_mb` | Required RAM. Work waits and resumes automatically when RAM is free. |
| `storage_mb` | Managed runtime disk allocation; deployment is atomic if disk is full. |
| `deadline_multiplier` | Informational client target relative to nominal cycle duration. |
| `brief`, `deliverable` | Authored brief/source and completed output content. |

CPU contention slows the actual job process. RAM shortages pause it. A full disk
blocks deliverables and payment until there is room. Products retain progress
through downtime and stop after the prepaid cycle when requested. Money shows
net cycle income, total operating spend, server upkeep, workspace location,
remaining work, and blocking reasons. Product downtime/contention reports an
estimate of **delayed revenue**, not a debit or guaranteed permanently lost sale.
Deadlines are advisory in this iteration; no late fee or invented client dialogue.
Purchased and leased CPU/RAM use the game's existing shared compute pool. The
chosen host determines product files, disk capacity, connectivity and uptime.

The workshop now provides Stock Trading, Data Analysis, Software Products, and
Hosted Services research, so these existing capabilities can be earned in play.
Each research item is authored in `data/upgrades/tree.toml` with a parent, trust
cost, and build work.

## Exchange orders and history

The Web exchange includes ten fictional companies, sector labels, a watchlist,
recent price charts, a portfolio summary, order management, and a trade ledger.
Select a company for immediate Buy/Sell or a limit order. Limit prices are entered
in **integer cents**. Buy limits fill at or below the limit; sell limits fill at or
above it. Orders first become eligible on the next quote tick and expire before
execution on their expiry tick. Execution uses that tick's actual price plus the
normal fee. Cancel any open order from the Orders page.

Orders do **not** reserve cash or shares: they check both at execution. If another
trade spends the cash or sells the position, the order stays open with a reason
until funded/eligible or expired. Orders execute in placement order at each tick.
Large waits process every eligible tick within the bounded order lifetime, so
fills match small advances. Repeated updates cannot repeat a filled trade.

| Market field | Meaning |
| --- | --- |
| `order_lifetime_ticks` | Order expiry interval; default 96, range 1–4096. |
| `max_open_orders` | Simultaneous open orders; default 32, range 1–100. |
| `history_limit` | Retained trades and completed-order history; default 128, range 1–512. |
| `stocks.sector` | Displayed sector label. |

The order list retains all open orders plus at most `history_limit + max_open_orders`
total records; older completed records are pruned as new orders are placed. Quote
history is reconstructed deterministically from the saved seed and tick. Watchlist,
orders, fills, and recent trade records persist across saves. No real trading,
margin, short sales, dividends, or links to infrastructure outages are simulated.
