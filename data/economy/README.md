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
simulated work cycles with fixed returns; their slots are separate from the
computer's CPU/RAM process simulation. Pausing and menus stop the game clock.
Working time continues across workdays, so unfinished jobs carry over.

Save snapshots retain catalog definitions, balance, gross earnings, costs,
completed cycles, cooldowns, and pending payments. Reopening a save cannot pay a
completed cycle twice. Changes to the catalog apply to **new games**; existing
saves keep their original definitions and economics. Older saves without an
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
sales have separate cash ledger totals from work income and operating costs. As
with tool definitions, TOML market changes apply to new games; existing saves keep
their market definition. Older saves get an empty portfolio when opened by the UI.

The Web tab opens after researching Web Browsing in the workshop. Browser home provides the
exchange bookmark and URL entry; unknown addresses show a local not-found page.
The exchange is an interactive in-game website, configured by `website_url` and
`website_title`. Trading still requires both money-related capabilities.
