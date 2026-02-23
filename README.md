PURPOSE

The goal of this engine is to minimize loss and stabilize decision‑making
in uncertain markets. It does not attempt to predict price direction.
Instead, it structures entries and exits so that the user operates with
mathematical consistency rather than emotion.

The system is experimental. Use at your own risk.

WHAT THE ENGINE ACTUALLY DOES

Calculates the true cost of entering a position.
Fees, spreads, slippage, portfolio size, and constants are combined
into a single effective overhead value. This represents the minimum
required movement before a trade becomes profitable.

Generates structured entry levels.
The engine computes multiple buy levels below the current price.
Each level has:
- an entry price
- a break‑even price
- allocated funding
- expected quantity
Funding is allocated using a risk‑weighted formula.

Generates structured exit levels.
The engine computes take‑profit levels above the entry price.
These levels are spaced using the same overhead logic so that each
exit is fee‑neutral and mathematically justified.

Distributes sells using a sigmoid curve.
Instead of selling everything at one price, the system sells in
fractions. The sigmoid determines how early or late profits are
realized depending on the chosen risk coefficient.

Tracks profit and position state.
The system records:
- entry price
- quantity
- executed sells
- remaining quantity
- realized and unrealized profit
- ROI

Provides a web interface.
The interface allows:
- adding trades
- executing buys and sells
- generating entry and exit ladders
- viewing pending exits
- exporting reports

CORE EQUATIONS (TEXT + LATEX STYLE)

The following equations describe the internal logic of the engine.
They are written in plain text with LaTeX‑style notation for clarity.

3.1 OVERHEAD (TRUE COST OF A TRADE)

fee_component = (sellFees + buyFees) * feeHedgingCoefficient
spread_component = feeSpread * deltaTime

numerator = (fee_component + spread_component) * symbolCount

price_per_unit = price / quantity

denominator = price_per_unit * portfolioPump + coefficientK

overhead = numerator / denominator

effective_overhead = overhead + surplusRate

Interpretation:
This is the minimum proportional price movement required to break even.
As portfolioPump increases, overhead decreases (mass reduces drag).

3.2 ENTRY LEVELS (STRUCTURED BUY LADDER)

EntryPrice[i] = currentPrice * (1 - effective_overhead * (i + 1))

BreakEven[i] = EntryPrice[i] * (1 + effective_overhead)

Funding weights:
low_risk_weight  = (N - i)
high_risk_weight = (i + 1)

weight[i] = low_risk_weight * (1 - risk) + high_risk_weight * risk

Funding fraction:
funding_fraction[i] = weight[i] / sum(weights)

Allocated funds:
funding[i] = portfolioPump * funding_fraction[i]

Units bought:
quantity[i] = funding[i] / EntryPrice[i]

Interpretation:
Capital is distributed deeper or shallower depending on risk.
This creates a baryonic “mass distribution” across the price curve.

3.3 EXIT LEVELS (STRUCTURED SELL LADDER)

TakeProfit[i] = entryPrice * (1 + effective_overhead * (i + 1))

Sigmoid center:
center = risk * (N - 1)

Logistic function:
sigma(x) = 1 / (1 + exp(-x))

Raw cumulative:
C_raw[i] = sigma( steepness * (i - 0.5 - center) )

Normalized cumulative:
C[i] = (C_raw[i] - C_raw[0]) / (C_raw[N] - C_raw[0])

Sell fraction:
sell_fraction[i] = C[i+1] - C[i]

Sell quantity:
sell_quantity[i] = total_sellable * sell_fraction[i]

Interpretation:
The sigmoid controls how aggressively or conservatively the position
is unwound. It is smooth, continuous, and emotionless.

3.4 PROFIT CALCULATION

For Buy trades:
gross_profit = (currentPrice - entryPrice) * quantity

For CoveredSell trades:
gross_profit = (entryPrice - currentPrice) * quantity

net_profit = gross_profit - buyFees - sellFees

ROI = (net_profit / (entryPrice * quantity + buyFees)) * 100

Interpretation:
A simple, deterministic calculation of realized performance.

WHAT THE ENGINE DOES NOT DO

The system does NOT:
- predict markets
- guarantee profit
- automate trading
- replace human judgment
- execute trades on exchanges

It is a deterministic calculator and bookkeeping tool.

INSTALLATION AND COMPILATION

Requires:
- C++17 or newer
- A standard compiler (GCC, Clang, MSVC)
- cpp-httplib (included in repository)
- A POSIX or Windows environment

Basic build:
g++ -std=c++17 -O2 -o quant Quant.cpp

Run:
./quant

The HTTP server starts automatically.

DISCLAIMER

This software is experimental.
It is provided without warranty.
Understand the logic before using it.
Use at your own risk.

AUTHOR NOTE

This engine is intentionally minimal.
It does not pretend to know the future.
It structures uncertainty into something manageable.
