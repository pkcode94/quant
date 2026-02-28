# Quant Trade Manager — Complete Mathematical Reference

All equations extracted from the engine source code. Variables are defined once in §1 and reused throughout.

---

## §1 — Notation & Parameters

| Symbol | Description | Domain |
|--------|-------------|--------|
| $P$ | Current market price per unit | $P > 0$ |
| $P_e$ | Entry price per unit | $P_e > 0$ |
| $q$ | Quantity (units held) | $q > 0$ |
| $N$ | Number of levels (horizons / entries / exits) | $N \geq 1$ |
| $i$ | Level index | $0 \leq i \leq N-1$ |
| $f_s$ | Fee spread / slippage rate | $f_s \geq 0$ |
| $f_h$ | Fee hedging coefficient (safety multiplier) | $f_h \geq 1$ |
| $\Delta t$ | Delta time | $\Delta t > 0$ |
| $n_s$ | Symbol count in portfolio | $n_s \geq 1$ |
| $T$ | Portfolio pump (injected capital for period $t$) | $T \geq 0$ |
| $K$ | Coefficient K (additive denominator offset) | $K \geq 0$ |
| $s$ | Surplus rate (profit margin above break-even) | $s \geq 0$ |
| $r$ | Risk coefficient | $r \in [0, 1]$ |
| $\alpha$ | Steepness (sigmoid sharpness) | $\alpha > 0$ |
| $R_{\max}$ | Max risk (TP ceiling fraction) | $R_{\max} \geq 0$ |
| $R_{\min}$ | Min risk (TP floor fraction above break-even) | $R_{\min} \geq 0$ |
| $\phi$ | Exit fraction (portion of holdings to sell) | $\phi \in [0, 1]$ |
| $\phi_{\text{sl}}$ | SL fraction (portion of position to sell at SL) | $\phi_{\text{sl}} \in [0, 1]$ |
| $n_f$ | Future trade count (chain trades whose fees to pre-hedge) | $n_f \geq 0$ |
| $n_{\text{sl}}$ | SL hedge count (future SL hits to pre-fund) | $n_{\text{sl}} \geq 0$ |
| $f_{\text{buy}}$ | Buy fee rate | $f_{\text{buy}} \geq 0$ |
| $f_{\text{sell}}$ | Sell fee rate | $f_{\text{sell}} \geq 0$ |

---

## §2 — Core Overhead & Position Sizing

### 2.1 — Fee Component

$$
\mathcal{F} = f_s \cdot f_h \cdot \Delta t
$$

### 2.2 — Raw Overhead

$$
\boxed{
\text{OH}(P, q) = \frac{\mathcal{F} \cdot n_s \cdot (1 + n_f)}{\dfrac{P}{q} \cdot T + K}
}
$$

The overhead normalises fee costs against the per-unit price ratio scaled by pump capital, with $K$ as an additive stabiliser. The factor $(1 + n_f)$ scales the overhead so this trade's TP covers fees for $n_f$ future chain trades ($n_f = 0$ means self only).

### 2.3 — Effective Overhead

$$
\boxed{
\text{EO}(P, q) = \text{OH}(P, q) + s \cdot f_h \cdot \Delta t + f_s \cdot f_h \cdot \Delta t
}
$$

Equivalently:

$$
\text{EO} = \text{OH} + (s + f_s) \cdot f_h \cdot \Delta t
$$

### 2.4 — Position Delta

$$
\delta = \frac{P \cdot q}{T}
$$

The portfolio weight of this position.

### 2.5 — Funded Quantity

$$
q_{\text{funded}} = \frac{T}{P}
$$

Additional units the pump capital buys at price $P$.

$$
q_{\text{total}} = q + q_{\text{funded}}
$$

---

## §3 — Sigmoid Building Block

All distribution curves use the logistic sigmoid:

$$
\sigma(x) = \frac{1}{1 + e^{-x}}
$$

### 3.1 — Normalised Sigmoid Mapping

For a parameter $t \in [0, 1]$ and steepness $\alpha$:

$$
\sigma_0 = \sigma\!\left(-\frac{\alpha}{2}\right), \qquad
\sigma_1 = \sigma\!\left(+\frac{\alpha}{2}\right)
$$

$$
\boxed{
\hat\sigma(t) = \frac{\sigma\!\bigl(\alpha \cdot (t - 0.5)\bigr) - \sigma_0}{\sigma_1 - \sigma_0}
}
$$

This maps $t \in [0,1]$ to a normalised $[0,1]$ sigmoid curve. When $\alpha \to 0$, $\hat\sigma(t) \to t$ (linear). When $\alpha \gg 1$, $\hat\sigma(t)$ approaches a step function.

---

## §4 — Market Entry Calculator

Generates $N$ entry price levels with sigmoid-distributed prices and risk-warped funding allocation.

### 4.1 — Price Range

**Default mode** (no explicit range):

$$
P_{\text{low}} = 0, \qquad P_{\text{high}} = P
$$

**Range mode** ($R_{\text{above}} > 0$ or $R_{\text{below}} > 0$):

$$
P_{\text{low}} = \max\!\bigl(P - R_{\text{below}},\; \varepsilon\bigr), \qquad P_{\text{high}} = P + R_{\text{above}}
$$

### 4.2 — Sigmoid Normalisation per Level

$$
t_i = \begin{cases}
\dfrac{i}{N-1} & \text{if } N > 1 \\[6pt]
1 & \text{if } N = 1
\end{cases}
$$

$$
n_i = \hat\sigma(t_i)
$$

### 4.3 — Entry Price

$$
\boxed{
P_e^{(i)} = P_{\text{low}} + n_i \cdot (P_{\text{high}} - P_{\text{low}})
}
$$

Level 0 is the deepest discount (near $P_{\text{low}}$); level $N{-}1$ is nearest to market.

### 4.4 — Break-Even Price

$$
\boxed{
P_{\text{BE}}^{(i)} = P_e^{(i)} \cdot \bigl(1 + \text{OH}(P, q)\bigr)
}
$$

The minimum price to recover all embedded fees and overhead.

### 4.5 — Funding Allocation (Risk-Warped Sigmoid)

The risk coefficient $r$ warps a sigmoid funding curve:

| $r$ | Behaviour |
|-----|-----------|
| $0$ | Conservative — more funds at higher (safer) entry prices |
| $0.5$ | Uniform distribution |
| $1$ | Aggressive — more funds at lower (deeper discount) entry prices |

**Weight per level:**

$$
w_i = (1 - r) \cdot n_i + r \cdot (1 - n_i)
$$

**Funding fraction:**

$$
\boxed{
F_i = \frac{w_i}{\displaystyle\sum_{j=0}^{N-1} w_j}
}
$$

**Capital allocated:**

$$
\text{Funding}_i = T_{\text{avail}} \cdot F_i
$$

**Units purchased at this entry:**

$$
q_i = \frac{\text{Funding}_i}{P_e^{(i)}}
$$

### 4.6 — Potential Net Profit

If price returns to current market $P$ after buying at discount:

$$
\Pi_{\text{net}}^{(i)} = (P - P_e^{(i)}) \cdot q_i
$$

### 4.7 — Cost Coverage

$$
C_i = i + 1
$$

The number of layers of overhead baked into deeper entry levels.

---

## §5 — Multi-Horizon Engine (TP/SL Generation)

Generates take-profit and stop-loss levels for an existing position.

### 5.1 — Standard Mode ($R_{\max} = 0$)

$$
\text{base} = P_e \cdot q
$$

$$
\boxed{
\text{TP}_i = \text{base} \cdot \bigl(1 + \text{EO} \cdot (i + 1)\bigr)
}
$$

$$
\boxed{
\text{SL}_i = \text{base} \cdot \bigl(1 - \text{EO} \cdot (i + 1)\bigr)
}
$$

Each successive level scales the effective overhead linearly. TP/SL values are in **total** (price × quantity), not per-unit.

### 5.2 — Max-Risk Mode ($R_{\max} > 0$)

When $R_{\max} > 0$, the factor is sigmoid-interpolated between overhead and max risk:

$$
f_{\min} = \text{OH}(P_e, q), \qquad f_{\max} = \max(R_{\max},\; f_{\min})
$$

$$
t_i = \begin{cases}
\dfrac{i}{N-1} & \text{if } N > 1 \\[4pt]
1 & \text{if } N = 1
\end{cases}
$$

$$
n_i = \hat\sigma_{\alpha=4}(t_i)
$$

$$
\text{factor}_i = f_{\min} + n_i \cdot (f_{\max} - f_{\min})
$$

$$
\boxed{
\text{TP}_i = \text{base} \cdot (1 + \text{factor}_i)
}
$$

$$
\text{SL}_i = \text{base} \cdot (1 - \text{factor}_i)
$$

### 5.3 — Per-Level TP with Risk Warping (`levelTP`)

Used by the Serial Generator for per-unit TP targets with risk-warped distribution.

Let $P_{\text{ref}}$ = reference price (highest entry, or current price). Steepness is halved ($\alpha' = \alpha / 2$) to avoid over-compression:

$$
t_i = \frac{i + 1}{N + 1}
$$

$$
n_i^{\text{raw}} = \hat\sigma_{\alpha'}(t_i)
$$

**Risk warping** (mirrors funding allocation):

$$
n_i = (1 - r) \cdot n_i^{\text{raw}} + r \cdot (1 - n_i^{\text{raw}})
$$

**LONG positions:**

$$
\text{TP}_{\min} = P_e \cdot (1 + \text{EO} + R_{\min})
$$

$$
\text{TP}_{\max} = P_{\text{ref}} \cdot (1 + R_{\max})
$$

$$
\boxed{
\text{TP}_i^{\text{long}} = \text{TP}_{\min} + n_i \cdot (\text{TP}_{\max} - \text{TP}_{\min})
}
$$

**SHORT positions:**

$$
\text{TP}_{\max}^{\text{short}} = P_e \cdot (1 - \text{EO} - R_{\min})
$$

$$
\text{TP}_{\text{floor}} = \max\!\bigl(P_{\text{ref}} \cdot (1 - R_{\max}),\; 0\bigr)
$$

$$
\boxed{
\text{TP}_i^{\text{short}} = \text{TP}_{\max}^{\text{short}} - n_i \cdot (\text{TP}_{\max}^{\text{short}} - \text{TP}_{\text{floor}})
}
$$

### 5.4 — Per-Level Stop-Loss (`levelSL`)

$$
\boxed{
\text{SL}^{\text{long}} = P_e \cdot (1 - \text{EO})
}
$$

$$
\boxed{
\text{SL}^{\text{short}} = P_e \cdot (1 + \text{EO})
}
$$

### 5.4b — Fractional Stop-Loss Exit

The SL fraction $\phi_{\text{sl}} \in [0, 1]$ controls how much of the position is sold when the SL price is hit:

$$
q_{\text{sl}} = q_i \cdot \phi_{\text{sl}}
$$

At $\phi_{\text{sl}} = 1$, the entire position exits (default). At $\phi_{\text{sl}} = 0.25$, only a quarter sells — the rest remains open. The loss at SL hit is:

$$
\text{Loss}_{\text{sl}} = -\text{EO} \cdot P_e \cdot q_i \cdot \phi_{\text{sl}}
$$

### 5.4c — Capital-Loss Cap (`clampStopLossFraction`)

The integrated worst-case SL loss across all $N$ levels must not exceed the available capital $T_{\text{avail}}$:

$$
\text{TotalSLLoss} = \phi_{\text{sl}} \cdot \sum_{i=0}^{N-1} \text{EO} \cdot \text{Funding}_i
$$

$$
\boxed{
\phi_{\text{sl}}^{\text{clamped}} = \begin{cases}
\phi_{\text{sl}} & \text{if TotalSLLoss} \leq T_{\text{avail}} \\[4pt]
\phi_{\text{sl}} \cdot \dfrac{T_{\text{avail}}}{\text{TotalSLLoss}} & \text{otherwise}
\end{cases}
}
$$

This guarantees that even if every SL across all levels triggers simultaneously, the total realised loss is bounded by $T_{\text{avail}}$.

### 5.5 — Downtrend Buffer (`calculateDowntrendBuffer`)

Computes a position-derived TP multiplier with **axis-dependent sigmoid curvature**.
The buffer scales TP upward to pre-fund $n_d$ future downturn cycles, shaped by a
normalised sigmoid (§3.1) whose steepness and input are both determined by the
position delta $\delta$ (§2.4).

The sigmoid interpolates between $R_{\min}$ (lower asymptote) and an upper bound.
When $R_{\max} > 0$, it caps the per-cycle buffer. When $R_{\max} = 0$, the upper
bound falls back to $\text{EO}$ (which embeds $\Delta t$ via the fee component,
making the buffer time-sensitive).

Because $\delta = P \cdot q \,/\, T$, the curvature is inherently dependent on where
the position sits in the price × quantity space:

| $\delta$ | Steepness | Curvature | Buffer behaviour |
|----------|-----------|-----------|------------------|
| $\ll 1$ | Near-linear | Gentle | Per-cycle $\approx R_{\min}$ — minimum protection |
| $\approx 1$ | Mild S-curve | Moderate | Per-cycle at sigmoid midpoint |
| $\gg 1$ | Steep S-curve | Sharp | Per-cycle saturates at upper bound |
| $= 0$ | — | — | Buffer $= 1$ — nothing deployed |

| $n_d$ | Meaning |
|-------|---------|
| $0$ | Downtrend hedging disabled — buffer $= 1$ |
| $1$ | Pre-fund one downturn cycle (default) |
| $n$ | Pre-fund $n$ downturn cycles |

**Asymptotes:**

$$
\text{upper} = \begin{cases}
R_{\max} & \text{if } R_{\max} > 0 \\
\text{EO} & \text{otherwise (time-sensitive fallback)}
\end{cases}
$$

$$
\text{lower} = R_{\min}
$$

**Hyperbolic compression** (maps $\delta$ to $[0, 1)$):

$$
t = \frac{\delta}{\delta + 1}
$$

**Axis-dependent steepness:**

$$
\alpha_d = \max(\delta,\; 0.1)
$$

**Per-cycle buffer** (sigmoid between asymptotes):

$$
\text{pc} = R_{\min} + \hat\sigma_{\alpha_d}(t) \cdot (\text{upper} - R_{\min})
$$

**Downtrend buffer:**

$$
\boxed{
\text{buffer} = 1 + n_d \cdot \text{pc}
}
$$

$$
\text{TP}_{\text{adj}} = \text{TP}_{\text{base}} \cdot \text{buffer}
$$

When $n_d = 0$ or $\delta = 0$, the multiplier is $1$ (no adjustment).
When $R_{\min} = R_{\max} = 0$, the upper bound is $\text{EO}$ and the lower is $0$,
preserving time sensitivity through $\Delta t$.

### 5.6 — Stop-Loss Hedge Buffer (`calculateStopLossBuffer`)

Mirrors the downtrend buffer but pre-funds potential future SL hits. Each pre-funded SL costs a fraction $\phi_{\text{sl}}$ of the per-cycle amount:

$$
\boxed{
\text{buffer}_{\text{sl}} = 1 + n_{\text{sl}} \cdot \phi_{\text{sl}} \cdot \text{pc}
}
$$

where $\text{pc}$ is the same per-cycle cost from §5.5, $n_{\text{sl}}$ is the number of future SL hits to pre-fund, and $\phi_{\text{sl}}$ is the SL fraction (§5.4b).

The combined TP multiplier applies both buffers:

$$
\text{TP}_{\text{adj}} = \text{TP}_{\text{base}} \cdot \text{buffer}_{\text{dt}} \cdot \text{buffer}_{\text{sl}}
$$

---

## §6 — Exit Strategy Calculator

Distributes a sigmoidal fraction of holdings across ascending TP levels as pending sell orders.

### 6.1 — TP Price per Level

**Standard mode** ($R_{\max} = 0$):

$$
\boxed{
P_{\text{TP}}^{(i)} = P_e \cdot \bigl(1 + \text{EO} \cdot (i + 1)\bigr)
}
$$

**Max-risk mode** ($R_{\max} > 0$): same sigmoid interpolation as §5.2.

### 6.2 — Sigmoid Cumulative Sell Distribution

The cumulative fraction sold follows a logistic curve whose centre is controlled by risk:

$$
c = r \cdot (N - 1)
$$

| $r$ | Centre position | Behaviour |
|-----|-----------------|-----------|
| $0$ | Near level 0 | Sell heavy early (lock in gains) |
| $0.5$ | Middle | Balanced |
| $1$ | Near level $N{-}1$ | Hold for deeper TP (maximise upside) |

$$
C_i = \sigma\!\bigl(\alpha \cdot (i - 0.5 - c)\bigr) \qquad \text{for } i = 0, 1, \ldots, N
$$

$$
\hat{C}_i = \frac{C_i - C_0}{C_N - C_0}
$$

### 6.3 — Sell Quantity per Level

$$
q_{\text{sell}} = q \cdot \phi
$$

$$
\boxed{
q_i^{\text{sell}} = q_{\text{sell}} \cdot \bigl(\hat{C}_{i+1} - \hat{C}_i\bigr)
}
$$

The fraction of sellable position at each level:

$$
f_i^{\text{sell}} = \hat{C}_{i+1} - \hat{C}_i
$$

### 6.4 — Per-Level Profit

$$
\text{Value}_i = P_{\text{TP}}^{(i)} \cdot q_i^{\text{sell}}
$$

$$
\text{Gross}_i = \bigl(P_{\text{TP}}^{(i)} - P_e\bigr) \cdot q_i^{\text{sell}}
$$

$$
\text{BuyFee}_i = F_{\text{buy}}^{\text{total}} \cdot f_i^{\text{sell}}
$$

$$
\boxed{
\text{Net}_i = \text{Gross}_i - \text{BuyFee}_i - \text{SellFee}_i
}
$$

**Cumulative:**

$$
\text{Net}_{\text{cum}}^{(i)} = \sum_{j=0}^{i} \text{Net}_j
$$

---

## §7 — Serial Generator

Combines Entry (§4) + TP (§5.3) + SL (§5.4) into a unified tuple per level. For each level $i$:

$$
P_e^{(i)} = P_{\text{low}} + \hat\sigma(t_i) \cdot (P_{\text{high}} - P_{\text{low}})
$$

$$
P_{\text{BE}}^{(i)} = P_e^{(i)} \cdot (1 + \text{OH})
$$

$$
\text{TP}^{(i)} = \texttt{levelTP}\!\bigl(P_e^{(i)},\; \text{OH},\; \text{EO},\; \alpha,\; i,\; N,\; r,\; P_{\text{high}}\bigr)
$$

$$
\text{SL}^{(i)} = \texttt{levelSL}\!\bigl(P_e^{(i)},\; \text{EO}\bigr)
$$

$$
\text{Funding}_i = T_{\text{avail}} \cdot F_i, \qquad q_i = \frac{\text{Funding}_i}{P_e^{(i)}}
$$

$$
\text{Cost}_i = P_e^{(i)} \cdot q_i
$$

$$
\text{TP Gross}_i = \text{TP}^{(i)} \cdot q_i - \text{Cost}_i
$$

$$
\text{SL Loss}_i = \text{SL}^{(i)} \cdot q_i - \text{Cost}_i
$$

$$
\text{Discount}_i = \frac{P - P_e^{(i)}}{P} \times 100\%
$$

---

## §8 — Profit Calculator

### 8.1 — Unrealised Profit (Open Position)

**LONG:**

$$
\text{Gross} = (P_{\text{now}} - P_e) \cdot q
$$

**SHORT:**

$$
\text{Gross} = (P_e - P_{\text{now}}) \cdot q
$$

$$
\text{Net} = \text{Gross} - F_{\text{buy}} - F_{\text{sell}}
$$

$$
\boxed{
\text{ROI} = \frac{\text{Net}}{P_e \cdot q + F_{\text{buy}}} \times 100\%
}
$$

### 8.2 — Realised Profit (Child Sell vs Parent Buy)

$$
\text{Gross} = (P_{\text{sell}} - P_{\text{entry}}) \cdot q_{\text{sold}}
$$

$$
\text{Net} = \text{Gross} - F_{\text{buy}} - F_{\text{sell}}
$$

$$
\text{ROI} = \frac{\text{Net}}{P_{\text{entry}} \cdot q_{\text{sold}} + F_{\text{buy}}} \times 100\%
$$

---

## §9 — Simulator

### 9.1 — Entry Trigger

At each timestep with price $P_t$, for each unfilled entry level $i$:

$$
\text{Buy if } P_t \leq P_e^{(i)} \text{ and } \text{Cost}_i + \text{Fee}_i \leq \text{Capital}
$$

$$
\text{Fee}_i = P_e^{(i)} \cdot q_i \cdot f_{\text{buy}}
$$

$$
\text{Capital} \leftarrow \text{Capital} - (P_e^{(i)} \cdot q_i + \text{Fee}_i)
$$

### 9.2 — Exit Trigger

For each open position with exit level $j$:

$$
\text{Sell if } P_t \geq P_{\text{TP}}^{(j)}
$$

$$
q_j^{\text{sell}} = \min\!\bigl(q_j^{\text{exit}},\; q_{\text{remaining}}\bigr)
$$

$$
\text{SellFee}_j = P_{\text{TP}}^{(j)} \cdot q_j^{\text{sell}} \cdot f_{\text{sell}}
$$

$$
\text{Gross}_j = \bigl(P_{\text{TP}}^{(j)} - P_e\bigr) \cdot q_j^{\text{sell}}
$$

$$
\text{Net}_j = \text{Gross}_j - \text{SellFee}_j
$$

$$
\text{Capital} \leftarrow \text{Capital} + P_{\text{TP}}^{(j)} \cdot q_j^{\text{sell}} - \text{SellFee}_j
$$

### 9.3 — Snapshot (per timestep)

$$
\text{Deployed} = \sum_{\text{open}} P_e^{(k)} \cdot q_{\text{remaining}}^{(k)}
$$

$$
\text{Total} = \text{Capital} + \text{Deployed}
$$

### 9.4 — Fee Hedging Coverage

$$
\text{HedgePool} = \sum_{\text{all exits}} \text{Gross}_j
$$

$$
\boxed{
\text{Coverage} = \frac{\text{HedgePool}}{\text{TotalFees}}
}
$$

Coverage $\geq 1$ means the overhead formula fully covered all trading fees.

### 9.5 — Chained Cycles

When chain mode is enabled, the simulator re-generates entry levels after each
completed cycle (all positions from the current cycle are closed). This creates
a sequence of cycles where each cycle's parameters are derived from the previous
cycle's outcome.

**Cycle transition:** when all positions from cycle $c$ are closed:

$$
\Pi_c = \sum_{\substack{j \in \text{sells} \\ \text{cycle}(j) = c}} \text{Net}_j
$$

$$
\text{Savings}_c = \Pi_c \cdot s_{\text{save}} \qquad (s_{\text{save}} \in [0,1])
$$

$$
T_{c+1} = \text{Capital} - \text{Savings}_c
$$

The engine then generates new serial entries at the current market price $P_t$
with pump $T_{c+1}$. Because $P$ and $T$ have changed, the new entries occupy
different price levels with different funding allocations:

$$
P_e^{(i,c+1)},\; q_i^{(c+1)},\; \text{TP}^{(i,c+1)},\; \text{SL}^{(i,c+1)}
$$

The entry levels form a **superposition** — all levels exist as potential states
until the market price observes (reaches) one, collapsing it into an open position
and triggering the chain toward the next cycle.

**Cumulative savings:**

$$
\text{Savings}_{\text{total}} = \sum_{c=0}^{C-1} \text{Savings}_c
$$

**Total wealth:**

$$
\text{Wealth} = \text{Capital}_{\text{final}} + \text{Savings}_{\text{total}}
$$

---

## §10 — Summary Diagram

```
                    ┌──────────────────────────────────────────────────────────────┐
                    │                    PARAMETER SPACE                          │
                    │  f_s, f_h, Δt, n_s, T, K, s, r, α, R_max, R_min, φ       │
                    └──────────┬──────────────┬──────────────┬────────────────────┘
                               │              │              │
                    ┌──────────▼──────┐ ┌─────▼──────┐ ┌────▼─────────────┐
                    │   §2 Overhead   │ │ §3 Sigmoid │ │  §4 Entry Calc   │
                    │ OH = F·n_s/den  │ │  σ(x) base │ │  P_e, BE, Fund   │
                    │ EO = OH + ...   │ │  normalised │ │  risk-warped     │
                    └──────┬──────────┘ └──────┬──────┘ └───────┬──────────┘
                           │                   │                │
                    ┌──────▼───────────────────▼────────────────▼──────────┐
                    │                §5 Horizon Engine                     │
                    │   TP_i = base · (1 + factor_i)                      │
                    │   SL_i = base · (1 - factor_i)                      │
                    │   levelTP: risk-warped sigmoid between min/max TP    │
                    └──────┬──────────────────────────────────┬────────────┘
                           │                                  │
                    ┌──────▼──────────┐              ┌───────▼────────────┐
                    │ §6 Exit Strategy│              │ §7 Serial Generator│
                    │ Sigmoid sell    │              │ Entry + TP + SL    │
                    │ distribution    │              │ unified tuples     │
                    └──────┬──────────┘              └───────┬────────────┘
                           │                                  │
                    ┌──────▼──────────────────────────────────▼────────────┐
                    │               §9 Simulator                          │
                    │   Step through price series                         │
                    │   Buy at entry triggers, sell at TP triggers        │
                    │   Track capital, fees, P&L, fee hedging coverage    │
                    └─────────────────────────────────────────────────────┘
```
